/**
 * @file rtsfr_detect.c
 * @brief Implementation of Module 1: Fault Detection & Kernel Integration
 * @details Owned by Việt Anh (Module 1 Lead)
 */

#include "rtsfr_detect.h"
#include <string.h>

/* FreeRTOS or Host Test compatibility */
#if defined(INC_FREERTOS_H) || defined(FREERTOS_H)
    #include "FreeRTOS.h"
    #include "task.h"
    #define RTSFR_ENTER_CRITICAL() taskENTER_CRITICAL()
    #define RTSFR_EXIT_CRITICAL()  taskEXIT_CRITICAL()
    #define RTSFR_GET_TICK()       xTaskGetTickCount()
#else
    /* Mock stubs for Host Unit Testing */
    extern TickType_t g_mock_tick;
    #define RTSFR_ENTER_CRITICAL() do {} while(0)
    #define RTSFR_EXIT_CRITICAL()  do {} while(0)
    #define RTSFR_GET_TICK()       (g_mock_tick)
#endif

/* Global Monotonically Increasing Event Counter */
static uint32_t s_next_event_id = 1U;

/* Static task registry (Zero runtime dynamic allocation) */
static rtsfr_task_record_t s_registry[configRTSFR_MAX_TRACKED_TASKS];

/* Static circular ring buffer for fault events */
static rtsfr_fault_event_t s_event_queue[configRTSFR_EVENT_QUEUE_SIZE];
static uint8_t s_queue_head = 0U;
static uint8_t s_queue_tail = 0U;
static uint8_t s_queue_count = 0U;

/* Helper to enqueue fault event (Must be called within critical section or atomic context) */
static bool prv_enqueue_event(const rtsfr_fault_event_t *event)
{
    if (s_queue_count >= configRTSFR_EVENT_QUEUE_SIZE) {
        /* Queue full: overwrite oldest or drop (Spec: fail-safe policy) */
        return false;
    }

    s_event_queue[s_queue_head] = *event;
    s_queue_head = (uint8_t)((s_queue_head + 1U) % configRTSFR_EVENT_QUEUE_SIZE);
    s_queue_count++;
    return true;
}

rtsfr_status_t rtsfr_detect_init(void)
{
    RTSFR_ENTER_CRITICAL();
    memset(s_registry, 0, sizeof(s_registry));
    memset(s_event_queue, 0, sizeof(s_event_queue));
    s_queue_head = 0U;
    s_queue_tail = 0U;
    s_queue_count = 0U;
    s_next_event_id = 1U;
    RTSFR_EXIT_CRITICAL();
    return RTSFR_OK;
}

rtsfr_status_t rtsfr_register_task(const rtsfr_task_config_t *cfg, TaskHandle_t task_handle)
{
    if (cfg == NULL || cfg->task_id >= configRTSFR_MAX_TRACKED_TASKS) {
        return RTSFR_ERR_INVALID;
    }

    RTSFR_ENTER_CRITICAL();
    rtsfr_task_record_t *rec = &s_registry[cfg->task_id];

    rec->task_id = cfg->task_id;
    rec->generation = 1U; /* Initial generation */
    rec->task_handle = task_handle;
    rec->timeout_ticks = cfg->timeout_ticks;
    rec->last_progress_tick = RTSFR_GET_TICK();
    rec->expected_wait_deadline = 0U;
    rec->progress_seq = 0U;
    rec->state = RTSFR_TASK_STATE_HEALTHY;
    rec->fault_pending = false;

    RTSFR_EXIT_CRITICAL();
    return RTSFR_OK;
}

rtsfr_status_t rtsfr_report_progress(uint8_t task_id)
{
    if (task_id >= configRTSFR_MAX_TRACKED_TASKS) {
        return RTSFR_ERR_INVALID;
    }

    RTSFR_ENTER_CRITICAL();
    rtsfr_task_record_t *rec = &s_registry[task_id];

    if (rec->state == RTSFR_TASK_STATE_UNREGISTERED) {
        RTSFR_EXIT_CRITICAL();
        return RTSFR_ERR_NOT_FOUND;
    }

    rec->last_progress_tick = RTSFR_GET_TICK();
    rec->progress_seq++;

    /* If task was in expected wait, returning progress transitions back to HEALTHY */
    if (rec->state == RTSFR_TASK_STATE_EXPECTED_WAIT) {
        rec->state = RTSFR_TASK_STATE_HEALTHY;
    }

    RTSFR_EXIT_CRITICAL();
    return RTSFR_OK;
}

rtsfr_status_t rtsfr_begin_expected_wait(uint8_t task_id, TickType_t timeout)
{
    if (task_id >= configRTSFR_MAX_TRACKED_TASKS) {
        return RTSFR_ERR_INVALID;
    }

    RTSFR_ENTER_CRITICAL();
    rtsfr_task_record_t *rec = &s_registry[task_id];

    if (rec->state == RTSFR_TASK_STATE_UNREGISTERED) {
        RTSFR_EXIT_CRITICAL();
        return RTSFR_ERR_NOT_FOUND;
    }

    TickType_t now = RTSFR_GET_TICK();
    rec->state = RTSFR_TASK_STATE_EXPECTED_WAIT;
    rec->expected_wait_deadline = now + timeout;
    rec->last_progress_tick = now;

    RTSFR_EXIT_CRITICAL();
    return RTSFR_OK;
}

rtsfr_status_t rtsfr_end_expected_wait(uint8_t task_id)
{
    if (task_id >= configRTSFR_MAX_TRACKED_TASKS) {
        return RTSFR_ERR_INVALID;
    }

    RTSFR_ENTER_CRITICAL();
    rtsfr_task_record_t *rec = &s_registry[task_id];

    if (rec->state == RTSFR_TASK_STATE_UNREGISTERED) {
        RTSFR_EXIT_CRITICAL();
        return RTSFR_ERR_NOT_FOUND;
    }

    rec->last_progress_tick = RTSFR_GET_TICK();
    rec->state = RTSFR_TASK_STATE_HEALTHY;

    RTSFR_EXIT_CRITICAL();
    return RTSFR_OK;
}

rtsfr_status_t rtsfr_detect_scan(TickType_t current_tick)
{
    RTSFR_ENTER_CRITICAL();

    for (uint8_t i = 0U; i < configRTSFR_MAX_TRACKED_TASKS; i++) {
        rtsfr_task_record_t *rec = &s_registry[i];

        if (rec->state == RTSFR_TASK_STATE_HEALTHY) {
            /* Tick wrap-around safe subtraction */
            TickType_t elapsed = current_tick - rec->last_progress_tick;

            if ((elapsed > rec->timeout_ticks) && (!rec->fault_pending)) {
                /* Task stopped reporting progress (F01) */
                rec->state = RTSFR_TASK_STATE_FAULTY;
                rec->fault_pending = true;

                rtsfr_fault_event_t event;
                event.event_id = s_next_event_id++;
                event.task_id = rec->task_id;
                event.generation = rec->generation;
                event.fault_type = RTSFR_FAULT_NO_PROGRESS;
                event.detected_tick = current_tick;
                event.last_progress_seq = rec->progress_seq;

                (void)prv_enqueue_event(&event);
            }
        }
        else if (rec->state == RTSFR_TASK_STATE_EXPECTED_WAIT) {
            /* Check if expected wait deadline has been exceeded */
            TickType_t elapsed = current_tick - rec->last_progress_tick;
            TickType_t allowed = rec->expected_wait_deadline - rec->last_progress_tick;

            if ((elapsed > allowed) && (!rec->fault_pending)) {
                /* Task exceeded declared wait duration (F04) */
                rec->state = RTSFR_TASK_STATE_FAULTY;
                rec->fault_pending = true;

                rtsfr_fault_event_t event;
                event.event_id = s_next_event_id++;
                event.task_id = rec->task_id;
                event.generation = rec->generation;
                event.fault_type = RTSFR_FAULT_WAIT_TIMEOUT;
                event.detected_tick = current_tick;
                event.last_progress_seq = rec->progress_seq;

                (void)prv_enqueue_event(&event);
            }
        }
    }

    RTSFR_EXIT_CRITICAL();
    return RTSFR_OK;
}

bool rtsfr_detect_pop_event(rtsfr_fault_event_t *event)
{
    if (event == NULL) {
        return false;
    }

    RTSFR_ENTER_CRITICAL();
    if (s_queue_count == 0U) {
        RTSFR_EXIT_CRITICAL();
        return false;
    }

    *event = s_event_queue[s_queue_tail];
    s_queue_tail = (uint8_t)((s_queue_tail + 1U) % configRTSFR_EVENT_QUEUE_SIZE);
    s_queue_count--;

    RTSFR_EXIT_CRITICAL();
    return true;
}

void rtsfr_detect_on_new_generation(uint8_t task_id, TaskHandle_t new_handle, uint16_t new_generation)
{
    if (task_id >= configRTSFR_MAX_TRACKED_TASKS) {
        return;
    }

    RTSFR_ENTER_CRITICAL();
    rtsfr_task_record_t *rec = &s_registry[task_id];

    rec->generation = new_generation;
    rec->task_handle = new_handle;
    rec->last_progress_tick = RTSFR_GET_TICK();
    rec->progress_seq = 0U;
    rec->state = RTSFR_TASK_STATE_HEALTHY;
    rec->fault_pending = false;

    /* Invalidate any remaining queued events from old generations for this task */
    uint8_t valid_count = 0U;
    rtsfr_fault_event_t temp_queue[configRTSFR_EVENT_QUEUE_SIZE];

    for (uint8_t i = 0U; i < s_queue_count; i++) {
        uint8_t idx = (uint8_t)((s_queue_tail + i) % configRTSFR_EVENT_QUEUE_SIZE);
        rtsfr_fault_event_t *e = &s_event_queue[idx];

        /* Keep event if it belongs to another task or matches current new generation */
        if (e->task_id != task_id || e->generation == new_generation) {
            temp_queue[valid_count++] = *e;
        }
    }

    /* Reconstruct queue */
    for (uint8_t i = 0U; i < valid_count; i++) {
        s_event_queue[i] = temp_queue[i];
    }
    s_queue_tail = 0U;
    s_queue_head = valid_count;
    s_queue_count = valid_count;

    RTSFR_EXIT_CRITICAL();
}

void rtsfr_detect_on_task_delete(TaskHandle_t task_handle)
{
    if (task_handle == NULL) {
        return;
    }

    RTSFR_ENTER_CRITICAL();
    for (uint8_t i = 0U; i < configRTSFR_MAX_TRACKED_TASKS; i++) {
        if (s_registry[i].task_handle == task_handle) {
            s_registry[i].task_handle = NULL;
            s_registry[i].state = RTSFR_TASK_STATE_QUIESCED;
            break;
        }
    }
    RTSFR_EXIT_CRITICAL();
}

rtsfr_status_t rtsfr_detect_get_record(uint8_t task_id, rtsfr_task_record_t *out_record)
{
    if (task_id >= configRTSFR_MAX_TRACKED_TASKS || out_record == NULL) {
        return RTSFR_ERR_INVALID;
    }

    RTSFR_ENTER_CRITICAL();
    if (s_registry[task_id].state == RTSFR_TASK_STATE_UNREGISTERED) {
        RTSFR_EXIT_CRITICAL();
        return RTSFR_ERR_NOT_FOUND;
    }

    *out_record = s_registry[task_id];
    RTSFR_EXIT_CRITICAL();
    return RTSFR_OK;
}
