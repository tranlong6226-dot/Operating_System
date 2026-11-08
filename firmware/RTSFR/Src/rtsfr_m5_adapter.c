#include "rtsfr_m5_adapter.h"
#include "rtsfr_trace.h"
#include <string.h>
static rtsfr_m5_bindings_t bindings;
static bool bound;
static uint8_t current_test;
void rtsfr_m5_adapter_init(void)
{
    (void)memset(&bindings, 0, sizeof(bindings)); bound = false; current_test = 0U;
    rtsfr_verify_bind(NULL); rtsfr_health_init();
}
rtsfr_status_t rtsfr_m5_bind(const rtsfr_m5_bindings_t *b)
{
    if (b == NULL) return RTSFR_ERR_INVALID;
    if (bound) return RTSFR_ERR_BUSY;
    bindings = *b; bound = true; rtsfr_verify_bind(b->snapshot); return RTSFR_OK;
}
rtsfr_status_t rtsfr_m5_validate(uint8_t task_id, uint16_t generation, bool caller)
{
    if ((task_id == 0U) || (task_id > RTSFR_VERIFY_MAX_TASKS) || (generation == 0U))
        return RTSFR_ERR_INVALID;
    return bindings.validate_instance ? bindings.validate_instance(task_id, generation, caller) : RTSFR_ERR_NOT_READY;
}
rtsfr_status_t rtsfr_m5_report_fault(uint8_t task_id, uint16_t generation, uint32_t event_id)
{
    return bindings.report_fault ? bindings.report_fault(task_id, generation, event_id) : RTSFR_ERR_NOT_READY;
}
rtsfr_status_t rtsfr_m5_start_test(uint8_t test_id)
{
    return bindings.start_test ? bindings.start_test(test_id) : RTSFR_ERR_NOT_READY;
}
void rtsfr_m5_set_test_id(uint8_t test_id) { current_test = test_id; }
void rtsfr_m5_on_fault(uint32_t event_id, uint8_t task_id, uint16_t generation, uint16_t reason)
{
    rtsfr_m5_emit(M5_FAULT_DETECTED, current_test, event_id, task_id, generation, reason, 0U);
}
void rtsfr_m5_on_decision(const rtsfr_decision_t *d)
{
    if (d != NULL) rtsfr_m5_emit(M5_POLICY, current_test, d->event_id, d->task_id,
                                 d->generation, d->reason_code, (uint32_t)d->action);
}
void rtsfr_m5_on_first_progress(uint32_t event_id, uint8_t task_id, uint16_t generation)
{
    rtsfr_m5_emit(M5_FIRST_PROGRESS, current_test, event_id, task_id, generation, 0U, 0U);
}
void rtsfr_m5_on_recovery_trace(const rtsfr_recovery_trace_t *t)
{
    rtsfr_trace_event_t event;
    if (t == NULL) return;
    (void)memset(&event, 0, sizeof(event));
    event.tick = t->tick; event.event_id = t->event_id; event.task_id = t->task_id;
    event.generation = t->generation; event.reason = t->reason_code;
    event.test_id = current_test; event.value = (uint32_t)t->state;
    /* M4 observes first progress later in its process loop. Preserve this as a
       step, not as the timestamp of the actual completed work reported by M1. */
    event.event = t->event == RTSFR_TRACE_SAFE_MODE ? M5_SAFE_MODE : M5_RECOVERY_STEP;
    rtsfr_trace_event(&event);
}
void rtsfr_m5_on_recovery_result(const rtsfr_recovery_result_t *r)
{
    if (r != NULL) rtsfr_m5_emit(r->outcome == RTSFR_RECOVERY_OK ? M5_RECOVERY_OK : M5_RECOVERY_FAILED,
        current_test, r->event_id, r->task_id, r->new_generation ? r->new_generation : r->old_generation,
        r->reason_code, (uint32_t)(TickType_t)(r->finished_tick - r->started_tick));
}
