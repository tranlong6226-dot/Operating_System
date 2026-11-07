#ifndef RTSFR_M5_ADAPTER_H
#define RTSFR_M5_ADAPTER_H
#include "rtsfr_recovery.h"
#include "rtsfr_verify.h"
#include "rtsfr_health.h"
typedef struct {
    /* Must check generation AND current calling task identity; task context. */
    rtsfr_status_t (*validate_instance)(uint8_t task_id, uint16_t generation, bool caller_is_target);
    rtsfr_status_t (*report_fault)(uint8_t task_id, uint16_t generation, uint32_t event_id);
    /* Starts an asynchronous real target test. Missing scenario -> NOT_READY. */
    rtsfr_status_t (*start_test)(uint8_t test_id);
    rtsfr_verify_provider_fn snapshot;
} rtsfr_m5_bindings_t;
/* One-time initialization in the bootstrap task before binding modules. */
void rtsfr_m5_adapter_init(void);
rtsfr_status_t rtsfr_m5_bind(const rtsfr_m5_bindings_t *bindings);
rtsfr_status_t rtsfr_m5_validate(uint8_t task_id, uint16_t generation, bool caller_is_target);
rtsfr_status_t rtsfr_m5_report_fault(uint8_t task_id, uint16_t generation, uint32_t event_id);
rtsfr_status_t rtsfr_m5_start_test(uint8_t test_id);
/* Install these in M4 hooks.trace / hooks.record_result. No I/O/blocking. */
void rtsfr_m5_on_recovery_trace(const rtsfr_recovery_trace_t *trace);
void rtsfr_m5_on_recovery_result(const rtsfr_recovery_result_t *result);
/* M1/M2 emit these with the same correlated event ID; task context. */
void rtsfr_m5_on_fault(uint32_t event_id, uint8_t task_id, uint16_t generation, uint16_t reason);
void rtsfr_m5_on_decision(const rtsfr_decision_t *decision);
/* M1 calls at the actual first completed work of the new instance; not at a later scan. */
void rtsfr_m5_on_first_progress(uint32_t event_id, uint8_t task_id, uint16_t generation);
/* Evidence correlation is set before test start, not inferred from event names. */
void rtsfr_m5_set_test_id(uint8_t test_id);
#endif
