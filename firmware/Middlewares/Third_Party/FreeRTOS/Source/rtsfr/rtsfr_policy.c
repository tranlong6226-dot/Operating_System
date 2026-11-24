#include "rtsfr_policy.h"

#include <limits.h>
#include <string.h>

static rtsfr_policy_task_state_t g_policy[configRTSFR_MAX_TASKS];

static rtsfr_policy_task_state_t *prv_entry(uint8_t task_id)
{
    if ((task_id == 0U) || (task_id > configRTSFR_MAX_TASKS))
    {
        return NULL;
    }
    return &g_policy[task_id - 1U];
}

static rtsfr_decision_t prv_decision(const rtsfr_fault_event_t *fault,
                                     rtsfr_action_t action,
                                     rtsfr_policy_reason_t reason)
{
    rtsfr_decision_t decision = {0};
    if (fault != NULL)
    {
        decision.event_id = fault->event_id;
        decision.task_id = fault->task_id;
        decision.generation = fault->generation;
    }
    decision.action = action;
    decision.reason_code = (uint16_t)reason;
    return decision;
}

static bool prv_fatal(rtsfr_fault_t fault)
{
    return (fault == RTSFR_FAULT_STACK_OVERFLOW) ||
           (fault == RTSFR_FAULT_HARD_FAULT) ||
           (fault == RTSFR_FAULT_CORRUPTION);
}

static bool prv_recovery_in_progress(rtsfr_task_state_t state)
{
    return (state == RTSFR_TASK_QUIESCING) ||
           (state == RTSFR_TASK_CLEANUP) ||
           (state == RTSFR_TASK_RECREATE) ||
           (state == RTSFR_TASK_VERIFYING);
}

static bool prv_event_seen(const rtsfr_policy_task_state_t *entry,
                           uint32_t event_id)
{
    uint8_t index;
    for (index = 0U; index < entry->event_history_count; index++)
    {
        if (entry->event_history[index] == event_id)
        {
            return true;
        }
    }
    return false;
}

static void prv_remember_event(rtsfr_policy_task_state_t *entry,
                               uint32_t event_id)
{
    entry->event_history[entry->event_history_head] = event_id;
    entry->event_history_head =
        (uint8_t)((entry->event_history_head + 1U) %
                  configRTSFR_EVENT_HISTORY_SIZE);
    if (entry->event_history_count < configRTSFR_EVENT_HISTORY_SIZE)
    {
        entry->event_history_count++;
    }
    entry->has_last_event = true;
    entry->last_event_id = event_id;
}

void rtsfr_policy_init(void)
{
    uint8_t index;
    (void)memset(g_policy, 0, sizeof(g_policy));
    for (index = 0U; index < configRTSFR_MAX_TASKS; index++)
    {
        g_policy[index].task_id = (uint8_t)(index + 1U);
        g_policy[index].active_generation = 1U;
        g_policy[index].criticality = RTSFR_CRIT_MEDIUM;
        g_policy[index].state = RTSFR_TASK_UNREGISTERED;
        g_policy[index].max_restarts = configRTSFR_MAX_RESTARTS;
        g_policy[index].retry_window_ticks =
            configRTSFR_RETRY_WINDOW_TICKS;
    }
}

rtsfr_status_t rtsfr_policy_register_task(uint8_t task_id,
                                          uint16_t initial_generation,
                                          rtsfr_criticality_t criticality)
{
    rtsfr_policy_task_state_t *entry = prv_entry(task_id);
    if ((entry == NULL) || (initial_generation == 0U) ||
        (criticality > RTSFR_CRIT_HIGH))
    {
        return RTSFR_ERR_INVALID;
    }
    entry->registered = true;
    entry->active_generation = initial_generation;
    entry->criticality = criticality;
    entry->state = RTSFR_TASK_HEALTHY;
    entry->retry_count = 0U;
    entry->window_start_tick = 0U;
    entry->last_event_id = 0U;
    entry->has_last_event = false;
    (void)memset(entry->event_history, 0, sizeof(entry->event_history));
    entry->event_history_head = 0U;
    entry->event_history_count = 0U;
    entry->last_severity = RTSFR_SEV_NONE;
    entry->last_reason_code = RTSFR_POL_REASON_NONE;
    return RTSFR_OK;
}

rtsfr_status_t rtsfr_policy_set_retry_policy(uint8_t task_id,
                                             uint8_t max_restarts,
                                             TickType_t retry_window_ticks)
{
    rtsfr_policy_task_state_t *entry = prv_entry(task_id);
    if ((entry == NULL) || (max_restarts == 0U) ||
        (retry_window_ticks == 0U))
    {
        return RTSFR_ERR_INVALID;
    }
    entry->max_restarts = max_restarts;
    entry->retry_window_ticks = retry_window_ticks;
    return RTSFR_OK;
}

rtsfr_status_t rtsfr_policy_reset_retry_window(uint8_t task_id)
{
    rtsfr_policy_task_state_t *entry = prv_entry(task_id);
    if ((entry == NULL) || !entry->registered ||
        prv_recovery_in_progress(entry->state))
    {
        return RTSFR_ERR_BUSY;
    }
    entry->retry_count = 0U;
    entry->window_start_tick = 0U;
    return RTSFR_OK;
}

rtsfr_status_t rtsfr_policy_set_task_state(uint8_t task_id,
                                           uint16_t generation,
                                           rtsfr_task_state_t new_state)
{
    rtsfr_policy_task_state_t *entry = prv_entry(task_id);
    if ((entry == NULL) || !entry->registered)
    {
        return RTSFR_ERR_INVALID;
    }
    if (entry->active_generation != generation)
    {
        return RTSFR_ERR_STALE;
    }
    entry->state = new_state;
    return RTSFR_OK;
}

rtsfr_severity_t rtsfr_policy_classify_severity(
    const rtsfr_fault_event_t *fault,
    const rtsfr_resource_summary_t *resource,
    bool retry_exhausted)
{
    if (fault == NULL)
    {
        return RTSFR_SEV_NONE;
    }
    if (prv_fatal(fault->fault_type) ||
        (fault->fault_type == RTSFR_FAULT_KERNEL_STALL))
    {
        return RTSFR_SEV_FATAL;
    }
    if ((fault->fault_type == RTSFR_FAULT_NONE) ||
        (fault->fault_type == RTSFR_FAULT_EXPECTED_WAIT))
    {
        return RTSFR_SEV_NONE;
    }
    if (retry_exhausted)
    {
        return RTSFR_SEV_PERSISTENT;
    }
    if ((resource == NULL) ||
        (resource->generation != fault->generation) ||
        (resource->mutex_held_count > 0U) ||
        (resource->unknown_resource_count > 0U) ||
        resource->has_inflight_io || !resource->can_reclaim_all)
    {
        return RTSFR_SEV_HAZARDOUS_RES;
    }
    return (resource->managed_buffers_owned > 0U)
               ? RTSFR_SEV_RECOVERABLE_RES
               : RTSFR_SEV_TRANSIENT;
}

rtsfr_decision_t rtsfr_policy_evaluate(
    const rtsfr_fault_event_t *fault,
    const rtsfr_resource_summary_t *resource)
{
    rtsfr_policy_task_state_t *entry;
    rtsfr_decision_t decision;
    TickType_t elapsed;

    if (fault == NULL)
    {
        return prv_decision(NULL, RTSFR_ACT_IGNORE,
                            RTSFR_POL_REASON_INVALID_INPUT);
    }
    entry = prv_entry(fault->task_id);
    if ((entry == NULL) || !entry->registered ||
        (fault->event_id == 0U) || (resource == NULL) ||
        (resource->task_id != fault->task_id))
    {
        return prv_decision(fault, RTSFR_ACT_IGNORE,
                            RTSFR_POL_REASON_INVALID_INPUT);
    }
    if (fault->generation != entry->active_generation)
    {
        return prv_decision(fault, RTSFR_ACT_IGNORE,
                            RTSFR_POL_REASON_STALE_GENERATION);
    }
    if (prv_event_seen(entry, fault->event_id))
    {
        return prv_decision(fault, RTSFR_ACT_IGNORE,
                            RTSFR_POL_REASON_DUPLICATE_EVENT);
    }

    prv_remember_event(entry, fault->event_id);

    if (prv_fatal(fault->fault_type) ||
        (fault->fault_type == RTSFR_FAULT_KERNEL_STALL))
    {
        entry->state = RTSFR_TASK_SAFE_MODE;
        entry->last_severity = RTSFR_SEV_FATAL;
        entry->last_reason_code = (fault->fault_type == RTSFR_FAULT_KERNEL_STALL)
                                      ? RTSFR_POL_REASON_KERNEL_STALL
                                      : RTSFR_POL_REASON_UNTRUSTED_STATE;
        return prv_decision(fault, RTSFR_ACT_REQUEST_RESET,
                            (rtsfr_policy_reason_t)entry->last_reason_code);
    }
    if ((fault->fault_type == RTSFR_FAULT_NONE) ||
        (fault->fault_type == RTSFR_FAULT_EXPECTED_WAIT))
    {
        return prv_decision(fault, RTSFR_ACT_IGNORE,
                            RTSFR_POL_REASON_LEGITIMATE_WAIT);
    }
    if (entry->state == RTSFR_TASK_SAFE_MODE)
    {
        return prv_decision(fault, RTSFR_ACT_IGNORE,
                            RTSFR_POL_REASON_ALREADY_IN_SAFE_MODE);
    }
    if (prv_recovery_in_progress(entry->state))
    {
        return prv_decision(fault, RTSFR_ACT_DEFER,
                            RTSFR_POL_REASON_RECOVERY_IN_PROGRESS);
    }

    if (entry->retry_count > 0U)
    {
        elapsed = (TickType_t)(fault->observed_tick -
                               entry->window_start_tick);
        if (elapsed >= entry->retry_window_ticks)
        {
            entry->retry_count = 0U;
            entry->window_start_tick = fault->observed_tick;
        }
    }
    if ((entry->retry_count >= entry->max_restarts) ||
        (entry->active_generation == UINT16_MAX))
    {
        entry->state = RTSFR_TASK_SAFE_MODE;
        entry->last_severity = RTSFR_SEV_PERSISTENT;
        decision = prv_decision(
            fault,
            (entry->criticality == RTSFR_CRIT_HIGH)
                ? RTSFR_ACT_REQUEST_RESET
                : RTSFR_ACT_SAFE_MODE,
            (entry->criticality == RTSFR_CRIT_HIGH)
                ? RTSFR_POL_REASON_HIGH_CRIT_EXHAUSTED
                : RTSFR_POL_REASON_RETRY_EXHAUSTED);
        entry->last_reason_code = decision.reason_code;
        return decision;
    }

    if (resource->generation != fault->generation)
    {
        decision = prv_decision(fault, RTSFR_ACT_SAFE_MODE,
                                RTSFR_POL_REASON_UNSAFE_UNKNOWN_RES);
    }
    else if (resource->mutex_held_count > 0U)
    {
        decision = prv_decision(fault, RTSFR_ACT_SAFE_MODE,
                                RTSFR_POL_REASON_UNSAFE_MUTEX_HELD);
    }
    else if (resource->unknown_resource_count > 0U)
    {
        decision = prv_decision(fault, RTSFR_ACT_SAFE_MODE,
                                RTSFR_POL_REASON_UNSAFE_UNKNOWN_RES);
    }
    else if (resource->has_inflight_io)
    {
        decision = prv_decision(fault, RTSFR_ACT_SAFE_MODE,
                                RTSFR_POL_REASON_UNSAFE_INFLIGHT_IO);
    }
    else if (!resource->can_reclaim_all)
    {
        decision = prv_decision(fault, RTSFR_ACT_SAFE_MODE,
                                RTSFR_POL_REASON_UNSAFE_CANNOT_RECLAIM);
    }
    else
    {
        if (entry->retry_count == 0U)
        {
            entry->window_start_tick = fault->observed_tick;
        }
        entry->retry_count++;
        entry->state = RTSFR_TASK_QUIESCING;
        entry->last_severity =
            (resource->managed_buffers_owned > 0U)
                ? RTSFR_SEV_RECOVERABLE_RES
                : RTSFR_SEV_TRANSIENT;
        decision = prv_decision(
            fault, RTSFR_ACT_RESTART,
            (resource->managed_buffers_owned > 0U)
                ? RTSFR_POL_REASON_SAFE_BUFFER_RECLAIM
                : RTSFR_POL_REASON_SAFE_NO_RESOURCE);
        entry->last_reason_code = decision.reason_code;
        return decision;
    }

    entry->state = RTSFR_TASK_SAFE_MODE;
    entry->last_severity = RTSFR_SEV_HAZARDOUS_RES;
    entry->last_reason_code = decision.reason_code;
    return decision;
}

bool rtsfr_policy_should_invoke_recovery(const rtsfr_decision_t *decision)
{
    return (decision != NULL) && (decision->event_id != 0U) &&
           ((decision->action == RTSFR_ACT_RESTART) ||
            (decision->action == RTSFR_ACT_SAFE_MODE) ||
            (decision->action == RTSFR_ACT_REQUEST_RESET));
}

void rtsfr_policy_record_result(uint8_t task_id,
                                rtsfr_recovery_outcome_t result,
                                TickType_t tick)
{
    rtsfr_policy_task_state_t *entry = prv_entry(task_id);
    (void)tick;
    if ((entry == NULL) || !entry->registered)
    {
        return;
    }
    if (result == RTSFR_RECOVERY_OK)
    {
        entry->active_generation++;
        entry->state = RTSFR_TASK_HEALTHY;
    }
    else if ((result == RTSFR_RECOVERY_REJECTED) ||
             (result == RTSFR_RECOVERY_STALE))
    {
        if (entry->retry_count > 0U)
        {
            entry->retry_count--;
        }
        entry->state = RTSFR_TASK_HEALTHY;
    }
    else if (result == RTSFR_RECOVERY_IN_PROGRESS)
    {
        entry->state = RTSFR_TASK_VERIFYING;
    }
    else
    {
        entry->state = RTSFR_TASK_SAFE_MODE;
        entry->last_reason_code = RTSFR_POL_REASON_RECOVERY_FAILED;
    }
}

void rtsfr_policy_on_recovery_result(const rtsfr_recovery_result_t *result)
{
    rtsfr_policy_task_state_t *entry;
    if (result == NULL)
    {
        return;
    }
    entry = prv_entry(result->task_id);
    if ((entry == NULL) || !entry->registered)
    {
        return;
    }
    if (result->outcome == RTSFR_RECOVERY_OK)
    {
        entry->active_generation = result->new_generation;
        entry->state = RTSFR_TASK_HEALTHY;
    }
    else if ((result->outcome == RTSFR_RECOVERY_REJECTED) &&
             (result->reason_code ==
              (uint16_t)RTSFR_REASON_SELF_RECOVERY_FORBIDDEN))
    {
        entry->state = RTSFR_TASK_SAFE_MODE;
    }
    else
    {
        rtsfr_policy_record_result(result->task_id, result->outcome,
                                   result->finished_tick);
    }
}

rtsfr_status_t rtsfr_policy_get_task_state(uint8_t task_id,
                                           rtsfr_policy_task_state_t *out)
{
    rtsfr_policy_task_state_t *entry = prv_entry(task_id);
    if ((entry == NULL) || (out == NULL) || !entry->registered)
    {
        return RTSFR_ERR_INVALID;
    }
    *out = *entry;
    return RTSFR_OK;
}
