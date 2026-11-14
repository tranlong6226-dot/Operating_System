#include "rtsfr_detect.h"

#include <string.h>

#include "rtsfr_resource.h"

static rtsfr_task_record_t g_registry[configRTSFR_MAX_TRACKED_TASKS];
static rtsfr_fault_event_t g_events[configRTSFR_EVENT_QUEUE_SIZE];
static uint8_t g_event_head;
static uint8_t g_event_tail;
static uint8_t g_event_count;
static uint32_t g_next_event_id = 1U;
static uint32_t g_event_overflows;

__attribute__((weak)) uint32_t rtsfr_detect_event_correlation(
    uint8_t task_id,
    uint16_t generation)
{
    (void)task_id;
    (void)generation;
    return 0U;
}

static rtsfr_task_record_t *prv_record(uint8_t task_id)
{
    if ((task_id == 0U) || (task_id > configRTSFR_MAX_TRACKED_TASKS))
    {
        return NULL;
    }
    return &g_registry[task_id - 1U];
}

static bool prv_enqueue(const rtsfr_fault_event_t *event)
{
    if (g_event_count >= configRTSFR_EVENT_QUEUE_SIZE)
    {
        if (g_event_overflows != UINT32_MAX)
        {
            g_event_overflows++;
        }
        return false;
    }

    g_events[g_event_head] = *event;
    g_event_head = (uint8_t)((g_event_head + 1U) % configRTSFR_EVENT_QUEUE_SIZE);
    g_event_count++;
    return true;
}

static rtsfr_status_t prv_emit_fault(rtsfr_task_record_t *record,
                                     TickType_t tick,
                                     uint32_t event_id,
                                     rtsfr_fault_t type)
{
    rtsfr_fault_event_t event;

    if (record->fault_pending)
    {
        return RTSFR_ERR_BUSY;
    }

    event.task_id = record->task_id;
    event.generation = record->generation;
    event.event_id = (event_id != 0U) ? event_id : g_next_event_id;
    event.observed_tick = tick;
    event.fault_type = type;
    event.evidence = record->progress_seq;

    if (!prv_enqueue(&event))
    {
        return RTSFR_ERR_NO_RESOURCE;
    }

    if (event_id == 0U)
    {
        g_next_event_id++;
        if (g_next_event_id == 0U)
        {
            g_next_event_id = 1U;
        }
    }
    record->state = RTSFR_TASK_FAULTY;
    record->fault_pending = true;
    return RTSFR_OK;
}

rtsfr_status_t rtsfr_detect_init(void)
{
    taskENTER_CRITICAL();
    (void)memset(g_registry, 0, sizeof(g_registry));
    (void)memset(g_events, 0, sizeof(g_events));
    g_event_head = 0U;
    g_event_tail = 0U;
    g_event_count = 0U;
    g_next_event_id = 1U;
    g_event_overflows = 0U;
    taskEXIT_CRITICAL();
    return RTSFR_OK;
}

rtsfr_status_t rtsfr_register_task(const rtsfr_task_config_t *cfg,
                                   TaskHandle_t task_handle)
{
    rtsfr_task_record_t *record;

    if ((cfg == NULL) || (task_handle == NULL) ||
        (cfg->timeout_ticks == 0U))
    {
        return RTSFR_ERR_INVALID;
    }
    record = prv_record(cfg->task_id);
    if (record == NULL)
    {
        return RTSFR_ERR_INVALID;
    }

    taskENTER_CRITICAL();
    if (record->state != RTSFR_TASK_UNREGISTERED)
    {
        taskEXIT_CRITICAL();
        return RTSFR_ERR_BUSY;
    }
    record->task_id = cfg->task_id;
    record->generation = 1U;
    record->task_handle = task_handle;
    record->timeout_ticks = cfg->timeout_ticks;
    record->last_progress_tick = xTaskGetTickCount();
    record->expected_wait_deadline = 0U;
    record->progress_seq = 0U;
    record->state = RTSFR_TASK_HEALTHY;
    record->fault_pending = false;
    taskEXIT_CRITICAL();

    rtsfr_resource_on_new_generation(cfg->task_id, 1U);
    return RTSFR_OK;
}

rtsfr_status_t rtsfr_report_progress(uint8_t task_id)
{
    rtsfr_task_record_t *record = prv_record(task_id);
    TaskHandle_t caller = xTaskGetCurrentTaskHandle();

    if (record == NULL)
    {
        return RTSFR_ERR_INVALID;
    }

    taskENTER_CRITICAL();
    if ((record->state == RTSFR_TASK_UNREGISTERED) ||
        (record->task_handle != caller))
    {
        taskEXIT_CRITICAL();
        return RTSFR_ERR_STALE;
    }
    if ((record->state == RTSFR_TASK_QUIESCING) ||
        (record->state == RTSFR_TASK_CLEANUP) ||
        (record->state == RTSFR_TASK_RECREATE) ||
        (record->state == RTSFR_TASK_SAFE_MODE))
    {
        taskEXIT_CRITICAL();
        return RTSFR_ERR_BUSY;
    }

    record->last_progress_tick = xTaskGetTickCount();
    record->progress_seq++;
    record->expected_wait_deadline = 0U;
    record->state = RTSFR_TASK_HEALTHY;
    record->fault_pending = false;
    taskEXIT_CRITICAL();
    return RTSFR_OK;
}

rtsfr_status_t rtsfr_begin_expected_wait(uint8_t task_id, TickType_t timeout)
{
    rtsfr_task_record_t *record = prv_record(task_id);
    TickType_t now;

    if ((record == NULL) || (timeout == 0U))
    {
        return RTSFR_ERR_INVALID;
    }
    taskENTER_CRITICAL();
    if ((record->task_handle != xTaskGetCurrentTaskHandle()) ||
        (record->state != RTSFR_TASK_HEALTHY))
    {
        taskEXIT_CRITICAL();
        return RTSFR_ERR_STALE;
    }
    now = xTaskGetTickCount();
    record->state = RTSFR_TASK_EXPECTED_WAIT;
    record->last_progress_tick = now;
    record->expected_wait_deadline = now + timeout;
    taskEXIT_CRITICAL();
    return RTSFR_OK;
}

rtsfr_status_t rtsfr_end_expected_wait(uint8_t task_id)
{
    rtsfr_task_record_t *record = prv_record(task_id);

    if (record == NULL)
    {
        return RTSFR_ERR_INVALID;
    }
    taskENTER_CRITICAL();
    if ((record->task_handle != xTaskGetCurrentTaskHandle()) ||
        (record->state != RTSFR_TASK_EXPECTED_WAIT))
    {
        taskEXIT_CRITICAL();
        return RTSFR_ERR_STALE;
    }
    record->state = RTSFR_TASK_HEALTHY;
    record->last_progress_tick = xTaskGetTickCount();
    record->expected_wait_deadline = 0U;
    taskEXIT_CRITICAL();
    return RTSFR_OK;
}

rtsfr_status_t rtsfr_detect_report_fault(uint8_t task_id,
                                         uint16_t generation,
                                         uint32_t event_id,
                                         rtsfr_fault_t fault_type)
{
    rtsfr_task_record_t *record = prv_record(task_id);
    rtsfr_status_t status;

    if ((record == NULL) || (generation == 0U) ||
        (fault_type == RTSFR_FAULT_NONE))
    {
        return RTSFR_ERR_INVALID;
    }
    taskENTER_CRITICAL();
    if (record->generation != generation)
    {
        taskEXIT_CRITICAL();
        return RTSFR_ERR_STALE;
    }
    status = prv_emit_fault(record, xTaskGetTickCount(), event_id, fault_type);
    taskEXIT_CRITICAL();
    return status;
}

rtsfr_status_t rtsfr_detect_scan(TickType_t current_tick)
{
    uint8_t index;

    taskENTER_CRITICAL();
    for (index = 0U; index < configRTSFR_MAX_TRACKED_TASKS; index++)
    {
        rtsfr_task_record_t *record = &g_registry[index];
        TickType_t elapsed;

        if (record->state == RTSFR_TASK_HEALTHY)
        {
            elapsed = (TickType_t)(current_tick - record->last_progress_tick);
            if (elapsed > record->timeout_ticks)
            {
                (void)prv_emit_fault(
                    record, current_tick,
                    rtsfr_detect_event_correlation(record->task_id,
                                                   record->generation),
                    RTSFR_FAULT_NO_PROGRESS);
            }
        }
        else if (record->state == RTSFR_TASK_EXPECTED_WAIT)
        {
            elapsed = (TickType_t)(current_tick - record->last_progress_tick);
            if (elapsed >
                (TickType_t)(record->expected_wait_deadline -
                             record->last_progress_tick))
            {
                (void)prv_emit_fault(record, current_tick,
                    rtsfr_detect_event_correlation(record->task_id,
                                                   record->generation),
                    RTSFR_FAULT_WAIT_TIMEOUT);
            }
        }
    }
    taskEXIT_CRITICAL();
    return RTSFR_OK;
}

bool rtsfr_detect_pop_event(rtsfr_fault_event_t *event)
{
    if (event == NULL)
    {
        return false;
    }
    taskENTER_CRITICAL();
    if (g_event_count == 0U)
    {
        taskEXIT_CRITICAL();
        return false;
    }
    *event = g_events[g_event_tail];
    g_event_tail = (uint8_t)((g_event_tail + 1U) % configRTSFR_EVENT_QUEUE_SIZE);
    g_event_count--;
    taskEXIT_CRITICAL();
    return true;
}

void rtsfr_detect_on_new_generation(uint8_t task_id,
                                    TaskHandle_t new_handle,
                                    uint16_t new_generation)
{
    rtsfr_task_record_t *record = prv_record(task_id);
    if ((record == NULL) || (new_handle == NULL) || (new_generation == 0U))
    {
        return;
    }
    taskENTER_CRITICAL();
    if (record->state == RTSFR_TASK_UNREGISTERED)
    {
        taskEXIT_CRITICAL();
        return;
    }
    record->generation = new_generation;
    record->task_handle = new_handle;
    record->last_progress_tick = xTaskGetTickCount();
    record->progress_seq = 0U;
    record->expected_wait_deadline = 0U;
    record->state = RTSFR_TASK_HEALTHY;
    record->fault_pending = false;
    taskEXIT_CRITICAL();
    rtsfr_resource_on_new_generation(task_id, new_generation);
}

void rtsfr_detect_on_task_delete(TaskHandle_t task_handle)
{
    uint8_t index;
    if (task_handle == NULL)
    {
        return;
    }
    taskENTER_CRITICAL();
    for (index = 0U; index < configRTSFR_MAX_TRACKED_TASKS; index++)
    {
        if (g_registry[index].task_handle == task_handle)
        {
            g_registry[index].task_handle = NULL;
            g_registry[index].state = RTSFR_TASK_QUIESCING;
            break;
        }
    }
    taskEXIT_CRITICAL();
}

rtsfr_status_t rtsfr_detect_get_record(uint8_t task_id,
                                       rtsfr_task_record_t *out_record)
{
    rtsfr_task_record_t *record = prv_record(task_id);
    if ((record == NULL) || (out_record == NULL))
    {
        return RTSFR_ERR_INVALID;
    }
    taskENTER_CRITICAL();
    if (record->state == RTSFR_TASK_UNREGISTERED)
    {
        taskEXIT_CRITICAL();
        return RTSFR_ERR_NOT_FOUND;
    }
    *out_record = *record;
    taskEXIT_CRITICAL();
    return RTSFR_OK;
}

uint8_t rtsfr_detect_task_id_from_handle(const void *task_handle)
{
    uint8_t index;
    uint8_t result = RTSFR_TASK_ID_NONE;
    if (task_handle == NULL)
    {
        return result;
    }
    taskENTER_CRITICAL();
    for (index = 0U; index < configRTSFR_MAX_TRACKED_TASKS; index++)
    {
        if ((g_registry[index].state != RTSFR_TASK_UNREGISTERED) &&
            ((const void *)g_registry[index].task_handle == task_handle))
        {
            result = g_registry[index].task_id;
            break;
        }
    }
    taskEXIT_CRITICAL();
    return result;
}

uint32_t rtsfr_detect_event_overflow_count(void)
{
    uint32_t result;
    taskENTER_CRITICAL();
    result = g_event_overflows;
    taskEXIT_CRITICAL();
    return result;
}

#if RTSFR_ENABLE_FAULT_INJECTION
rtsfr_status_t rtsfr_detect_test_saturate_event_queue(void)
{
    rtsfr_fault_event_t event;
    uint32_t overflow_before;
    uint8_t index;

    (void)memset(&event, 0, sizeof(event));
    event.task_id = 1U;
    /* Generation zero is deliberately stale, so queued test events cannot
     * start a real recovery after the overflow path has been observed. */
    event.generation = 0U;
    event.fault_type = RTSFR_FAULT_SELF_REPORTED;

    taskENTER_CRITICAL();
    overflow_before = g_event_overflows;
    for (index = 0U; index <= configRTSFR_EVENT_QUEUE_SIZE; index++)
    {
        event.event_id = 0xF0000000UL + index;
        (void)prv_enqueue(&event);
    }
    taskEXIT_CRITICAL();

    return (g_event_overflows > overflow_before) ? RTSFR_OK
                                                  : RTSFR_ERR_INTERNAL;
}
#endif
