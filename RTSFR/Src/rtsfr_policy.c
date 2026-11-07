#include "rtsfr_policy.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

/* ==========================================================================
 * BẢNG TRẠNG THÁI TĨNH CỦA MODULE 2 (KHÔNG DÙNG MALLOC / TIẾT KIỆM SRAM)
 * ========================================================================== */
static rtsfr_policy_task_state_t s_policy_tasks[configRTSFR_MAX_TASKS];

/* ==========================================================================
 * CÁC HÀM NỘI BỘ (PRIVATE HELPERS)
 * ========================================================================== */

static rtsfr_policy_task_state_t *prv_get_task_entry(uint8_t task_id)
{
    if ((task_id == 0U) || (task_id > configRTSFR_MAX_TASKS))
    {
        return NULL;
    }
    return &s_policy_tasks[task_id - 1U];
}

static rtsfr_decision_t prv_make_decision(uint32_t event_id,
                                          uint8_t task_id,
                                          uint16_t generation,
                                          rtsfr_action_t action,
                                          uint16_t reason_code)
{
    rtsfr_decision_t dec;
    dec.event_id    = event_id;
    dec.task_id     = task_id;
    dec.generation  = generation;
    dec.action      = action;
    dec.reason_code = reason_code;
    return dec;
}

static bool prv_is_fatal_fault(rtsfr_fault_t fault_type)
{
    return (fault_type == RTSFR_FAULT_STACK_OVERFLOW) ||
           (fault_type == RTSFR_FAULT_HARD_FAULT) ||
           (fault_type == RTSFR_FAULT_CORRUPTION);
}

static bool prv_is_recovery_in_progress(rtsfr_task_state_t state)
{
    return (state == RTSFR_TASK_QUIESCING) ||
           (state == RTSFR_TASK_CLEANUP) ||
           (state == RTSFR_TASK_RECREATE) ||
           (state == RTSFR_TASK_VERIFYING);
}

/*
 * Kiểm tra xem cửa sổ thời gian retry đã hết hạn chưa bằng phép trừ không dấu
 * (an toàn tuyệt đối khi bộ đếm TickType_t bị tràn - wrap-around).
 * Nếu đã qua cửa sổ retry_window_ticks, tự động reset retry_count về 0.
 */
static void prv_refresh_retry_window(rtsfr_policy_task_state_t *entry,
                                     TickType_t observed_tick)
{
    if (entry->retry_count > 0U)
    {
        TickType_t elapsed = (TickType_t)(observed_tick - entry->window_start_tick);
        if (elapsed >= entry->retry_window_ticks)
        {
            entry->retry_count       = 0U;
            entry->window_start_tick = observed_tick;
        }
    }
}

/* ==========================================================================
 * CÁC HÀM PUBLIC API CỦA MODULE 2
 * ========================================================================== */

void rtsfr_policy_init(void)
{
    uint8_t i;
    (void)memset(s_policy_tasks, 0, sizeof(s_policy_tasks));

    for (i = 0U; i < configRTSFR_MAX_TASKS; i++)
    {
        s_policy_tasks[i].task_id            = (uint8_t)(i + 1U);
        s_policy_tasks[i].active_generation  = 1U;
        s_policy_tasks[i].criticality        = RTSFR_CRIT_MEDIUM;
        s_policy_tasks[i].state              = RTSFR_TASK_UNREGISTERED;
        s_policy_tasks[i].max_restarts       = (uint8_t)configRTSFR_MAX_RESTARTS;
        s_policy_tasks[i].retry_window_ticks = configRTSFR_RETRY_WINDOW_TICKS;
        s_policy_tasks[i].last_severity      = RTSFR_SEV_NONE;
        s_policy_tasks[i].last_reason_code   = (uint16_t)RTSFR_POL_REASON_NONE;
    }
}

rtsfr_status_t rtsfr_policy_register_task(uint8_t task_id,
                                          uint16_t initial_generation,
                                          rtsfr_criticality_t criticality)
{
    rtsfr_policy_task_state_t *entry = prv_get_task_entry(task_id);

    if ((entry == NULL) || (initial_generation == 0U))
    {
        return RTSFR_ERR_INVALID;
    }

    if ((criticality != RTSFR_CRIT_LOW) &&
        (criticality != RTSFR_CRIT_MEDIUM) &&
        (criticality != RTSFR_CRIT_HIGH))
    {
        return RTSFR_ERR_INVALID;
    }

    entry->registered         = true;
    entry->task_id            = task_id;
    entry->active_generation  = initial_generation;
    entry->criticality        = criticality;
    entry->state              = RTSFR_TASK_HEALTHY;
    entry->retry_count        = 0U;
    entry->window_start_tick  = 0U;
    entry->last_event_id      = 0U;
    entry->has_last_event     = false;
    entry->last_severity      = RTSFR_SEV_NONE;
    entry->last_reason_code   = (uint16_t)RTSFR_POL_REASON_NONE;

    return RTSFR_OK;
}

rtsfr_status_t rtsfr_policy_set_retry_policy(uint8_t task_id,
                                             uint8_t max_restarts,
                                             TickType_t retry_window_ticks)
{
    rtsfr_policy_task_state_t *entry = prv_get_task_entry(task_id);

    if ((entry == NULL) || (retry_window_ticks == 0U))
    {
        return RTSFR_ERR_INVALID;
    }

    entry->max_restarts       = max_restarts;
    entry->retry_window_ticks = retry_window_ticks;
    return RTSFR_OK;
}

rtsfr_status_t rtsfr_policy_set_task_state(uint8_t task_id,
                                           uint16_t generation,
                                           rtsfr_task_state_t new_state)
{
    rtsfr_policy_task_state_t *entry = prv_get_task_entry(task_id);

    if ((entry == NULL) || (!entry->registered))
    {
        return RTSFR_ERR_INVALID;
    }

    if (generation != entry->active_generation)
    {
        return RTSFR_ERR_STALE;
    }

    entry->state = new_state;
    return RTSFR_OK;
}

bool rtsfr_policy_should_invoke_recovery(const rtsfr_decision_t *decision)
{
    if ((decision == NULL) || (decision->event_id == 0U))
    {
        return false;
    }

    return (decision->action == RTSFR_ACT_RESTART) ||
           (decision->action == RTSFR_ACT_SAFE_MODE) ||
           (decision->action == RTSFR_ACT_REQUEST_RESET);
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

    if (prv_is_fatal_fault(fault->fault_type) ||
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

    if (resource == NULL)
    {
        return RTSFR_SEV_HAZARDOUS_RES;
    }

    if ((resource->generation != fault->generation) ||
        (resource->mutex_held_count > 0U) ||
        (resource->unknown_resource_count > 0U) ||
        (resource->has_inflight_io) ||
        (!resource->can_reclaim_all))
    {
        return RTSFR_SEV_HAZARDOUS_RES;
    }

    if (resource->managed_buffers_owned > 0U)
    {
        return RTSFR_SEV_RECOVERABLE_RES;
    }

    return RTSFR_SEV_TRANSIENT;
}

rtsfr_decision_t rtsfr_policy_evaluate(const rtsfr_fault_event_t *fault,
                                       const rtsfr_resource_summary_t *resource)
{
    rtsfr_policy_task_state_t *entry;
    uint16_t reason;
    rtsfr_action_t action;

    /* ======================================================================
     * CỔNG 1: KIỂM TRA HỢP LỆ ĐẦU VÀO & CHỐNG SỰ KIỆN CŨ / TRÙNG LẶP
     * Đồng bộ với rtsfr_recovery_request(): event_id == 0U là không hợp lệ.
     * ====================================================================== */
    if (fault == NULL)
    {
        return prv_make_decision(0U, 0U, 0U,
                                 RTSFR_ACT_IGNORE,
                                 (uint16_t)RTSFR_POL_REASON_INVALID_INPUT);
    }

    entry = prv_get_task_entry(fault->task_id);
    if ((entry == NULL) || (!entry->registered) ||
        (fault->event_id == 0U) || (resource == NULL) ||
        (resource->task_id != fault->task_id))
    {
        return prv_make_decision(fault->event_id,
                                 fault->task_id,
                                 fault->generation,
                                 RTSFR_ACT_IGNORE,
                                 (uint16_t)RTSFR_POL_REASON_INVALID_INPUT);
    }

    /* Invariant I2 (TC10): Bỏ qua event của generation cũ */
    if (fault->generation != entry->active_generation)
    {
        entry->last_severity    = RTSFR_SEV_NONE;
        entry->last_reason_code = (uint16_t)RTSFR_POL_REASON_STALE_GENERATION;
        return prv_make_decision(fault->event_id,
                                 fault->task_id,
                                 fault->generation,
                                 RTSFR_ACT_IGNORE,
                                 (uint16_t)RTSFR_POL_REASON_STALE_GENERATION);
    }

    /* Chống xử lý 2 lần cho cùng một event_id */
    if (entry->has_last_event && (fault->event_id == entry->last_event_id))
    {
        entry->last_severity    = RTSFR_SEV_NONE;
        entry->last_reason_code = (uint16_t)RTSFR_POL_REASON_DUPLICATE_EVENT;
        return prv_make_decision(fault->event_id,
                                 fault->task_id,
                                 fault->generation,
                                 RTSFR_ACT_IGNORE,
                                 (uint16_t)RTSFR_POL_REASON_DUPLICATE_EVENT);
    }

    /* ======================================================================
     * CỔNG 2: KIỂM TRA LỖI NGHIÊM TRỌNG MỨC KERNEL / PHẦN CỨNG (F08, F09)
     * ====================================================================== */
    if (prv_is_fatal_fault(fault->fault_type))
    {
        entry->has_last_event   = true;
        entry->last_event_id    = fault->event_id;
        entry->last_severity    = RTSFR_SEV_FATAL;
        entry->last_reason_code = (uint16_t)RTSFR_POL_REASON_UNTRUSTED_STATE;
        entry->state            = RTSFR_TASK_SAFE_MODE;
        return prv_make_decision(fault->event_id,
                                 fault->task_id,
                                 fault->generation,
                                 RTSFR_ACT_REQUEST_RESET,
                                 (uint16_t)RTSFR_POL_REASON_UNTRUSTED_STATE);
    }

    if (fault->fault_type == RTSFR_FAULT_KERNEL_STALL)
    {
        entry->has_last_event   = true;
        entry->last_event_id    = fault->event_id;
        entry->last_severity    = RTSFR_SEV_FATAL;
        entry->last_reason_code = (uint16_t)RTSFR_POL_REASON_KERNEL_STALL;
        entry->state            = RTSFR_TASK_SAFE_MODE;
        return prv_make_decision(fault->event_id,
                                 fault->task_id,
                                 fault->generation,
                                 RTSFR_ACT_REQUEST_RESET,
                                 (uint16_t)RTSFR_POL_REASON_KERNEL_STALL);
    }

    /* ======================================================================
     * CỔNG 3: LỌC CHỜ HỢP LỆ (F04/F10) & TRẠNG THÁI HIỆN TẠI CỦA TASK
     * ====================================================================== */
    if ((fault->fault_type == RTSFR_FAULT_NONE) ||
        (fault->fault_type == RTSFR_FAULT_EXPECTED_WAIT))
    {
        entry->has_last_event   = true;
        entry->last_event_id    = fault->event_id;
        entry->last_severity    = RTSFR_SEV_NONE;
        entry->last_reason_code = (uint16_t)RTSFR_POL_REASON_LEGITIMATE_WAIT;
        return prv_make_decision(fault->event_id,
                                 fault->task_id,
                                 fault->generation,
                                 RTSFR_ACT_IGNORE,
                                 (uint16_t)RTSFR_POL_REASON_LEGITIMATE_WAIT);
    }

    if (entry->state == RTSFR_TASK_SAFE_MODE)
    {
        entry->has_last_event   = true;
        entry->last_event_id    = fault->event_id;
        entry->last_severity    = RTSFR_SEV_HAZARDOUS_RES;
        entry->last_reason_code = (uint16_t)RTSFR_POL_REASON_ALREADY_IN_SAFE_MODE;
        return prv_make_decision(fault->event_id,
                                 fault->task_id,
                                 fault->generation,
                                 RTSFR_ACT_IGNORE,
                                 (uint16_t)RTSFR_POL_REASON_ALREADY_IN_SAFE_MODE);
    }

    if (prv_is_recovery_in_progress(entry->state))
    {
        entry->has_last_event   = true;
        entry->last_event_id    = fault->event_id;
        entry->last_severity    = RTSFR_SEV_TRANSIENT;
        entry->last_reason_code = (uint16_t)RTSFR_POL_REASON_RECOVERY_IN_PROGRESS;
        return prv_make_decision(fault->event_id,
                                 fault->task_id,
                                 fault->generation,
                                 RTSFR_ACT_DEFER,
                                 (uint16_t)RTSFR_POL_REASON_RECOVERY_IN_PROGRESS);
    }

    /* Ghi nhận event_id hợp lệ để không xử lý lặp */
    entry->has_last_event = true;
    entry->last_event_id  = fault->event_id;

    /* ======================================================================
     * CỔNG 4: KIỂM TRA NGÂN SÁCH THỬ LẠI (RETRY BUDGET WINDOW - F07 / TC07)
     * Đồng thời chặn tràn số generation == UINT16_MAX theo rtsfr_recovery.c
     * ====================================================================== */
    prv_refresh_retry_window(entry, fault->observed_tick);

    if ((entry->retry_count >= entry->max_restarts) ||
        (entry->active_generation == UINT16_MAX))
    {
        if (entry->criticality == RTSFR_CRIT_HIGH)
        {
            entry->last_severity    = RTSFR_SEV_FATAL;
            entry->last_reason_code = (uint16_t)RTSFR_POL_REASON_HIGH_CRIT_EXHAUSTED;
            entry->state            = RTSFR_TASK_SAFE_MODE;
            return prv_make_decision(fault->event_id,
                                     fault->task_id,
                                     fault->generation,
                                     RTSFR_ACT_REQUEST_RESET,
                                     (uint16_t)RTSFR_POL_REASON_HIGH_CRIT_EXHAUSTED);
        }

        entry->last_severity    = RTSFR_SEV_PERSISTENT;
        entry->last_reason_code = (uint16_t)RTSFR_POL_REASON_RETRY_EXHAUSTED;
        entry->state            = RTSFR_TASK_SAFE_MODE;
        return prv_make_decision(fault->event_id,
                                 fault->task_id,
                                 fault->generation,
                                 RTSFR_ACT_SAFE_MODE,
                                 (uint16_t)RTSFR_POL_REASON_RETRY_EXHAUSTED);
    }

    /* ======================================================================
     * CỔNG 5: KIỂM TRA AN TOÀN TÀI NGUYÊN (RESOURCE SAFETY GATE - F05 / F06)
     * Quy tắc cứng: Bất kỳ dấu hiệu unsafe/unknown nào đều CẤM RESTART!
     * ====================================================================== */
    if (resource->generation != fault->generation)
    {
        entry->last_severity    = RTSFR_SEV_HAZARDOUS_RES;
        entry->last_reason_code = (uint16_t)RTSFR_POL_REASON_UNSAFE_UNKNOWN_RES;
        entry->state            = RTSFR_TASK_SAFE_MODE;
        return prv_make_decision(fault->event_id,
                                 fault->task_id,
                                 fault->generation,
                                 RTSFR_ACT_SAFE_MODE,
                                 (uint16_t)RTSFR_POL_REASON_UNSAFE_UNKNOWN_RES);
    }

    if (resource->mutex_held_count > 0U)
    {
        entry->last_severity    = RTSFR_SEV_HAZARDOUS_RES;
        entry->last_reason_code = (uint16_t)RTSFR_POL_REASON_UNSAFE_MUTEX_HELD;
        entry->state            = RTSFR_TASK_SAFE_MODE;
        return prv_make_decision(fault->event_id,
                                 fault->task_id,
                                 fault->generation,
                                 RTSFR_ACT_SAFE_MODE,
                                 (uint16_t)RTSFR_POL_REASON_UNSAFE_MUTEX_HELD);
    }

    if (resource->unknown_resource_count > 0U)
    {
        entry->last_severity    = RTSFR_SEV_HAZARDOUS_RES;
        entry->last_reason_code = (uint16_t)RTSFR_POL_REASON_UNSAFE_UNKNOWN_RES;
        entry->state            = RTSFR_TASK_SAFE_MODE;
        return prv_make_decision(fault->event_id,
                                 fault->task_id,
                                 fault->generation,
                                 RTSFR_ACT_SAFE_MODE,
                                 (uint16_t)RTSFR_POL_REASON_UNSAFE_UNKNOWN_RES);
    }

    if (resource->has_inflight_io)
    {
        entry->last_severity    = RTSFR_SEV_HAZARDOUS_RES;
        entry->last_reason_code = (uint16_t)RTSFR_POL_REASON_UNSAFE_INFLIGHT_IO;
        entry->state            = RTSFR_TASK_SAFE_MODE;
        return prv_make_decision(fault->event_id,
                                 fault->task_id,
                                 fault->generation,
                                 RTSFR_ACT_SAFE_MODE,
                                 (uint16_t)RTSFR_POL_REASON_UNSAFE_INFLIGHT_IO);
    }

    if (!resource->can_reclaim_all)
    {
        entry->last_severity    = RTSFR_SEV_HAZARDOUS_RES;
        entry->last_reason_code = (uint16_t)RTSFR_POL_REASON_UNSAFE_CANNOT_RECLAIM;
        entry->state            = RTSFR_TASK_SAFE_MODE;
        return prv_make_decision(fault->event_id,
                                 fault->task_id,
                                 fault->generation,
                                 RTSFR_ACT_SAFE_MODE,
                                 (uint16_t)RTSFR_POL_REASON_UNSAFE_CANNOT_RECLAIM);
    }

    /* ======================================================================
     * CỔNG 6: CẤP PHÉP PHỤC HỒI CÓ CHỌN LỌC (SELECTIVE RESTART)
     * ====================================================================== */
    if (entry->retry_count == 0U)
    {
        entry->window_start_tick = fault->observed_tick;
    }
    entry->retry_count++;
    entry->state = RTSFR_TASK_QUIESCING;

    if (resource->managed_buffers_owned > 0U)
    {
        entry->last_severity = RTSFR_SEV_RECOVERABLE_RES;
        reason               = (uint16_t)RTSFR_POL_REASON_SAFE_BUFFER_RECLAIM;
    }
    else
    {
        entry->last_severity = RTSFR_SEV_TRANSIENT;
        reason               = (uint16_t)RTSFR_POL_REASON_SAFE_NO_RESOURCE;
    }

    action                  = RTSFR_ACT_RESTART;
    entry->last_reason_code = reason;

    return prv_make_decision(fault->event_id,
                             fault->task_id,
                             fault->generation,
                             action,
                             reason);
}

void rtsfr_policy_record_result(uint8_t task_id,
                                rtsfr_recovery_outcome_t result,
                                TickType_t tick)
{
    rtsfr_policy_task_state_t *entry = prv_get_task_entry(task_id);
    (void)tick;

    if ((entry == NULL) || (!entry->registered))
    {
        return;
    }

    switch (result)
    {
        case RTSFR_RECOVERY_OK:
            entry->active_generation++;
            entry->state = RTSFR_TASK_HEALTHY;
            break;

        case RTSFR_RECOVERY_IN_PROGRESS:
            entry->state = RTSFR_TASK_VERIFYING;
            break;

        case RTSFR_RECOVERY_REJECTED:
        case RTSFR_RECOVERY_STALE:
            /* Nếu hủy báo động giả (false alarm rollback), hoàn lại 1 lượt retry */
            if (entry->retry_count > 0U)
            {
                entry->retry_count--;
            }
            entry->state = RTSFR_TASK_HEALTHY;
            break;

        case RTSFR_RECOVERY_UNSAFE_RESOURCE:
        case RTSFR_RECOVERY_CLEANUP_FAILED:
        case RTSFR_RECOVERY_CREATE_FAILED:
        case RTSFR_RECOVERY_VERIFY_TIMEOUT:
        case RTSFR_RECOVERY_INTERNAL_ERROR:
        default:
            entry->state            = RTSFR_TASK_SAFE_MODE;
            entry->last_reason_code = (uint16_t)RTSFR_POL_REASON_RECOVERY_FAILED;
            break;
    }
}

void rtsfr_policy_on_recovery_result(const rtsfr_recovery_result_t *result)
{
    rtsfr_policy_task_state_t *entry;

    if (result == NULL)
    {
        return;
    }

    entry = prv_get_task_entry(result->task_id);
    if ((entry == NULL) || (!entry->registered))
    {
        return;
    }

    if (result->outcome == RTSFR_RECOVERY_OK)
    {
        /* Đồng bộ chính xác với new_generation do Module 4 (Tú) tạo ra */
        if (result->new_generation > entry->active_generation)
        {
            entry->active_generation = result->new_generation;
        }
        else
        {
            entry->active_generation++;
        }
        entry->state = RTSFR_TASK_HEALTHY;
        return;
    }

    /*
     * Trong rtsfr_recovery.c của Tú, RTSFR_RECOVERY_REJECTED có thể do:
     * 1) RTSFR_REASON_FALSE_ALARM_ROLLBACK (task được vTaskResume, vẫn HEALTHY)
     * 2) RTSFR_REASON_SELF_RECOVERY_FORBIDDEN (enter_safe_mode = true -> SAFE_MODE)
     */
    if ((result->outcome == RTSFR_RECOVERY_REJECTED) &&
        (result->reason_code == (uint16_t)RTSFR_REASON_SELF_RECOVERY_FORBIDDEN))
    {
        entry->state            = RTSFR_TASK_SAFE_MODE;
        entry->last_reason_code = (uint16_t)RTSFR_POL_REASON_RECOVERY_FAILED;
        return;
    }

    rtsfr_policy_record_result(result->task_id,
                               result->outcome,
                               result->finished_tick);
}

rtsfr_status_t rtsfr_policy_get_task_state(uint8_t task_id,
                                           rtsfr_policy_task_state_t *out)
{
    const rtsfr_policy_task_state_t *entry = prv_get_task_entry(task_id);

    if ((entry == NULL) || (out == NULL) || (!entry->registered))
    {
        return RTSFR_ERR_INVALID;
    }

    *out = *entry;
    return RTSFR_OK;
}
