#include "rtsfr_m5_adapter.h"

#include <string.h>

#include "rtsfr_trace.h"

static rtsfr_m5_bindings_t g_bindings;
static bool g_bound;
static uint8_t g_current_test;

void rtsfr_m5_adapter_init(void)
{
    (void)memset(&g_bindings, 0, sizeof(g_bindings));
    g_bound = false;
    g_current_test = 0U;
    rtsfr_verify_bind(NULL);
    rtsfr_health_init();
}

rtsfr_status_t rtsfr_m5_bind(const rtsfr_m5_bindings_t *bindings)
{
    if (bindings == NULL)
    {
        return RTSFR_ERR_INVALID;
    }
    if (g_bound)
    {
        return RTSFR_ERR_BUSY;
    }
    g_bindings = *bindings;
    g_bound = true;
    rtsfr_verify_bind(bindings->snapshot);
    return RTSFR_OK;
}

rtsfr_status_t rtsfr_m5_validate(uint8_t task_id,
                                 uint16_t generation,
                                 bool caller_is_target)
{
    if ((task_id == 0U) || (task_id > RTSFR_VERIFY_MAX_TASKS) ||
        (generation == 0U))
    {
        return RTSFR_ERR_INVALID;
    }
    return (g_bindings.validate_instance != NULL)
               ? g_bindings.validate_instance(task_id, generation,
                                              caller_is_target)
               : RTSFR_ERR_NOT_READY;
}

rtsfr_status_t rtsfr_m5_report_fault(uint8_t task_id,
                                     uint16_t generation,
                                     uint32_t event_id)
{
    return (g_bindings.report_fault != NULL)
               ? g_bindings.report_fault(task_id, generation, event_id)
               : RTSFR_ERR_NOT_READY;
}

rtsfr_status_t rtsfr_m5_start_test(uint8_t test_id)
{
    return (g_bindings.start_test != NULL)
               ? g_bindings.start_test(test_id)
               : RTSFR_ERR_NOT_READY;
}

void rtsfr_m5_set_test_id(uint8_t test_id)
{
    g_current_test = test_id;
}

void rtsfr_m5_on_fault(uint32_t event_id,
                       uint8_t task_id,
                       uint16_t generation,
                       uint16_t reason)
{
    rtsfr_m5_emit(M5_FAULT_DETECTED, g_current_test, event_id, task_id,
                  generation, reason, 0U);
}

void rtsfr_m5_on_decision(const rtsfr_decision_t *decision)
{
    if (decision != NULL)
    {
        rtsfr_m5_emit(M5_POLICY, g_current_test, decision->event_id,
                      decision->task_id, decision->generation,
                      decision->reason_code, (uint32_t)decision->action);
    }
}

void rtsfr_m5_on_first_progress(uint32_t event_id,
                                uint8_t task_id,
                                uint16_t generation)
{
    rtsfr_m5_emit(M5_FIRST_PROGRESS, g_current_test, event_id, task_id,
                  generation, 0U, 0U);
}

void rtsfr_m5_on_recovery_trace(const rtsfr_recovery_trace_t *trace)
{
    rtsfr_trace_event_t event;
    if (trace == NULL)
    {
        return;
    }
    (void)memset(&event, 0, sizeof(event));
    event.tick = trace->tick;
    event.event_id = trace->event_id;
    event.task_id = trace->task_id;
    event.generation = trace->generation;
    event.reason = trace->reason_code;
    event.test_id = g_current_test;
    event.value = (uint32_t)trace->state;
    event.event = (trace->event == RTSFR_TRACE_SAFE_MODE)
                      ? M5_SAFE_MODE
                      : M5_RECOVERY_STEP;
    rtsfr_trace_event(&event);
}

void rtsfr_m5_on_recovery_result(const rtsfr_recovery_result_t *result)
{
    if (result != NULL)
    {
        rtsfr_m5_emit((result->outcome == RTSFR_RECOVERY_OK)
                          ? M5_RECOVERY_OK
                          : M5_RECOVERY_FAILED,
                      g_current_test, result->event_id, result->task_id,
                      (result->new_generation != 0U)
                          ? result->new_generation
                          : result->old_generation,
                      result->reason_code,
                      (uint32_t)(TickType_t)(result->finished_tick -
                                             result->started_tick));
    }
}
