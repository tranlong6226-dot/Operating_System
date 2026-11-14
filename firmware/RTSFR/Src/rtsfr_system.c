#include "rtsfr_system.h"

#include <string.h>

#include "fault_scenarios.h"
#include "cycle_counter.h"
#include "iwdg.h"
#include "main.h"
#include "m5_test_runner.h"
#include "rtsfr_detect.h"
#include "rtsfr_health.h"
#include "rtsfr_m5_adapter.h"
#include "rtsfr_policy.h"
#include "rtsfr_recovery.h"
#include "rtsfr_resource.h"
#include "rtsfr_safe_mode.h"
#include "rtsfr_trace.h"
#include "rtsfr_verify.h"
#include "semphr.h"

#define RTSFR_SENSOR_TASK_ID  1U
#define RTSFR_COMM_TASK_ID    2U
#define RTSFR_CONTROL_TASK_ID 3U
#define RTSFR_DEMO_STACK_WORDS 160U
#define RTSFR_DEMO_PRIORITY   (tskIDLE_PRIORITY + 1U)
#define RTSFR_PROGRESS_TIMEOUT pdMS_TO_TICKS(500U)
#define RTSFR_VERIFY_TIMEOUT   pdMS_TO_TICKS(500U)
#define RTSFR_RECOVERY_BUDGET  pdMS_TO_TICKS(1500U)
#define RTSFR_STRESS_RECOVERIES 100U
#define RTSFR_TEST_SETTLE_TIME   pdMS_TO_TICKS(500U)

typedef struct
{
    uint8_t task_id;
    volatile uint16_t generation;
    volatile bool first_progress_pending;
    volatile bool expected_wait_test;
    TaskHandle_t handle;
    uint32_t active_event_id;
} rtsfr_demo_context_t;

static rtsfr_demo_context_t g_contexts[configRTSFR_MAX_TASKS];
static SemaphoreHandle_t g_control_mutex;
static bool g_ready;
static bool g_safe_output_active;
static bool g_reset_requested;
static uint32_t g_supervisor_sequence;
static TickType_t g_recovery_started_tick;
static rtsfr_recovery_result_t g_last_recovery;
static bool g_have_recovery_result;
static uint8_t g_active_system_test;
static TickType_t g_system_test_started;
static uint32_t g_test_progress_baseline;
static uint16_t g_test_initial_generation;
static uint16_t g_test_recovery_count;
static uint8_t g_test_phase;
static TickType_t g_test_deadline;
static size_t g_test_heap_baseline;
static rtsfr_status_t g_test_observed_status;
static bool g_force_next_create_failure;
static uint32_t g_detect_overflow_seen;
static uint32_t g_monitor_polls;
static uint32_t g_monitor_last_cycles;
static uint32_t g_monitor_max_cycles;
static uint16_t g_successful_recoveries;
static uint16_t g_unsafe_restarts_prevented;
static uint16_t g_false_alarms_rolled_back;

static rtsfr_demo_context_t *prv_context(uint8_t task_id)
{
    if ((task_id == 0U) || (task_id > configRTSFR_MAX_TASKS))
    {
        return NULL;
    }
    return &g_contexts[task_id - 1U];
}

static void prv_report_progress(rtsfr_demo_context_t *context)
{
    if (rtsfr_report_progress(context->task_id) == RTSFR_OK)
    {
        if (!g_safe_output_active)
        {
            if (context->task_id == RTSFR_SENSOR_TASK_ID)
            {
                HAL_GPIO_TogglePin(SENSOR_LED_GPIO_Port, SENSOR_LED_Pin);
            }
            else if (context->task_id == RTSFR_COMM_TASK_ID)
            {
                HAL_GPIO_TogglePin(COMM_LED_GPIO_Port, COMM_LED_Pin);
            }
            else
            {
                HAL_GPIO_TogglePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin);
            }
        }
        rtsfr_recovery_on_first_progress(context->task_id,
                                         context->generation);
        if (context->first_progress_pending)
        {
            context->first_progress_pending = false;
            rtsfr_m5_on_first_progress(context->active_event_id,
                                       context->task_id,
                                       context->generation);
        }
    }
}

static void prv_sensor_task(void *argument)
{
    rtsfr_demo_context_t *context = argument;
    for (;;)
    {
        if (context->expected_wait_test)
        {
            context->expected_wait_test = false;
            (void)rtsfr_begin_expected_wait(context->task_id,
                                            pdMS_TO_TICKS(1500U));
            (void)rtsfr_fault_checkpoint(context->task_id,
                                         context->generation,
                                         M5_POINT_WORK);
            (void)rtsfr_end_expected_wait(context->task_id);
            prv_report_progress(context);
        }
        else if (rtsfr_fault_checkpoint(context->task_id,
                                        context->generation,
                                        M5_POINT_WORK))
        {
            prv_report_progress(context);
        }
        vTaskDelay(pdMS_TO_TICKS(100U));
    }
}

static void prv_comm_task(void *argument)
{
    rtsfr_demo_context_t *context = argument;
    uint8_t buffer_index;
    for (;;)
    {
        if (rtsfr_buffer_acquire(context->task_id, &buffer_index) == RTSFR_OK)
        {
            (void)rtsfr_fault_checkpoint(context->task_id,
                                         context->generation,
                                         M5_POINT_BUFFER_HELD);
            (void)rtsfr_buffer_release(context->task_id, buffer_index);
        }
        if (xSemaphoreTake(g_control_mutex, pdMS_TO_TICKS(50U)) == pdTRUE)
        {
            (void)rtsfr_fault_checkpoint(context->task_id,
                                         context->generation,
                                         M5_POINT_MUTEX_HELD);
            (void)xSemaphoreGive(g_control_mutex);
        }
        if (rtsfr_fault_checkpoint(context->task_id,
                                   context->generation,
                                   M5_POINT_WORK))
        {
            prv_report_progress(context);
        }
        vTaskDelay(pdMS_TO_TICKS(150U));
    }
}

static void prv_control_task(void *argument)
{
    rtsfr_demo_context_t *context = argument;
    for (;;)
    {
        if (xSemaphoreTake(g_control_mutex, pdMS_TO_TICKS(50U)) == pdTRUE)
        {
            (void)rtsfr_fault_checkpoint(context->task_id,
                                         context->generation,
                                         M5_POINT_MUTEX_HELD);
            (void)xSemaphoreGive(g_control_mutex);
        }
        if (rtsfr_fault_checkpoint(context->task_id,
                                   context->generation,
                                   M5_POINT_WORK))
        {
            prv_report_progress(context);
        }
        vTaskDelay(pdMS_TO_TICKS(200U));
    }
}

static TaskFunction_t prv_task_entry(uint8_t task_id)
{
    if (task_id == RTSFR_SENSOR_TASK_ID)
    {
        return prv_sensor_task;
    }
    if (task_id == RTSFR_COMM_TASK_ID)
    {
        return prv_comm_task;
    }
    return prv_control_task;
}

static const char *prv_task_name(uint8_t task_id)
{
    if (task_id == RTSFR_SENSOR_TASK_ID)
    {
        return "SensorTask";
    }
    if (task_id == RTSFR_COMM_TASK_ID)
    {
        return "CommTask";
    }
    return "ControlTask";
}

static rtsfr_criticality_t prv_task_criticality(uint8_t task_id)
{
    return (task_id == RTSFR_CONTROL_TASK_ID)
               ? RTSFR_CRIT_HIGH
               : RTSFR_CRIT_MEDIUM;
}

static rtsfr_status_t prv_create_task(TaskFunction_t entry,
                                      const char *name,
                                      configSTACK_DEPTH_TYPE stack_depth,
                                      void *argument,
                                      UBaseType_t priority,
                                      TaskHandle_t *out_handle)
{
    if ((entry == NULL) || (name == NULL) || (out_handle == NULL))
    {
        return RTSFR_ERR_INVALID;
    }
    if (g_force_next_create_failure)
    {
        g_force_next_create_failure = false;
        *out_handle = NULL;
        return RTSFR_ERR_NO_RESOURCE;
    }
    return (xTaskCreate(entry, name, stack_depth, argument, priority,
                        out_handle) == pdPASS)
               ? RTSFR_OK
               : RTSFR_ERR_NO_RESOURCE;
}

static rtsfr_status_t prv_reset_context(uint8_t task_id,
                                        uint16_t new_generation,
                                        void *argument)
{
    rtsfr_demo_context_t *context = argument;
    if ((context == NULL) || (context->task_id != task_id) ||
        (new_generation == 0U))
    {
        return RTSFR_ERR_INVALID;
    }
    context->generation = new_generation;
    context->first_progress_pending = true;
    context->expected_wait_test = false;
    return RTSFR_OK;
}

static rtsfr_status_t prv_prepare_delete(uint8_t task_id,
                                         uint16_t generation,
                                         TaskHandle_t handle)
{
    rtsfr_task_record_t record;
    if ((rtsfr_detect_get_record(task_id, &record) != RTSFR_OK) ||
        (record.generation != generation) ||
        (record.task_handle != handle))
    {
        return RTSFR_ERR_STALE;
    }
    rtsfr_detect_on_task_delete(handle);
    return RTSFR_OK;
}

static rtsfr_status_t prv_publish_generation(uint8_t task_id,
                                              uint16_t generation,
                                              TaskHandle_t new_handle)
{
    rtsfr_demo_context_t *context = prv_context(task_id);
    if ((context == NULL) || (new_handle == NULL) ||
        (context->generation != generation))
    {
        return RTSFR_ERR_INVALID;
    }
    context->handle = new_handle;
    rtsfr_detect_on_new_generation(task_id, new_handle, generation);
    return RTSFR_OK;
}

static void prv_record_result(const rtsfr_recovery_result_t *result)
{
    if (result == NULL)
    {
        return;
    }
    g_last_recovery = *result;
    g_have_recovery_result = true;
    if ((result->outcome == RTSFR_RECOVERY_OK) &&
        (g_successful_recoveries != UINT16_MAX))
    {
        g_successful_recoveries++;
    }
    rtsfr_policy_on_recovery_result(result);
    rtsfr_m5_on_recovery_result(result);
    (void)rtsfr_system_emit_snapshot();
}

static void prv_safe_output(const rtsfr_safe_mode_info_t *info)
{
    (void)info;
    g_safe_output_active = true;
    HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin, GPIO_PIN_RESET);
}

static void prv_request_reset(uint16_t reason_code)
{
    g_reset_requested = true;
    g_safe_output_active = true;
    HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin, GPIO_PIN_RESET);
    rtsfr_m5_emit(M5_STATUS, rtsfr_m5_test_active(), 0U, 0U, 0U,
                  reason_code, 0U);
}

static rtsfr_status_t prv_validate_instance(uint8_t task_id,
                                            uint16_t generation,
                                            bool caller_is_target)
{
    rtsfr_task_record_t record;
    if (rtsfr_detect_get_record(task_id, &record) != RTSFR_OK)
    {
        return RTSFR_ERR_NOT_FOUND;
    }
    if ((record.generation != generation) || (record.task_handle == NULL))
    {
        return RTSFR_ERR_STALE;
    }
    if (caller_is_target &&
        (record.task_handle != xTaskGetCurrentTaskHandle()))
    {
        return RTSFR_ERR_STALE;
    }
    return RTSFR_OK;
}

static rtsfr_status_t prv_report_injected_fault(uint8_t task_id,
                                                uint16_t generation,
                                                uint32_t event_id)
{
    return rtsfr_detect_report_fault(task_id, generation, event_id,
                                     RTSFR_FAULT_SELF_REPORTED);
}

static rtsfr_status_t prv_verify_snapshot(rtsfr_verify_snapshot_t *out)
{
    uint8_t task_id;
    uint8_t resource_count;
    rtsfr_resource_record_t resources[RTSFR_VERIFY_MAX_RESOURCES];
    if (out == NULL)
    {
        return RTSFR_ERR_INVALID;
    }
    (void)memset(out, 0, sizeof(*out));
    for (task_id = 1U; task_id <= configRTSFR_MAX_TASKS; task_id++)
    {
        rtsfr_task_record_t record;
        rtsfr_policy_task_state_t policy;
        rtsfr_resource_summary_t resource;
        rtsfr_verify_task_t *task = &out->tasks[task_id - 1U];
        if ((rtsfr_detect_get_record(task_id, &record) != RTSFR_OK) ||
            (rtsfr_policy_get_task_state(task_id, &policy) != RTSFR_OK) ||
            (rtsfr_resource_snapshot(task_id, record.generation,
                                     &resource) != RTSFR_OK))
        {
            return RTSFR_ERR_NOT_READY;
        }
        task->task_id = task_id;
        task->generation = record.generation;
        task->active = record.task_handle != NULL;
        task->restarting = (policy.state == RTSFR_TASK_QUIESCING) ||
                           (policy.state == RTSFR_TASK_CLEANUP) ||
                           (policy.state == RTSFR_TASK_RECREATE) ||
                           (policy.state == RTSFR_TASK_VERIFYING);
        task->resources_safe = resource.can_reclaim_all;
        task->first_progress = record.progress_seq > 0U;
        task->recovery_ok = g_have_recovery_result &&
                            (g_last_recovery.task_id == task_id) &&
                            (g_last_recovery.new_generation ==
                             record.generation) &&
                            (g_last_recovery.outcome == RTSFR_RECOVERY_OK);
        task->retries = policy.retry_count;
        task->retry_limit = policy.max_restarts;
    }
    if (rtsfr_resource_export(resources, RTSFR_VERIFY_MAX_RESOURCES,
                              &resource_count) != RTSFR_OK)
    {
        return RTSFR_ERR_NOT_READY;
    }
    for (task_id = 0U; task_id < resource_count; task_id++)
    {
        out->resources[task_id].resource_id =
            resources[task_id].resource_id;
        out->resources[task_id].owner_id =
            resources[task_id].owner_task_id;
        out->resources[task_id].owner_generation =
            resources[task_id].owner_generation;
        out->resources[task_id].state =
            (rtsfr_verify_resource_state_t)resources[task_id].state;
    }
    out->task_count = configRTSFR_MAX_TASKS;
    out->resource_count = resource_count;
    out->registry_complete = true;
    out->resources_complete = true;
    return RTSFR_OK;
}

static uint32_t prv_progress_sum(void)
{
    uint8_t task_id;
    uint32_t sum = 0U;
    for (task_id = 1U; task_id <= configRTSFR_MAX_TASKS; task_id++)
    {
        rtsfr_task_record_t record;
        if (rtsfr_detect_get_record(task_id, &record) == RTSFR_OK)
        {
            sum += record.progress_seq;
        }
    }
    return sum;
}

static rtsfr_status_t prv_arm_test_fault(uint8_t task_id, uint8_t scenario)
{
    rtsfr_demo_context_t *context = prv_context(task_id);
    if (context == NULL)
    {
        return RTSFR_ERR_INVALID;
    }
    return rtsfr_fault_arm(scenario, task_id, context->generation);
}

static rtsfr_status_t prv_start_test(uint8_t test_id)
{
    rtsfr_demo_context_t *context = NULL;
    uint8_t scenario;
    uint8_t task_id;
    uint16_t index;
    uint16_t anomaly_before;
    uint8_t buffer_index;
    rtsfr_task_record_t record;
    rtsfr_status_t status = RTSFR_OK;

    g_active_system_test = test_id;
    g_system_test_started = xTaskGetTickCount();
    g_test_progress_baseline = prv_progress_sum();
    g_have_recovery_result = false;
    g_test_initial_generation = 0U;
    g_test_recovery_count = 0U;
    g_test_phase = 0U;
    g_test_deadline = 0U;
    g_test_heap_baseline = 0U;
    g_test_observed_status = RTSFR_ERR_NOT_READY;
    g_force_next_create_failure = false;

    if (test_id == 1U)
    {
        return RTSFR_OK;
    }
#if RTSFR_M5_TEST_BUILD
    /* The classroom demos are independent acceptance scenarios. Do not let
       successful restarts in D02/D03 consume the retry budget used to prove
       the MUTEX_HELD decision in D04. Production policy and TC07 keep the
       cumulative retry history unchanged. */
    if ((test_id == 2U) || (test_id == 5U) || (test_id == 6U))
    {
        status = rtsfr_policy_reset_retry_window(RTSFR_COMM_TASK_ID);
        if (status != RTSFR_OK)
        {
            goto start_done;
        }
    }
#endif
    if ((test_id == 2U) || (test_id == 3U))
    {
        task_id = RTSFR_COMM_TASK_ID;
        scenario = (test_id == 2U) ? M5_FAULT_STOP_PROGRESS
                                   : M5_FAULT_BUSY_LOOP;
    }
    else if (test_id == 4U)
    {
        task_id = RTSFR_SENSOR_TASK_ID;
        scenario = M5_FAULT_EXPECTED_WAIT;
        context = prv_context(task_id);
        context->expected_wait_test = true;
    }
    else if (test_id == 5U)
    {
        task_id = RTSFR_COMM_TASK_ID;
        scenario = M5_FAULT_BUFFER_HELD;
    }
    else if (test_id == 6U)
    {
        task_id = RTSFR_COMM_TASK_ID;
        scenario = M5_FAULT_MUTEX_HELD;
    }
    else if (test_id == 7U)
    {
        status = rtsfr_policy_reset_retry_window(RTSFR_COMM_TASK_ID);
        if (status == RTSFR_OK)
        {
            status = rtsfr_policy_set_retry_policy(
                RTSFR_COMM_TASK_ID, configRTSFR_MAX_RESTARTS,
                configRTSFR_RETRY_WINDOW_TICKS);
        }
        if (status == RTSFR_OK)
        {
            status = prv_arm_test_fault(RTSFR_COMM_TASK_ID,
                                        M5_FAULT_SELF_REPORT_STALL);
        }
        goto start_done;
    }
    else if (test_id == 8U)
    {
        g_force_next_create_failure = true;
        status = prv_arm_test_fault(RTSFR_COMM_TASK_ID,
                                    M5_FAULT_SELF_REPORT_STALL);
        goto start_done;
    }
    else if (test_id == 9U)
    {
        context = prv_context(RTSFR_COMM_TASK_ID);
        g_test_initial_generation = context->generation;
        status = prv_arm_test_fault(RTSFR_COMM_TASK_ID,
                                    M5_FAULT_SELF_REPORT);
        goto start_done;
    }
    else if (test_id == 10U)
    {
        status = rtsfr_detect_get_record(RTSFR_COMM_TASK_ID, &record);
        if (status == RTSFR_OK)
        {
            uint16_t stale_generation = (record.generation == 1U)
                                            ? UINT16_MAX
                                            : (uint16_t)(record.generation - 1U);
            g_test_observed_status = rtsfr_detect_report_fault(
                RTSFR_COMM_TASK_ID, stale_generation, 0xE0000010UL,
                RTSFR_FAULT_SELF_REPORTED);
            status = RTSFR_OK;
        }
        goto start_done;
    }
    else if (test_id == 11U)
    {
        status = rtsfr_policy_reset_retry_window(RTSFR_COMM_TASK_ID);
        if (status == RTSFR_OK)
        {
            status = rtsfr_policy_set_retry_policy(
                RTSFR_COMM_TASK_ID,
                (uint8_t)(RTSFR_STRESS_RECOVERIES + 1U),
                pdMS_TO_TICKS(600000U));
        }
        if (status == RTSFR_OK)
        {
            g_test_heap_baseline = xPortGetFreeHeapSize();
            status = prv_arm_test_fault(RTSFR_COMM_TASK_ID,
                                        M5_FAULT_SELF_REPORT_STALL);
        }
        goto start_done;
    }
    else if (test_id == 12U)
    {
#if RTSFR_IWDG_CONFIG_CONFIRMED
        g_test_deadline = xTaskGetTickCount() + RTSFR_TEST_SETTLE_TIME;
        status = RTSFR_OK;
#else
        status = RTSFR_ERR_NOT_READY;
#endif
        goto start_done;
    }
    else if (test_id == 13U)
    {
        status = rtsfr_detect_test_saturate_event_queue();
        goto start_done;
    }
    else if (test_id == 14U)
    {
        anomaly_before = rtsfr_resource_get_anomaly_count();
        for (index = 0U; index < 100U; index++)
        {
            status = rtsfr_buffer_acquire(RTSFR_SENSOR_TASK_ID,
                                          &buffer_index);
            if (status != RTSFR_OK)
            {
                break;
            }
            status = rtsfr_buffer_release(RTSFR_SENSOR_TASK_ID,
                                          buffer_index);
            if (status != RTSFR_OK)
            {
                break;
            }
        }
        if ((status == RTSFR_OK) &&
            (rtsfr_resource_get_anomaly_count() == anomaly_before) &&
            rtsfr_verify_invariants())
        {
            g_test_observed_status = RTSFR_OK;
        }
        else
        {
            g_test_observed_status = RTSFR_ERR_UNSAFE;
        }
        status = RTSFR_OK;
        goto start_done;
    }
    else
    {
        status = RTSFR_ERR_NOT_READY;
        goto start_done;
    }
    context = prv_context(task_id);
    status = rtsfr_fault_arm(scenario, task_id, context->generation);

start_done:
    if (status != RTSFR_OK)
    {
        if (context != NULL)
        {
            context->expected_wait_test = false;
        }
        g_force_next_create_failure = false;
        g_active_system_test = 0U;
    }
    return status;
}

static void prv_finish_test(bool passed, uint16_t reason)
{
    uint8_t test_id = g_active_system_test;
    if (test_id == 0U)
    {
        return;
    }
    rtsfr_fault_disarm();
    g_force_next_create_failure = false;
    if (test_id == 11U)
    {
        (void)rtsfr_policy_set_retry_policy(
            RTSFR_COMM_TASK_ID, configRTSFR_MAX_RESTARTS,
            configRTSFR_RETRY_WINDOW_TICKS);
        (void)rtsfr_policy_reset_retry_window(RTSFR_COMM_TASK_ID);
    }
    g_active_system_test = 0U;
    (void)rtsfr_m5_test_finish(test_id, passed, reason);
}

static void prv_poll_test(void)
{
    TickType_t elapsed;
    rtsfr_safe_mode_info_t safe_info;
    if (g_active_system_test == 0U)
    {
        return;
    }
    elapsed = (TickType_t)(xTaskGetTickCount() - g_system_test_started);
    if (g_active_system_test == 1U)
    {
        if (elapsed >= pdMS_TO_TICKS(5000U))
        {
            prv_finish_test((prv_progress_sum() > g_test_progress_baseline) &&
                            !rtsfr_safe_mode_is_active(), 0U);
        }
    }
    else if ((g_active_system_test == 2U) ||
             (g_active_system_test == 3U) ||
             (g_active_system_test == 5U))
    {
        if (g_have_recovery_result)
        {
            prv_finish_test(g_last_recovery.outcome == RTSFR_RECOVERY_OK,
                            g_last_recovery.reason_code);
        }
    }
    else if (g_active_system_test == 4U)
    {
        if (elapsed >= pdMS_TO_TICKS(1800U))
        {
            prv_finish_test(!rtsfr_safe_mode_is_active() &&
                            (prv_progress_sum() > g_test_progress_baseline),
                            0U);
        }
    }
    else if ((g_active_system_test == 6U) &&
             (rtsfr_safe_mode_get_info(&safe_info) == RTSFR_OK))
    {
        prv_finish_test((safe_info.reason_code ==
                         RTSFR_POL_REASON_UNSAFE_MUTEX_HELD),
                        safe_info.reason_code);
    }
    else if (g_active_system_test == 7U)
    {
        if (rtsfr_safe_mode_get_info(&safe_info) == RTSFR_OK)
        {
            prv_finish_test((safe_info.reason_code ==
                             RTSFR_POL_REASON_RETRY_EXHAUSTED),
                            safe_info.reason_code);
        }
        else if (g_have_recovery_result)
        {
            if (g_last_recovery.outcome != RTSFR_RECOVERY_OK)
            {
                prv_finish_test(false, g_last_recovery.reason_code);
            }
            else
            {
                g_have_recovery_result = false;
                g_test_recovery_count++;
                if (prv_arm_test_fault(RTSFR_COMM_TASK_ID,
                                       M5_FAULT_SELF_REPORT_STALL) !=
                    RTSFR_OK)
                {
                    prv_finish_test(false, RTSFR_ERR_NOT_READY);
                }
            }
        }
    }
    else if ((g_active_system_test == 8U) && g_have_recovery_result)
    {
        prv_finish_test(
            (g_last_recovery.outcome == RTSFR_RECOVERY_CREATE_FAILED) &&
                rtsfr_safe_mode_is_active(),
            g_last_recovery.reason_code);
    }
    else if ((g_active_system_test == 9U) &&
             (elapsed >= pdMS_TO_TICKS(1000U)))
    {
        rtsfr_task_record_t record;
        bool unchanged =
            (rtsfr_detect_get_record(RTSFR_COMM_TASK_ID, &record) ==
             RTSFR_OK) &&
            (record.generation == g_test_initial_generation);
        prv_finish_test(unchanged && !g_have_recovery_result &&
                            !rtsfr_safe_mode_is_active() &&
                            (prv_progress_sum() > g_test_progress_baseline),
                        0U);
    }
    else if ((g_active_system_test == 10U) &&
             (elapsed >= pdMS_TO_TICKS(20U)))
    {
        prv_finish_test((g_test_observed_status == RTSFR_ERR_STALE) &&
                            !g_have_recovery_result &&
                            !rtsfr_safe_mode_is_active(),
                        (uint16_t)g_test_observed_status);
    }
    else if (g_active_system_test == 11U)
    {
        if ((g_test_phase == 0U) && g_have_recovery_result)
        {
            if (g_last_recovery.outcome != RTSFR_RECOVERY_OK)
            {
                prv_finish_test(false, g_last_recovery.reason_code);
            }
            else
            {
                g_have_recovery_result = false;
                g_test_recovery_count++;
                if (g_test_recovery_count >= RTSFR_STRESS_RECOVERIES)
                {
                    g_test_phase = 1U;
                    g_test_deadline = xTaskGetTickCount() +
                                      RTSFR_TEST_SETTLE_TIME;
                }
                else if (prv_arm_test_fault(
                             RTSFR_COMM_TASK_ID,
                             M5_FAULT_SELF_REPORT_STALL) != RTSFR_OK)
                {
                    prv_finish_test(false, RTSFR_ERR_NOT_READY);
                }
            }
        }
        else if ((g_test_phase == 1U) &&
                 ((TickType_t)(xTaskGetTickCount() - g_test_deadline) <
                  (TickType_t)0x80000000UL))
        {
            prv_finish_test(xPortGetFreeHeapSize() >= g_test_heap_baseline,
                            (uint16_t)(g_test_heap_baseline -
                                ((xPortGetFreeHeapSize() < g_test_heap_baseline)
                                     ? xPortGetFreeHeapSize()
                                     : g_test_heap_baseline)));
        }
    }
    else if ((g_active_system_test == 12U) &&
             ((TickType_t)(xTaskGetTickCount() - g_test_deadline) <
              (TickType_t)0x80000000UL))
    {
        rtsfr_m5_emit(M5_INJECT, 12U, 0U, 0U, 0U, 8U, 0U);
        rtsfr_status_t status = rtsfr_iwdg_board_reset_test();
        prv_finish_test(false, (uint16_t)status);
    }
    else if ((g_active_system_test == 13U) &&
             (rtsfr_safe_mode_get_info(&safe_info) == RTSFR_OK))
    {
        prv_finish_test((safe_info.reason_code ==
                         RTSFR_POL_REASON_EVENT_QUEUE_OVERFLOW),
                        safe_info.reason_code);
    }
    else if ((g_active_system_test == 14U) &&
             (elapsed >= pdMS_TO_TICKS(20U)))
    {
        prv_finish_test(g_test_observed_status == RTSFR_OK,
                        (uint16_t)g_test_observed_status);
    }
}

uint32_t rtsfr_detect_event_correlation(uint8_t task_id,
                                        uint16_t generation)
{
    return rtsfr_fault_correlation(task_id, generation);
}

static void prv_handle_fault(const rtsfr_fault_event_t *fault)
{
    rtsfr_task_record_t record;
    rtsfr_resource_summary_t resource;
    rtsfr_decision_t decision;
    rtsfr_demo_context_t *context;

    if (rtsfr_detect_get_record(fault->task_id, &record) != RTSFR_OK)
    {
        return;
    }
    if ((record.generation != fault->generation) ||
        ((record.state == RTSFR_TASK_HEALTHY) &&
         (record.progress_seq > fault->evidence)))
    {
        if ((record.generation == fault->generation) &&
            (record.progress_seq > fault->evidence) &&
            (g_false_alarms_rolled_back != UINT16_MAX))
        {
            g_false_alarms_rolled_back++;
        }
        return;
    }
    if (rtsfr_resource_snapshot(fault->task_id, fault->generation,
                                &resource) != RTSFR_OK)
    {
        (void)memset(&resource, 0, sizeof(resource));
        resource.task_id = fault->task_id;
        resource.generation = fault->generation;
        resource.unknown_resource_count = 1U;
        resource.can_reclaim_all = false;
    }
    rtsfr_m5_on_fault(fault->event_id, fault->task_id,
                      fault->generation, (uint16_t)fault->fault_type);
    rtsfr_m5_emit(
        M5_RESOURCE_CHECK, rtsfr_m5_test_active(), fault->event_id,
        fault->task_id, fault->generation, 0U,
        ((uint32_t)resource.mutex_held_count) |
            ((uint32_t)resource.managed_buffers_owned << 8U) |
            ((uint32_t)resource.unknown_resource_count << 16U) |
            (resource.has_inflight_io ? (1UL << 24U) : 0UL) |
            (resource.can_reclaim_all ? (1UL << 25U) : 0UL));
    decision = rtsfr_policy_evaluate(fault, &resource);
    rtsfr_m5_on_decision(&decision);
    if (((decision.action == RTSFR_ACT_SAFE_MODE) ||
         (decision.action == RTSFR_ACT_REQUEST_RESET)) &&
        ((decision.reason_code == RTSFR_POL_REASON_UNSAFE_MUTEX_HELD) ||
         (decision.reason_code == RTSFR_POL_REASON_UNSAFE_UNKNOWN_RES) ||
         (decision.reason_code == RTSFR_POL_REASON_UNSAFE_INFLIGHT_IO) ||
         (decision.reason_code == RTSFR_POL_REASON_UNSAFE_CANNOT_RECLAIM)) &&
        (g_unsafe_restarts_prevented != UINT16_MAX))
    {
        g_unsafe_restarts_prevented++;
    }
    if (rtsfr_policy_should_invoke_recovery(&decision))
    {
        context = prv_context(fault->task_id);
        context->active_event_id = fault->event_id;
        if (rtsfr_recovery_request(&decision) == RTSFR_OK)
        {
            if (decision.action == RTSFR_ACT_RESTART)
            {
                g_recovery_started_tick = xTaskGetTickCount();
            }
        }
    }
    (void)rtsfr_system_emit_snapshot();
}

static void prv_publish_health(void)
{
    rtsfr_health_sample_t health;
    rtsfr_verify_snapshot_t snapshot;
    rtsfr_verify_report_t report;
    rtsfr_status_t verify_status;
    uint8_t task_id;
    bool all_healthy = true;
    rtsfr_recovery_state_t recovery_state = rtsfr_recovery_get_state();

    (void)memset(&health, 0, sizeof(health));
    for (task_id = 1U; task_id <= configRTSFR_MAX_TASKS; task_id++)
    {
        rtsfr_task_record_t record;
        if ((rtsfr_detect_get_record(task_id, &record) != RTSFR_OK) ||
            (record.state != RTSFR_TASK_HEALTHY &&
             record.state != RTSFR_TASK_EXPECTED_WAIT))
        {
            all_healthy = false;
        }
    }
    verify_status = prv_verify_snapshot(&snapshot);
    if (verify_status == RTSFR_OK)
    {
        verify_status = rtsfr_verify_check(&snapshot, &report);
    }
    health.valid = true;
    health.scheduler_running =
        xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
    health.all_tasks_healthy = all_healthy;
    health.invariants_verified = verify_status == RTSFR_OK;
    health.resources_consistent = verify_status == RTSFR_OK;
    health.recovery_in_progress =
        (recovery_state >= RTSFR_REC_STATE_QUIESCING) &&
        (recovery_state <= RTSFR_REC_STATE_VERIFYING);
    health.safe_mode = rtsfr_safe_mode_is_active();
    health.safe_outputs_confirmed = g_safe_output_active;
    health.reset_requested = g_reset_requested;
    health.supervisor_tick = xTaskGetTickCount();
    health.recovery_started_tick = g_recovery_started_tick;
    health.recovery_budget_ticks = RTSFR_RECOVERY_BUDGET;
    health.supervisor_sequence = ++g_supervisor_sequence;
    (void)rtsfr_health_publish(&health);
}

rtsfr_status_t rtsfr_system_init(void)
{
    uint8_t task_id;
    uint16_t mutex_id;
    rtsfr_recovery_hooks_t hooks = {0};
    rtsfr_m5_bindings_t bindings = {0};

    (void)memset(g_contexts, 0, sizeof(g_contexts));
    g_ready = false;
    g_safe_output_active = false;
    g_reset_requested = false;
    g_supervisor_sequence = 0U;
    g_recovery_started_tick = 0U;
    g_have_recovery_result = false;
    g_active_system_test = 0U;
    g_force_next_create_failure = false;
    g_detect_overflow_seen = 0U;
    g_monitor_polls = 0U;
    g_monitor_last_cycles = 0U;
    g_monitor_max_cycles = 0U;
    g_successful_recoveries = 0U;
    g_unsafe_restarts_prevented = 0U;
    g_false_alarms_rolled_back = 0U;

    rtsfr_resource_init();
    (void)rtsfr_detect_init();
    rtsfr_policy_init();

    hooks.resource_snapshot = rtsfr_resource_snapshot;
    hooks.resource_cleanup = rtsfr_resource_cleanup_safe;
    hooks.prepare_delete = prv_prepare_delete;
    hooks.publish_generation = prv_publish_generation;
    hooks.create_task = prv_create_task;
    hooks.record_result = prv_record_result;
    hooks.trace = rtsfr_m5_on_recovery_trace;
    hooks.request_reset = prv_request_reset;
    hooks.apply_safe_output = prv_safe_output;
    rtsfr_recovery_init(&hooks);

    g_control_mutex = xSemaphoreCreateMutex();
    if ((g_control_mutex == NULL) ||
        (rtsfr_resource_register_mutex(g_control_mutex, &mutex_id) !=
         RTSFR_OK))
    {
        return RTSFR_ERR_NO_RESOURCE;
    }
    (void)mutex_id;

    for (task_id = 1U; task_id <= configRTSFR_MAX_TASKS; task_id++)
    {
        rtsfr_task_config_t detect_config;
        rtsfr_recovery_task_config_t recovery_config;
        rtsfr_demo_context_t *context = prv_context(task_id);

        context->task_id = task_id;
        context->generation = 1U;
        if (xTaskCreate(prv_task_entry(task_id), prv_task_name(task_id),
                        RTSFR_DEMO_STACK_WORDS, context,
                        RTSFR_DEMO_PRIORITY, &context->handle) != pdPASS)
        {
            return RTSFR_ERR_NO_RESOURCE;
        }
        /* Do not let a partially registered task run if a later gate fails. */
        vTaskSuspend(context->handle);
        detect_config.task_id = task_id;
        detect_config.task_name = prv_task_name(task_id);
        detect_config.timeout_ticks = RTSFR_PROGRESS_TIMEOUT;
        detect_config.criticality = prv_task_criticality(task_id);
        if (rtsfr_register_task(&detect_config, context->handle) != RTSFR_OK)
        {
            return RTSFR_ERR_INTERNAL;
        }
        if (rtsfr_policy_register_task(task_id, 1U,
                                       detect_config.criticality) != RTSFR_OK)
        {
            return RTSFR_ERR_INTERNAL;
        }
        (void)memset(&recovery_config, 0, sizeof(recovery_config));
        recovery_config.task_id = task_id;
        recovery_config.initial_generation = 1U;
        recovery_config.entry = prv_task_entry(task_id);
        recovery_config.argument = context;
        recovery_config.stack_depth = RTSFR_DEMO_STACK_WORDS;
        recovery_config.priority = RTSFR_DEMO_PRIORITY;
        recovery_config.name = prv_task_name(task_id);
        recovery_config.verify_timeout_ticks = RTSFR_VERIFY_TIMEOUT;
        recovery_config.reset_context = prv_reset_context;
        if (rtsfr_recovery_register_task(&recovery_config,
                                         context->handle) != RTSFR_OK)
        {
            return RTSFR_ERR_INTERNAL;
        }
    }

    bindings.validate_instance = prv_validate_instance;
    bindings.report_fault = prv_report_injected_fault;
    bindings.start_test = prv_start_test;
    bindings.snapshot = prv_verify_snapshot;
    if ((rtsfr_m5_bind(&bindings) != RTSFR_OK) ||
        (rtsfr_health_bind_supervisor(xTaskGetCurrentTaskHandle()) != RTSFR_OK))
    {
        return RTSFR_ERR_INTERNAL;
    }
    g_ready = true;
    for (task_id = 1U; task_id <= configRTSFR_MAX_TASKS; task_id++)
    {
        vTaskResume(prv_context(task_id)->handle);
    }
    return RTSFR_OK;
}

void rtsfr_system_poll(void)
{
    rtsfr_fault_event_t fault;
    rtsfr_recovery_state_t state;
    uint32_t overflow_count;
    uint32_t cycle_start = 0U;
    if (!g_ready)
    {
        return;
    }
    if (rtsfr_cycles_ready())
    {
        cycle_start = rtsfr_cycles_now();
    }
    (void)rtsfr_detect_scan(xTaskGetTickCount());
    overflow_count = rtsfr_detect_event_overflow_count();
    if (overflow_count != g_detect_overflow_seen)
    {
        g_detect_overflow_seen = overflow_count;
        (void)rtsfr_safe_mode_enter(0xF0000000UL,
                                    RTSFR_TASK_ID_NONE,
                                    0U,
                                    RTSFR_POL_REASON_EVENT_QUEUE_OVERFLOW);
        rtsfr_m5_emit(M5_SAFE_MODE, rtsfr_m5_test_active(),
                      0xF0000000UL, RTSFR_TASK_ID_NONE, 0U,
                      RTSFR_POL_REASON_EVENT_QUEUE_OVERFLOW,
                      overflow_count);
    }
    while (rtsfr_detect_pop_event(&fault))
    {
        prv_handle_fault(&fault);
    }
    state = rtsfr_recovery_get_state();
    if ((state >= RTSFR_REC_STATE_QUIESCING) &&
        (state <= RTSFR_REC_STATE_VERIFYING))
    {
        (void)rtsfr_recovery_process();
    }
    prv_poll_test();
    prv_publish_health();
    if (g_monitor_polls != UINT32_MAX)
    {
        g_monitor_polls++;
    }
    if (rtsfr_cycles_ready())
    {
        g_monitor_last_cycles =
            rtsfr_cycles_elapsed(cycle_start, rtsfr_cycles_now());
        if (g_monitor_last_cycles > g_monitor_max_cycles)
        {
            g_monitor_max_cycles = g_monitor_last_cycles;
        }
    }
}

bool rtsfr_system_is_ready(void)
{
    return g_ready;
}

rtsfr_status_t rtsfr_system_get_metrics(rtsfr_system_metrics_t *out)
{
    uint8_t task_id;
    if ((out == NULL) || !g_ready)
    {
        return RTSFR_ERR_NOT_READY;
    }
    (void)memset(out, 0, sizeof(*out));
    for (task_id = 1U; task_id <= configRTSFR_MAX_TASKS; task_id++)
    {
        rtsfr_task_record_t record;
        if (rtsfr_detect_get_record(task_id, &record) == RTSFR_OK)
        {
            out->task_progress[task_id - 1U] = record.progress_seq;
            if (record.task_handle != NULL)
            {
                out->task_stack_high_water[task_id - 1U] =
                    uxTaskGetStackHighWaterMark(record.task_handle);
            }
        }
    }
    out->monitor_polls = g_monitor_polls;
    out->monitor_last_cycles = g_monitor_last_cycles;
    out->monitor_max_cycles = g_monitor_max_cycles;
    out->event_overflows = rtsfr_detect_event_overflow_count();
    out->resource_anomalies = rtsfr_resource_get_anomaly_count();
    out->successful_recoveries = g_successful_recoveries;
    out->unsafe_restarts_prevented = g_unsafe_restarts_prevented;
    out->false_alarms_rolled_back = g_false_alarms_rolled_back;
    return RTSFR_OK;
}

rtsfr_status_t rtsfr_system_emit_snapshot(void)
{
    rtsfr_verify_snapshot_t snapshot;
    uint8_t index;
    rtsfr_status_t status;

    if (!g_ready)
    {
        return RTSFR_ERR_NOT_READY;
    }
    status = prv_verify_snapshot(&snapshot);
    if (status != RTSFR_OK)
    {
        return status;
    }

    rtsfr_m5_emit(M5_SNAPSHOT_BEGIN, rtsfr_m5_test_active(), 0U,
                  0U, 0U, snapshot.resource_count, snapshot.task_count);
    for (index = 0U; index < snapshot.task_count; index++)
    {
        rtsfr_task_record_t record;
        rtsfr_policy_task_state_t policy;
        const rtsfr_verify_task_t *task = &snapshot.tasks[index];
        uint16_t flags;

        if ((rtsfr_detect_get_record(task->task_id, &record) != RTSFR_OK) ||
            (rtsfr_policy_get_task_state(task->task_id, &policy) != RTSFR_OK))
        {
            continue;
        }
        flags = (uint16_t)record.state;
        flags |= task->active ? 0x0100U : 0U;
        flags |= task->restarting ? 0x0200U : 0U;
        flags |= task->resources_safe ? 0x0400U : 0U;
        flags |= (uint16_t)((uint16_t)policy.retry_count << 12U);
        rtsfr_m5_emit(M5_TASK_SNAPSHOT, rtsfr_m5_test_active(), 0U,
                      task->task_id, task->generation, flags,
                      record.progress_seq);
    }
    for (index = 0U; index < snapshot.resource_count; index++)
    {
        const rtsfr_verify_resource_t *resource = &snapshot.resources[index];
        rtsfr_m5_emit(M5_RESOURCE_SNAPSHOT, rtsfr_m5_test_active(),
                      resource->resource_id, resource->owner_id,
                      resource->owner_generation,
                      (uint16_t)resource->state,
                      (resource->resource_id >= 0x100U) ? 2U : 1U);
    }
    rtsfr_m5_emit(M5_SYSTEM_SNAPSHOT, rtsfr_m5_test_active(), 0U,
                  g_reset_requested ? 1U : 0U, 0U,
                  rtsfr_safe_mode_is_active() ? 1U : 0U,
                  (uint32_t)rtsfr_recovery_get_state());
    rtsfr_m5_emit(M5_SNAPSHOT_END, rtsfr_m5_test_active(), 0U,
                  0U, 0U, 0U, 0U);
    return RTSFR_OK;
}
