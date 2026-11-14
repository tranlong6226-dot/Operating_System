#include "rtsfr_recovery.h"

#include <limits.h>
#include <string.h>

#if ( configRTSFR_ENABLE == 1 ) && ( INCLUDE_vTaskDelete == 1 )
extern UBaseType_t uxTaskGetDeletedTasksWaitingCleanUp(void);
#endif

typedef struct
{
    bool used;
    rtsfr_recovery_task_config_t config;
    char name[configMAX_TASK_NAME_LEN];
    TaskHandle_t active_handle;
    uint16_t generation;
} rtsfr_task_descriptor_t;

typedef struct
{
    bool active;
    bool quiesced;
    bool task_deleted;
    bool cancel_requested;
    bool first_progress_seen;
    rtsfr_decision_t decision;
    rtsfr_recovery_state_t state;
    uint16_t old_generation;
    uint16_t new_generation;
    TickType_t started_tick;
    TickType_t verify_deadline;
    TickType_t cleanup_deadline;
    UBaseType_t deleted_tasks_before;
    TaskHandle_t original_handle;
} rtsfr_recovery_transaction_t;

static bool g_initialized;
static rtsfr_task_descriptor_t g_tasks[configRTSFR_MAX_TASKS];
static rtsfr_recovery_hooks_t g_hooks;
static rtsfr_recovery_transaction_t g_transaction;
static rtsfr_recovery_result_t g_last_result;
static uint32_t g_event_history[configRTSFR_EVENT_HISTORY_SIZE];
static uint8_t g_event_history_head;
static uint8_t g_event_history_count;

static bool prv_event_seen(uint32_t event_id)
{
    uint8_t index;
    for (index = 0U; index < g_event_history_count; index++)
    {
        if (g_event_history[index] == event_id)
        {
            return true;
        }
    }
    return false;
}

static void prv_remember_event(uint32_t event_id)
{
    g_event_history[g_event_history_head] = event_id;
    g_event_history_head = (uint8_t)((g_event_history_head + 1U) %
                                     configRTSFR_EVENT_HISTORY_SIZE);
    if (g_event_history_count < configRTSFR_EVENT_HISTORY_SIZE)
    {
        g_event_history_count++;
    }
}

static rtsfr_task_descriptor_t *prv_find_task(uint8_t task_id)
{
    uint32_t index;

    if ((task_id == 0U) || (task_id > configRTSFR_MAX_TASKS))
    {
        return NULL;
    }

    index = (uint32_t)task_id - 1U;
    if (!g_tasks[index].used)
    {
        return NULL;
    }

    return &g_tasks[index];
}

static bool prv_tick_reached(TickType_t now, TickType_t deadline)
{
    const TickType_t half_range = (TickType_t)(~(TickType_t)0U >> 1U);
    return (TickType_t)(now - deadline) <= half_range;
}

static void prv_trace(rtsfr_recovery_trace_event_t event,
                      uint16_t reason_code)
{
    rtsfr_recovery_trace_t trace;

    if (g_hooks.trace == NULL)
    {
        return;
    }

    trace.event_id = g_transaction.decision.event_id;
    trace.task_id = g_transaction.decision.task_id;
    trace.generation = (g_transaction.new_generation != 0U)
                           ? g_transaction.new_generation
                           : g_transaction.old_generation;
    trace.state = g_transaction.state;
    trace.event = event;
    trace.reason_code = reason_code;
    trace.tick = xTaskGetTickCount();
    g_hooks.trace(&trace);
}

static rtsfr_status_t prv_finish(rtsfr_recovery_outcome_t outcome,
                                 uint16_t reason_code,
                                 bool enter_safe_mode)
{
    rtsfr_recovery_result_t result;

    result.event_id = g_transaction.decision.event_id;
    result.task_id = g_transaction.decision.task_id;
    result.old_generation = g_transaction.old_generation;
    result.new_generation = g_transaction.new_generation;
    result.outcome = outcome;
    result.reason_code = reason_code;
    result.started_tick = g_transaction.started_tick;
    result.finished_tick = xTaskGetTickCount();

    if (enter_safe_mode)
    {
        g_transaction.state = RTSFR_REC_STATE_SAFE_MODE;
        (void)rtsfr_safe_mode_enter(result.event_id,
                                    result.task_id,
                                    (result.new_generation != 0U)
                                        ? result.new_generation
                                        : result.old_generation,
                                    reason_code);
        prv_trace(RTSFR_TRACE_SAFE_MODE, reason_code);
    }
    else
    {
        g_transaction.state = RTSFR_REC_STATE_COMPLETE;
    }

    prv_trace(RTSFR_TRACE_RECOVERY_RESULT, reason_code);

    taskENTER_CRITICAL();
    {
        g_last_result = result;
        g_transaction.active = false;
        g_transaction.first_progress_seen = false;
    }
    taskEXIT_CRITICAL();

    if (g_hooks.record_result != NULL)
    {
        g_hooks.record_result(&result);
    }

    if (outcome == RTSFR_RECOVERY_OK)
    {
        return RTSFR_OK;
    }
    if (outcome == RTSFR_RECOVERY_STALE)
    {
        return RTSFR_ERR_STALE;
    }
    if (outcome == RTSFR_RECOVERY_VERIFY_TIMEOUT)
    {
        return RTSFR_ERR_TIMEOUT;
    }
    if (outcome == RTSFR_RECOVERY_REJECTED)
    {
        return enter_safe_mode ? RTSFR_ERR_UNSAFE : RTSFR_OK;
    }

    return enter_safe_mode ? RTSFR_ERR_UNSAFE : RTSFR_ERR_INTERNAL;
}

static bool prv_resource_is_safe(const rtsfr_resource_summary_t *summary,
                                 uint16_t *reason_code)
{
    if (summary->mutex_held_count > 0U)
    {
        *reason_code = RTSFR_REASON_MUTEX_HELD;
        return false;
    }
    if (summary->unknown_resource_count > 0U)
    {
        *reason_code = RTSFR_REASON_UNKNOWN_RESOURCE;
        return false;
    }
    if (summary->has_inflight_io)
    {
        *reason_code = RTSFR_REASON_IO_IN_FLIGHT;
        return false;
    }
    if (!summary->can_reclaim_all)
    {
        *reason_code = RTSFR_REASON_RESOURCE_NOT_RECLAIMABLE;
        return false;
    }

    *reason_code = RTSFR_REASON_NONE;
    return true;
}

void rtsfr_recovery_init(const rtsfr_recovery_hooks_t *hooks)
{
    taskENTER_CRITICAL();
    {
        (void)memset(g_tasks, 0, sizeof(g_tasks));
        (void)memset(&g_hooks, 0, sizeof(g_hooks));
        (void)memset(&g_transaction, 0, sizeof(g_transaction));
        (void)memset(&g_last_result, 0, sizeof(g_last_result));
        (void)memset(g_event_history, 0, sizeof(g_event_history));

        if (hooks != NULL)
        {
            g_hooks = *hooks;
        }

        g_transaction.state = RTSFR_REC_STATE_IDLE;
        g_event_history_head = 0U;
        g_event_history_count = 0U;
        g_initialized = true;
    }
    taskEXIT_CRITICAL();

    rtsfr_safe_mode_init(g_hooks.apply_safe_output);
}

rtsfr_status_t rtsfr_recovery_register_task(
    const rtsfr_recovery_task_config_t *config,
    TaskHandle_t active_handle)
{
    rtsfr_task_descriptor_t *descriptor;
    size_t name_length;

    if (!g_initialized || (config == NULL) || (active_handle == NULL) ||
        (config->task_id == 0U) ||
        (config->task_id > configRTSFR_MAX_TASKS) ||
        (config->initial_generation == 0U) || (config->entry == NULL) ||
        (config->stack_depth == 0U) ||
        (config->priority >= configMAX_PRIORITIES) ||
        (config->name == NULL) || (config->verify_timeout_ticks == 0U))
    {
        return RTSFR_ERR_INVALID;
    }

    name_length = strlen(config->name);
    if ((name_length == 0U) || (name_length >= configMAX_TASK_NAME_LEN))
    {
        return RTSFR_ERR_INVALID;
    }

    descriptor = &g_tasks[(uint32_t)config->task_id - 1U];

    taskENTER_CRITICAL();
    {
        if (descriptor->used)
        {
            taskEXIT_CRITICAL();
            return RTSFR_ERR_BUSY;
        }

        descriptor->used = true;
        descriptor->config = *config;
        (void)memcpy(descriptor->name, config->name, name_length + 1U);
        descriptor->config.name = descriptor->name;
        descriptor->active_handle = active_handle;
        descriptor->generation = config->initial_generation;
    }
    taskEXIT_CRITICAL();

    return RTSFR_OK;
}

rtsfr_status_t rtsfr_recovery_request(const rtsfr_decision_t *decision)
{
    rtsfr_task_descriptor_t *descriptor;

    if (!g_initialized || (decision == NULL) || (decision->event_id == 0U))
    {
        return RTSFR_ERR_INVALID;
    }

    if (decision->action == RTSFR_ACT_SAFE_MODE)
    {
        descriptor = prv_find_task(decision->task_id);
        if ((descriptor != NULL) &&
            (descriptor->generation == decision->generation) &&
            (descriptor->active_handle != NULL) &&
            (descriptor->active_handle != xTaskGetCurrentTaskHandle()))
        {
            vTaskSuspend(descriptor->active_handle);
        }
        g_transaction.state = RTSFR_REC_STATE_SAFE_MODE;
        return rtsfr_safe_mode_enter(decision->event_id,
                                     decision->task_id,
                                     decision->generation,
                                     decision->reason_code);
    }

    if (decision->action == RTSFR_ACT_REQUEST_RESET)
    {
        if (g_hooks.request_reset == NULL)
        {
            return RTSFR_ERR_NOT_READY;
        }
        descriptor = prv_find_task(decision->task_id);
        if ((descriptor != NULL) &&
            (descriptor->generation == decision->generation) &&
            (descriptor->active_handle != NULL) &&
            (descriptor->active_handle != xTaskGetCurrentTaskHandle()))
        {
            vTaskSuspend(descriptor->active_handle);
        }
        g_hooks.request_reset(decision->reason_code);
        return RTSFR_OK;
    }

    if (decision->action != RTSFR_ACT_RESTART)
    {
        return RTSFR_ERR_INVALID;
    }

    if (rtsfr_safe_mode_is_active())
    {
        return RTSFR_ERR_UNSAFE;
    }

    descriptor = prv_find_task(decision->task_id);
    if (descriptor == NULL)
    {
        return RTSFR_ERR_INVALID;
    }

    taskENTER_CRITICAL();
    {
        if (g_transaction.active)
        {
            taskEXIT_CRITICAL();
            return RTSFR_ERR_BUSY;
        }
        if (prv_event_seen(decision->event_id))
        {
            taskEXIT_CRITICAL();
            return RTSFR_ERR_STALE;
        }
        if ((descriptor->generation != decision->generation) ||
            (descriptor->active_handle == NULL))
        {
            taskEXIT_CRITICAL();
            return RTSFR_ERR_STALE;
        }

        (void)memset(&g_transaction, 0, sizeof(g_transaction));
        g_transaction.active = true;
        g_transaction.decision = *decision;
        g_transaction.state = RTSFR_REC_STATE_QUIESCING;
        g_transaction.old_generation = decision->generation;
        g_transaction.started_tick = xTaskGetTickCount();
        g_transaction.original_handle = descriptor->active_handle;
        prv_remember_event(decision->event_id);
    }
    taskEXIT_CRITICAL();

    prv_trace(RTSFR_TRACE_REQUEST, RTSFR_REASON_NONE);
    return RTSFR_OK;
}

rtsfr_status_t rtsfr_recovery_cancel_pending(uint32_t event_id,
                                              uint8_t task_id,
                                              uint16_t generation)
{
    rtsfr_status_t status = RTSFR_ERR_STALE;

    if (!g_initialized || (event_id == 0U) || (task_id == 0U) ||
        (generation == 0U))
    {
        return RTSFR_ERR_INVALID;
    }

    taskENTER_CRITICAL();
    {
        if (g_transaction.active &&
            (g_transaction.decision.event_id == event_id) &&
            (g_transaction.decision.task_id == task_id) &&
            (g_transaction.old_generation == generation))
        {
            if ((g_transaction.state == RTSFR_REC_STATE_QUIESCING) ||
                (g_transaction.state == RTSFR_REC_STATE_REVALIDATING))
            {
                g_transaction.cancel_requested = true;
                status = RTSFR_OK;
            }
            else
            {
                status = RTSFR_ERR_BUSY;
            }
        }
    }
    taskEXIT_CRITICAL();

    return status;
}

rtsfr_status_t rtsfr_recovery_process(void)
{
    rtsfr_task_descriptor_t *descriptor;
    rtsfr_resource_summary_t summary;
    rtsfr_status_t status;
    TaskHandle_t handle;
    uint16_t reason_code;
    uint16_t next_generation;
    bool cancel_requested;
    bool first_progress_seen;

    if (!g_initialized)
    {
        return RTSFR_ERR_NOT_READY;
    }
    if (!g_transaction.active)
    {
        return RTSFR_ERR_NO_RESOURCE;
    }

    descriptor = prv_find_task(g_transaction.decision.task_id);
    if (descriptor == NULL)
    {
        return prv_finish(RTSFR_RECOVERY_INTERNAL_ERROR,
                          RTSFR_REASON_INTERNAL,
                          true);
    }

    taskENTER_CRITICAL();
    {
        cancel_requested = g_transaction.cancel_requested;
    }
    taskEXIT_CRITICAL();

    if (cancel_requested &&
        ((g_transaction.state == RTSFR_REC_STATE_QUIESCING) ||
         (g_transaction.state == RTSFR_REC_STATE_REVALIDATING)))
    {
        if (g_transaction.quiesced)
        {
            if ((descriptor->active_handle != g_transaction.original_handle) ||
                (descriptor->generation != g_transaction.old_generation) ||
                g_transaction.task_deleted)
            {
                return prv_finish(RTSFR_RECOVERY_INTERNAL_ERROR,
                                  RTSFR_REASON_STALE_GENERATION,
                                  true);
            }

            vTaskResume(g_transaction.original_handle);
            g_transaction.quiesced = false;
        }

        prv_trace(RTSFR_TRACE_CANCELLED,
                  RTSFR_REASON_FALSE_ALARM_ROLLBACK);
        return prv_finish(RTSFR_RECOVERY_REJECTED,
                          RTSFR_REASON_FALSE_ALARM_ROLLBACK,
                          false);
    }

    switch (g_transaction.state)
    {
        case RTSFR_REC_STATE_QUIESCING:
            handle = descriptor->active_handle;
            if ((handle == NULL) ||
                (descriptor->generation != g_transaction.old_generation))
            {
                return prv_finish(RTSFR_RECOVERY_STALE,
                                  RTSFR_REASON_STALE_GENERATION,
                                  false);
            }
            if (handle == xTaskGetCurrentTaskHandle())
            {
                return prv_finish(RTSFR_RECOVERY_REJECTED,
                                  RTSFR_REASON_SELF_RECOVERY_FORBIDDEN,
                                  true);
            }

            vTaskSuspend(handle);
            g_transaction.quiesced = true;
            g_transaction.state = RTSFR_REC_STATE_REVALIDATING;
            prv_trace(RTSFR_TRACE_QUIESCED, RTSFR_REASON_NONE);
            return RTSFR_OK;

        case RTSFR_REC_STATE_REVALIDATING:
            if (g_hooks.resource_snapshot == NULL)
            {
                return prv_finish(RTSFR_RECOVERY_UNSAFE_RESOURCE,
                                  RTSFR_REASON_SNAPSHOT_UNAVAILABLE,
                                  true);
            }

            status = g_hooks.resource_snapshot(g_transaction.decision.task_id,
                                               g_transaction.old_generation,
                                               &summary);
            if (status == RTSFR_ERR_STALE)
            {
                return prv_finish(RTSFR_RECOVERY_INTERNAL_ERROR,
                                  RTSFR_REASON_STALE_GENERATION,
                                  true);
            }
            if (status != RTSFR_OK)
            {
                return prv_finish(RTSFR_RECOVERY_UNSAFE_RESOURCE,
                                  RTSFR_REASON_SNAPSHOT_UNAVAILABLE,
                                  true);
            }
            if ((summary.task_id != g_transaction.decision.task_id) ||
                (summary.generation != g_transaction.old_generation))
            {
                return prv_finish(RTSFR_RECOVERY_INTERNAL_ERROR,
                                  RTSFR_REASON_SNAPSHOT_MISMATCH,
                                  true);
            }
            if (!prv_resource_is_safe(&summary, &reason_code))
            {
                prv_trace(RTSFR_TRACE_RESOURCE_UNSAFE, reason_code);
                return prv_finish(RTSFR_RECOVERY_UNSAFE_RESOURCE,
                                  reason_code,
                                  true);
            }

            prv_trace(RTSFR_TRACE_RESOURCE_SAFE, RTSFR_REASON_NONE);
            g_transaction.state = (summary.managed_buffers_owned > 0U)
                                      ? RTSFR_REC_STATE_CLEANUP
                                      : RTSFR_REC_STATE_DELETING;
            return RTSFR_OK;

        case RTSFR_REC_STATE_CLEANUP:
            if (g_hooks.resource_cleanup == NULL)
            {
                return prv_finish(RTSFR_RECOVERY_CLEANUP_FAILED,
                                  RTSFR_REASON_CLEANUP_UNAVAILABLE,
                                  true);
            }

            status = g_hooks.resource_cleanup(g_transaction.decision.task_id,
                                              g_transaction.old_generation);
            if (status != RTSFR_OK)
            {
                return prv_finish(RTSFR_RECOVERY_CLEANUP_FAILED,
                                  RTSFR_REASON_CLEANUP_FAILED,
                                  true);
            }

            status = g_hooks.resource_snapshot(g_transaction.decision.task_id,
                                               g_transaction.old_generation,
                                               &summary);
            if ((status != RTSFR_OK) ||
                (summary.task_id != g_transaction.decision.task_id) ||
                (summary.generation != g_transaction.old_generation) ||
                !prv_resource_is_safe(&summary, &reason_code) ||
                (summary.managed_buffers_owned != 0U))
            {
                return prv_finish(RTSFR_RECOVERY_CLEANUP_FAILED,
                                  RTSFR_REASON_CLEANUP_INCOMPLETE,
                                  true);
            }

            g_transaction.state = RTSFR_REC_STATE_DELETING;
            prv_trace(RTSFR_TRACE_CLEANUP_OK, RTSFR_REASON_NONE);
            return RTSFR_OK;

        case RTSFR_REC_STATE_DELETING:
            handle = descriptor->active_handle;
            if ((handle == NULL) ||
                (descriptor->generation != g_transaction.old_generation))
            {
                return prv_finish(RTSFR_RECOVERY_INTERNAL_ERROR,
                                  RTSFR_REASON_STALE_GENERATION,
                                  true);
            }

            if (g_hooks.prepare_delete != NULL)
            {
                status = g_hooks.prepare_delete(g_transaction.decision.task_id,
                                                g_transaction.old_generation,
                                                handle);
                if (status != RTSFR_OK)
                {
                    return prv_finish(RTSFR_RECOVERY_INTERNAL_ERROR,
                                      RTSFR_REASON_PREPARE_DELETE_FAILED,
                                      true);
                }
            }

#if ( configRTSFR_ENABLE == 1 ) && ( INCLUDE_vTaskDelete == 1 )
            g_transaction.deleted_tasks_before =
                uxTaskGetDeletedTasksWaitingCleanUp();
#else
            g_transaction.deleted_tasks_before = 0U;
#endif
            vTaskDelete(handle);
            descriptor->active_handle = NULL;
            g_transaction.quiesced = false;
            g_transaction.task_deleted = true;
            g_transaction.cleanup_deadline = xTaskGetTickCount() +
                pdMS_TO_TICKS(configRTSFR_KERNEL_CLEANUP_TIMEOUT_MS);
            g_transaction.state = RTSFR_REC_STATE_WAIT_KERNEL_CLEANUP;
            prv_trace(RTSFR_TRACE_TASK_DELETED, RTSFR_REASON_NONE);
            return RTSFR_OK;

        case RTSFR_REC_STATE_WAIT_KERNEL_CLEANUP:
#if ( configRTSFR_ENABLE == 1 ) && ( INCLUDE_vTaskDelete == 1 )
            if (uxTaskGetDeletedTasksWaitingCleanUp() <=
                g_transaction.deleted_tasks_before)
            {
                g_transaction.state = RTSFR_REC_STATE_RECREATING;
                prv_trace(RTSFR_TRACE_KERNEL_CLEANUP_OK,
                          RTSFR_REASON_NONE);
                return RTSFR_OK;
            }
#else
            g_transaction.state = RTSFR_REC_STATE_RECREATING;
            return RTSFR_OK;
#endif
            if (prv_tick_reached(xTaskGetTickCount(),
                                 g_transaction.cleanup_deadline))
            {
                return prv_finish(RTSFR_RECOVERY_CREATE_FAILED,
                                  RTSFR_REASON_KERNEL_CLEANUP_TIMEOUT,
                                  true);
            }
            return RTSFR_ERR_BUSY;

        case RTSFR_REC_STATE_RECREATING:
            if (g_transaction.old_generation == UINT16_MAX)
            {
                return prv_finish(RTSFR_RECOVERY_CREATE_FAILED,
                                  RTSFR_REASON_GENERATION_EXHAUSTED,
                                  true);
            }
            next_generation = (uint16_t)(g_transaction.old_generation + 1U);

            if (descriptor->config.reset_context != NULL)
            {
                status = descriptor->config.reset_context(
                    descriptor->config.task_id,
                    next_generation,
                    descriptor->config.argument);
                if (status != RTSFR_OK)
                {
                    return prv_finish(RTSFR_RECOVERY_CREATE_FAILED,
                                      RTSFR_REASON_CONTEXT_RESET_FAILED,
                                      true);
                }
            }

            handle = NULL;
            if (g_hooks.create_task != NULL)
            {
                status = g_hooks.create_task(descriptor->config.entry,
                                             descriptor->name,
                                             descriptor->config.stack_depth,
                                             descriptor->config.argument,
                                             descriptor->config.priority,
                                             &handle);
            }
            else
            {
                status = (xTaskCreate(descriptor->config.entry,
                                      descriptor->name,
                                      descriptor->config.stack_depth,
                                      descriptor->config.argument,
                                      descriptor->config.priority,
                                      &handle) == pdPASS)
                             ? RTSFR_OK
                             : RTSFR_ERR_NO_RESOURCE;
            }
            if ((status != RTSFR_OK) || (handle == NULL))
            {
                return prv_finish(RTSFR_RECOVERY_CREATE_FAILED,
                                  RTSFR_REASON_CREATE_FAILED,
                                  true);
            }

            if (g_hooks.publish_generation != NULL)
            {
                status = g_hooks.publish_generation(
                    descriptor->config.task_id,
                    next_generation,
                    handle);
                if (status != RTSFR_OK)
                {
                    vTaskDelete(handle);
                    return prv_finish(RTSFR_RECOVERY_CREATE_FAILED,
                                      RTSFR_REASON_GENERATION_PUBLISH_FAILED,
                                      true);
                }
            }

            taskENTER_CRITICAL();
            {
                descriptor->active_handle = handle;
                descriptor->generation = next_generation;
                g_transaction.new_generation = next_generation;
                g_transaction.verify_deadline =
                    xTaskGetTickCount() + descriptor->config.verify_timeout_ticks;
                g_transaction.state = RTSFR_REC_STATE_VERIFYING;
            }
            taskEXIT_CRITICAL();

            prv_trace(RTSFR_TRACE_TASK_CREATED, RTSFR_REASON_NONE);
            return RTSFR_OK;

        case RTSFR_REC_STATE_VERIFYING:
            taskENTER_CRITICAL();
            {
                first_progress_seen = g_transaction.first_progress_seen;
            }
            taskEXIT_CRITICAL();

            if (first_progress_seen)
            {
                prv_trace(RTSFR_TRACE_FIRST_PROGRESS,
                          RTSFR_REASON_FIRST_PROGRESS);
                return prv_finish(RTSFR_RECOVERY_OK,
                                  RTSFR_REASON_FIRST_PROGRESS,
                                  false);
            }
            if (prv_tick_reached(xTaskGetTickCount(),
                                 g_transaction.verify_deadline))
            {
                /*
                 * The new instance exists but has not proved useful progress.
                 * Isolate it before latching safe-mode so it cannot overwrite
                 * the safe output after the callback applies it.
                 */
                handle = descriptor->active_handle;
                if ((handle != NULL) &&
                    (handle != xTaskGetCurrentTaskHandle()))
                {
                    vTaskSuspend(handle);
                    g_transaction.quiesced = true;
                }

                return prv_finish(RTSFR_RECOVERY_VERIFY_TIMEOUT,
                                  RTSFR_REASON_VERIFY_TIMEOUT,
                                  true);
            }
            return RTSFR_ERR_BUSY;

        default:
            return prv_finish(RTSFR_RECOVERY_INTERNAL_ERROR,
                              RTSFR_REASON_INTERNAL,
                              true);
    }
}

void rtsfr_recovery_on_first_progress(uint8_t task_id,
                                      uint16_t generation)
{
    taskENTER_CRITICAL();
    {
        if (g_transaction.active &&
            (g_transaction.state == RTSFR_REC_STATE_VERIFYING) &&
            (g_transaction.decision.task_id == task_id) &&
            (g_transaction.new_generation == generation))
        {
            g_transaction.first_progress_seen = true;
        }
    }
    taskEXIT_CRITICAL();
}

rtsfr_recovery_state_t rtsfr_recovery_get_state(void)
{
    rtsfr_recovery_state_t state;
    bool safe_mode_active;

    safe_mode_active = rtsfr_safe_mode_is_active();
    taskENTER_CRITICAL();
    {
        state = safe_mode_active ? RTSFR_REC_STATE_SAFE_MODE
                                 : g_transaction.state;
    }
    taskEXIT_CRITICAL();

    return state;
}

rtsfr_status_t rtsfr_recovery_get_result(uint32_t event_id,
                                         rtsfr_recovery_result_t *out)
{
    if ((event_id == 0U) || (out == NULL))
    {
        return RTSFR_ERR_INVALID;
    }

    taskENTER_CRITICAL();
    {
        if (g_last_result.event_id != event_id)
        {
            taskEXIT_CRITICAL();
            return RTSFR_ERR_NO_RESOURCE;
        }
        *out = g_last_result;
    }
    taskEXIT_CRITICAL();

    return RTSFR_OK;
}

rtsfr_status_t rtsfr_recovery_get_active_instance(uint8_t task_id,
                                                  uint16_t *generation,
                                                  TaskHandle_t *handle)
{
    rtsfr_task_descriptor_t *descriptor;

    if ((generation == NULL) || (handle == NULL))
    {
        return RTSFR_ERR_INVALID;
    }

    descriptor = prv_find_task(task_id);
    if (descriptor == NULL)
    {
        return RTSFR_ERR_INVALID;
    }

    taskENTER_CRITICAL();
    {
        *generation = descriptor->generation;
        *handle = descriptor->active_handle;
    }
    taskEXIT_CRITICAL();

    return (*handle != NULL) ? RTSFR_OK : RTSFR_ERR_NOT_READY;
}
