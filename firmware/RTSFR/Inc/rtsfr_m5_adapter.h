#ifndef RTSFR_M5_ADAPTER_H
#define RTSFR_M5_ADAPTER_H

#include "rtsfr_health.h"
#include "rtsfr_recovery.h"
#include "rtsfr_verify.h"

typedef struct
{
    rtsfr_status_t (*validate_instance)(uint8_t task_id,
                                        uint16_t generation,
                                        bool caller_is_target);
    rtsfr_status_t (*report_fault)(uint8_t task_id,
                                   uint16_t generation,
                                   uint32_t event_id);
    rtsfr_status_t (*start_test)(uint8_t test_id);
    rtsfr_verify_provider_fn snapshot;
} rtsfr_m5_bindings_t;

void rtsfr_m5_adapter_init(void);
rtsfr_status_t rtsfr_m5_bind(const rtsfr_m5_bindings_t *bindings);
rtsfr_status_t rtsfr_m5_validate(uint8_t task_id,
                                 uint16_t generation,
                                 bool caller_is_target);
rtsfr_status_t rtsfr_m5_report_fault(uint8_t task_id,
                                     uint16_t generation,
                                     uint32_t event_id);
rtsfr_status_t rtsfr_m5_start_test(uint8_t test_id);
void rtsfr_m5_on_recovery_trace(const rtsfr_recovery_trace_t *trace);
void rtsfr_m5_on_recovery_result(const rtsfr_recovery_result_t *result);
void rtsfr_m5_on_fault(uint32_t event_id,
                       uint8_t task_id,
                       uint16_t generation,
                       uint16_t reason);
void rtsfr_m5_on_decision(const rtsfr_decision_t *decision);
void rtsfr_m5_on_first_progress(uint32_t event_id,
                                uint8_t task_id,
                                uint16_t generation);
void rtsfr_m5_set_test_id(uint8_t test_id);

#endif /* RTSFR_M5_ADAPTER_H */
