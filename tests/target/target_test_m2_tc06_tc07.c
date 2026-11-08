/**
 * @file target_test_m2_tc06_tc07.c
 * @brief Tích hợp M1-M2-M3-M4 và kịch bản kiểm thử nghiệm thu TC06, TC07 trên STM32 / Host
 * @details Phụ trách: Đinh Vũ Long (Module 2 — Fault Classification & Recovery Policy)
 */

#include <stdio.h>
#include <string.h>

#include "rtsfr_detect.h"
#include "rtsfr_policy.h"
#include "rtsfr_recovery.h"
#include "rtsfr_resource.h"
#include "rtsfr_safe_mode.h"
#include "rtsfr_types.h"

/* ==========================================================================
 * 1. HÀM CẦU NỐI TÍCH HỢP (SUPERVISOR BRIDGE: M1 -> M3 -> M2 -> M4)
 * Hàm này được gọi trong vòng lặp của RTSFR_Supervisor trên STM32F103C8T6.
 * ========================================================================== */
rtsfr_status_t rtsfr_m2_supervisor_dispatch_once(rtsfr_decision_t *out_decision)
{
    rtsfr_fault_event_t      ev;
    rtsfr_resource_summary_t res;
    rtsfr_decision_t         dec;
    rtsfr_status_t           snap_status;

    /* Bước 1: Lấy 1 sự kiện lỗi từ hàng đợi của Module 1 (Việt Anh) */
    if (!rtsfr_detect_pop_event(&ev))
    {
        return RTSFR_ERR_NO_RESOURCE;
    }

    /* Bước 2: Lấy bản chụp tài nguyên của task đó từ Module 3 (Hồng Long) */
    (void)memset(&res, 0, sizeof(res));
    snap_status = rtsfr_resource_snapshot(ev.task_id, ev.generation, &res);

    if (snap_status != RTSFR_OK)
    {
        /*
         * Quy tắc an toàn mục 8.2: Khi không lấy được trạng thái tài nguyên rõ ràng,
         * bắt buộc đánh dấu UNSAFE/UNKNOWN, tuyệt đối không tự suy diễn là safe.
         */
        res.task_id                = ev.task_id;
        res.generation             = ev.generation;
        res.unknown_resource_count = 1U;
        res.can_reclaim_all        = false;
    }

    /* Bước 3: Module 2 (Vũ Long) phân loại lỗi và ra quyết định tất định */
    dec = rtsfr_policy_evaluate(&ev, &res);

    if (out_decision != NULL)
    {
        *out_decision = dec;
    }

    /* Bước 4: Nếu quyết định cần hành động (RESTART / SAFE_MODE / REQUEST_RESET),
     * chuyển tiếp sang Module 4 (Tú) thông qua rtsfr_recovery_request() */
    if (rtsfr_policy_should_invoke_recovery(&dec))
    {
        return rtsfr_recovery_request(&dec);
    }

    return RTSFR_OK;
}

/* ==========================================================================
 * 2. KỊCH BẢN NGHIỆM THU TC06 (Hồng Long + Vũ Long phụ trách - Mục 13 Phân công)
 * Đề bài TC06: ControlTask (task_id = 3) đang giữ Mutex (mutex_held_count = 1)
 * và ngừng báo tiến độ (RTSFR_FAULT_NO_PROGRESS).
 * Kỳ vọng: FAULT_DETECTED -> RESOURCE_UNSAFE -> SAFE_MODE; cấm tự unlock/restart.
 * ========================================================================== */
bool rtsfr_m2_verify_tc06_mutex_held_safe_mode(const char *run_id)
{
    rtsfr_task_config_t      m1_cfg;
    rtsfr_fault_event_t      ev;
    rtsfr_resource_summary_t res;
    rtsfr_decision_t         dec;
    rtsfr_policy_task_state_t st;

    if (run_id == NULL)
    {
        run_id = "20261108_TC06";
    }

    rtsfr_policy_init();

    /* Đăng ký ControlTask (task_id = 3, HIGH criticality) theo chuẩn struct M1 */
    m1_cfg.task_id       = 3U;
    m1_cfg.task_name     = "ControlTask";
    m1_cfg.timeout_ticks = 500U;
    m1_cfg.criticality   = (uint8_t)RTSFR_CRIT_HIGH;

    if (rtsfr_policy_register_from_config(&m1_cfg, 1U) != RTSFR_OK)
    {
        return false;
    }

    /* Giả lập sự kiện lỗi F01/F06 từ Module 1 (Việt Anh) dùng trường detected_tick */
    (void)memset(&ev, 0, sizeof(ev));
    ev.event_id          = 601U;
    ev.task_id           = 3U;
    ev.generation        = 1U;
    ev.fault_type        = RTSFR_FAULT_NO_PROGRESS;
    ev.detected_tick     = 1520U;
    ev.last_progress_seq = 14U;

    /* Giả lập bản chụp tài nguyên từ Module 3 (Hồng Long): đang giữ 1 Mutex */
    (void)memset(&res, 0, sizeof(res));
    res.task_id                = 3U;
    res.generation             = 1U;
    res.mutex_held_count       = 1U;
    res.managed_buffers_owned  = 0U;
    res.unknown_resource_count = 0U;
    res.has_inflight_io        = false;
    res.can_reclaim_all        = false;

    /* Đánh giá qua Module 2 (Vũ Long) */
    dec = rtsfr_policy_evaluate(&ev, &res);

    /* Xuất log định dạng CSV chuẩn mục 8.3 Đặc tả hệ thống */
    printf("%s,V2,TC06,%u,%u,%u,%u,FAULT_DETECTED,NO_PROGRESS,%u\n",
           run_id, (unsigned)ev.detected_tick, (unsigned)ev.event_id,
           (unsigned)ev.task_id, (unsigned)ev.generation,
           (unsigned)ev.last_progress_seq);

    printf("%s,V2,TC06,%u,%u,%u,%u,POLICY,%s,0x%04X\n",
           run_id, (unsigned)(ev.detected_tick + 1U), (unsigned)dec.event_id,
           (unsigned)dec.task_id, (unsigned)dec.generation,
           (dec.action == RTSFR_ACT_SAFE_MODE) ? "SAFE_MODE" : "UNEXPECTED",
           (unsigned)dec.reason_code);

    if (rtsfr_policy_get_task_state(3U, &st) != RTSFR_OK)
    {
        return false;
    }

    return (dec.action == RTSFR_ACT_SAFE_MODE) &&
           (dec.reason_code == (uint16_t)RTSFR_POL_REASON_UNSAFE_MUTEX_HELD) &&
           (st.state == RTSFR_TASK_SAFE_MODE) &&
           (st.retry_count == 0U);
}

/* ==========================================================================
 * 3. KỊCH BẢN NGHIỆM THU TC07 (Vũ Long phụ trách chính - Mục 13 Phân công)
 * Đề bài TC07: CommTask (task_id = 2) bị lỗi lặp lại 3 lần trong cửa sổ 60 giây
 * với max_restarts = 2.
 * Kỳ vọng: Lần 1 -> RESTART (gen 1->2); Lần 2 -> RESTART (gen 2->3);
 *          Lần 3 -> Chặn RESTART, chuyển sang SAFE_MODE (REASON_RETRY_EXHAUSTED).
 * ========================================================================== */
bool rtsfr_m2_verify_tc07_retry_budget_exhaustion(const char *run_id)
{
    const TickType_t fault_ticks[3] = {1000U, 18000U, 35000U};
    rtsfr_decision_t decisions[3];
    uint16_t         current_gen = 1U;
    size_t           i;

    if (run_id == NULL)
    {
        run_id = "20261108_TC07";
    }

    rtsfr_policy_init();
    if (rtsfr_policy_register_task(2U, current_gen, RTSFR_CRIT_LOW) != RTSFR_OK)
    {
        return false;
    }
    (void)rtsfr_policy_set_retry_policy(2U, 2U, 60000U);

    for (i = 0U; i < 3U; i++)
    {
        rtsfr_fault_event_t      ev;
        rtsfr_resource_summary_t res;

        (void)memset(&ev, 0, sizeof(ev));
        ev.event_id          = (uint32_t)(701U + i);
        ev.task_id           = 2U;
        ev.generation        = current_gen;
        ev.fault_type        = RTSFR_FAULT_NO_PROGRESS;
        ev.detected_tick     = fault_ticks[i];
        ev.last_progress_seq = (uint32_t)(10U * (i + 1U));

        (void)memset(&res, 0, sizeof(res));
        res.task_id                = 2U;
        res.generation             = current_gen;
        res.mutex_held_count       = 0U;
        res.managed_buffers_owned  = 0U;
        res.unknown_resource_count = 0U;
        res.has_inflight_io        = false;
        res.can_reclaim_all        = true;

        decisions[i] = rtsfr_policy_evaluate(&ev, &res);

        printf("%s,V2,TC07,%u,%u,%u,%u,FAULT_DETECTED,NO_PROGRESS,%u\n",
               run_id, (unsigned)ev.detected_tick, (unsigned)ev.event_id,
               (unsigned)ev.task_id, (unsigned)ev.generation,
               (unsigned)ev.last_progress_seq);

        printf("%s,V2,TC07,%u,%u,%u,%u,POLICY,%s,0x%04X\n",
               run_id, (unsigned)(ev.detected_tick + 1U),
               (unsigned)decisions[i].event_id,
               (unsigned)decisions[i].task_id,
               (unsigned)decisions[i].generation,
               (decisions[i].action == RTSFR_ACT_RESTART) ? "RESTART" :
               (decisions[i].action == RTSFR_ACT_SAFE_MODE) ? "SAFE_MODE" : "OTHER",
               (unsigned)decisions[i].reason_code);

        /* Nếu được phép RESTART, mô phỏng M4 hoàn tất phục hồi thành công sang gen mới */
        if (decisions[i].action == RTSFR_ACT_RESTART)
        {
            rtsfr_recovery_result_t m4_res;
            (void)memset(&m4_res, 0, sizeof(m4_res));
            m4_res.event_id       = ev.event_id;
            m4_res.task_id        = 2U;
            m4_res.old_generation = current_gen;
            m4_res.new_generation = (uint16_t)(current_gen + 1U);
            m4_res.outcome        = RTSFR_RECOVERY_OK;
            m4_res.reason_code    = (uint16_t)RTSFR_REASON_FIRST_PROGRESS;
            m4_res.started_tick   = ev.detected_tick + 1U;
            m4_res.finished_tick  = ev.detected_tick + 45U;

            rtsfr_policy_on_recovery_result(&m4_res);
            current_gen = m4_res.new_generation;

            printf("%s,V2,TC07,%u,%u,%u,%u,RECOVERY_OK,FIRST_PROGRESS,44\n",
                   run_id, (unsigned)m4_res.finished_tick,
                   (unsigned)m4_res.event_id, (unsigned)m4_res.task_id,
                   (unsigned)m4_res.new_generation);
        }
    }

    return (decisions[0].action == RTSFR_ACT_RESTART) &&
           (decisions[1].action == RTSFR_ACT_RESTART) &&
           (decisions[2].action == RTSFR_ACT_SAFE_MODE) &&
           (decisions[2].reason_code == (uint16_t)RTSFR_POL_REASON_RETRY_EXHAUSTED);
}

#ifdef RTSFR_STANDALONE_TARGET_TEST_MAIN
/* Stubs cho M1, M3, M4 khi chạy kiểm thử độc lập trên PC */
bool rtsfr_detect_pop_event(rtsfr_fault_event_t *event)
{
    (void)event;
    return false;
}

rtsfr_status_t rtsfr_resource_snapshot(uint8_t task_id,
                                       uint16_t generation,
                                       rtsfr_resource_summary_t *out)
{
    (void)task_id;
    (void)generation;
    (void)out;
    return RTSFR_OK;
}

rtsfr_status_t rtsfr_recovery_request(const rtsfr_decision_t *decision)
{
    (void)decision;
    return RTSFR_OK;
}

int main(void)
{
    bool tc06_ok;
    bool tc07_ok;

    printf("run_id,version,test_id,tick,event_id,task_id,generation,event,reason,value\n");
    tc06_ok = rtsfr_m2_verify_tc06_mutex_held_safe_mode("20261108_01");
    tc07_ok = rtsfr_m2_verify_tc07_retry_budget_exhaustion("20261108_02");

    printf("\n[VERDICT] TC06 (Mutex held -> SAFE_MODE): %s\n", tc06_ok ? "PASS" : "FAIL");
    printf("[VERDICT] TC07 (Retry budget exhausted -> SAFE_MODE): %s\n", tc07_ok ? "PASS" : "FAIL");

    return (tc06_ok && tc07_ok) ? 0 : 1;
}
#endif /* RTSFR_STANDALONE_TARGET_TEST_MAIN */
