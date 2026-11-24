#ifndef RTSFR_POLICY_H
#define RTSFR_POLICY_H

#include "rtsfr_recovery.h"
#include "rtsfr_types.h"

#ifndef configRTSFR_RETRY_WINDOW_TICKS
#define configRTSFR_RETRY_WINDOW_TICKS \
    pdMS_TO_TICKS(configRTSFR_RETRY_WINDOW_MS)
#endif

typedef struct
{
    bool registered;
    uint8_t task_id;
    uint16_t active_generation;
    rtsfr_criticality_t criticality;
    rtsfr_task_state_t state;
    uint8_t retry_count;
    uint8_t max_restarts;
    TickType_t retry_window_ticks;
    TickType_t window_start_tick;
    uint32_t last_event_id;
    bool has_last_event;
    uint32_t event_history[configRTSFR_EVENT_HISTORY_SIZE];
    uint8_t event_history_head;
    uint8_t event_history_count;
    rtsfr_severity_t last_severity;
    uint16_t last_reason_code;
} rtsfr_policy_task_state_t;

void rtsfr_policy_init(void);
rtsfr_status_t rtsfr_policy_register_task(uint8_t task_id,
                                          uint16_t initial_generation,
                                          rtsfr_criticality_t criticality);
rtsfr_status_t rtsfr_policy_set_retry_policy(uint8_t task_id,
                                             uint8_t max_restarts,
                                             TickType_t retry_window_ticks);
rtsfr_status_t rtsfr_policy_reset_retry_window(uint8_t task_id);
rtsfr_status_t rtsfr_policy_set_task_state(uint8_t task_id,
                                           uint16_t generation,
                                           rtsfr_task_state_t new_state);
rtsfr_decision_t rtsfr_policy_evaluate(
    const rtsfr_fault_event_t *fault,
    const rtsfr_resource_summary_t *resource);
bool rtsfr_policy_should_invoke_recovery(const rtsfr_decision_t *decision);
rtsfr_severity_t rtsfr_policy_classify_severity(
    const rtsfr_fault_event_t *fault,
    const rtsfr_resource_summary_t *resource,
    bool retry_exhausted);
void rtsfr_policy_record_result(uint8_t task_id,
                                rtsfr_recovery_outcome_t result,
                                TickType_t tick);
void rtsfr_policy_on_recovery_result(const rtsfr_recovery_result_t *result);
rtsfr_status_t rtsfr_policy_get_task_state(uint8_t task_id,
                                           rtsfr_policy_task_state_t *out);

#endif /* RTSFR_POLICY_H */
