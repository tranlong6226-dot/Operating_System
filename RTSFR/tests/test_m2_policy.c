#include <stdio.h>
#include <string.h>

#include "rtsfr_policy.h"

static uint32_t g_tests_run    = 0U;
static uint32_t g_tests_passed = 0U;
static uint32_t g_tests_failed = 0U;

#define EXPECT_TRUE(cond, msg)                                                 \
    do                                                                         \
    {                                                                          \
        if (!(cond))                                                           \
        {                                                                      \
            printf("  [FAIL] Line %d: %s\n", __LINE__, (msg));                 \
            return false;                                                      \
        }                                                                      \
    } while (0)

static rtsfr_fault_event_t make_fault(uint32_t event_id,
                                      uint8_t task_id,
                                      uint16_t generation,
                                      TickType_t tick,
                                      rtsfr_fault_t type)
{
    rtsfr_fault_event_t ev;
    ev.event_id      = event_id;
    ev.task_id       = task_id;
    ev.generation    = generation;
    ev.observed_tick = tick;
    ev.fault_type    = type;
    ev.evidence      = 500U;
    return ev;
}

static rtsfr_resource_summary_t make_clean_res(uint8_t task_id,
                                               uint16_t generation)
{
    rtsfr_resource_summary_t res;
    res.task_id                = task_id;
    res.generation             = generation;
    res.mutex_held_count       = 0U;
    res.managed_buffers_owned  = 0U;
    res.unknown_resource_count = 0U;
    res.has_inflight_io        = false;
    res.can_reclaim_all        = true;
    return res;
}

/* ==========================================================================
 * UT01: Kiểm tra đầu vào NULL, event_id == 0, task_id ngoài biên
 * ========================================================================== */
static bool test_ut01_invalid_inputs_and_boundaries(void)
{
    rtsfr_policy_init();
    EXPECT_TRUE(rtsfr_policy_register_task(1U, 1U, RTSFR_CRIT_LOW) == RTSFR_OK,
                "Register task 1 failed");
    EXPECT_TRUE(rtsfr_policy_register_task(0U, 1U, RTSFR_CRIT_LOW) == RTSFR_ERR_INVALID,
                "Task ID 0 must be invalid");
    EXPECT_TRUE(rtsfr_policy_register_task(configRTSFR_MAX_TASKS + 1U, 1U, RTSFR_CRIT_LOW) == RTSFR_ERR_INVALID,
                "Task ID > MAX_TASKS must be invalid");

    rtsfr_fault_event_t ev = make_fault(1U, 1U, 1U, 100U, RTSFR_FAULT_NO_PROGRESS);
    rtsfr_resource_summary_t res = make_clean_res(1U, 1U);

    /* NULL pointers */
    rtsfr_decision_t d1 = rtsfr_policy_evaluate(NULL, &res);
    EXPECT_TRUE(d1.action == RTSFR_ACT_IGNORE, "NULL fault must IGNORE");
    EXPECT_TRUE(d1.reason_code == RTSFR_POL_REASON_INVALID_INPUT, "Reason INVALID_INPUT");

    rtsfr_decision_t d2 = rtsfr_policy_evaluate(&ev, NULL);
    EXPECT_TRUE(d2.action == RTSFR_ACT_IGNORE, "NULL resource must IGNORE");
    EXPECT_TRUE(d2.reason_code == RTSFR_POL_REASON_INVALID_INPUT, "Reason INVALID_INPUT");

    /* event_id == 0U (Đồng bộ với rtsfr_recovery_request của M4) */
    ev.event_id = 0U;
    rtsfr_decision_t d3 = rtsfr_policy_evaluate(&ev, &res);
    EXPECT_TRUE(d3.action == RTSFR_ACT_IGNORE, "event_id 0 must IGNORE");
    EXPECT_TRUE(d3.reason_code == RTSFR_POL_REASON_INVALID_INPUT, "Reason INVALID_INPUT");

    /* Unregistered task 2 */
    ev = make_fault(2U, 2U, 1U, 100U, RTSFR_FAULT_NO_PROGRESS);
    res = make_clean_res(2U, 1U);
    rtsfr_decision_t d4 = rtsfr_policy_evaluate(&ev, &res);
    EXPECT_TRUE(d4.action == RTSFR_ACT_IGNORE, "Unregistered task must IGNORE");
    EXPECT_TRUE(d4.reason_code == RTSFR_POL_REASON_INVALID_INPUT, "Reason INVALID_INPUT");

    return true;
}

/* ==========================================================================
 * UT02: Sự kiện thuộc về thế hệ cũ (Stale Generation - TC10 / Invariant I2)
 * ========================================================================== */
static bool test_ut02_stale_generation_rejected(void)
{
    rtsfr_policy_init();
    rtsfr_policy_register_task(2U, 3U, RTSFR_CRIT_LOW); /* Active generation = 3 */

    rtsfr_fault_event_t ev = make_fault(10U, 2U, 2U, 500U, RTSFR_FAULT_NO_PROGRESS);
    rtsfr_resource_summary_t res = make_clean_res(2U, 2U);

    rtsfr_decision_t dec = rtsfr_policy_evaluate(&ev, &res);
    EXPECT_TRUE(dec.action == RTSFR_ACT_IGNORE, "Stale generation must IGNORE");
    EXPECT_TRUE(dec.reason_code == RTSFR_POL_REASON_STALE_GENERATION, "Reason STALE_GENERATION");
    EXPECT_TRUE(!rtsfr_policy_should_invoke_recovery(&dec), "Should not invoke M4 recovery");

    return true;
}

/* ==========================================================================
 * UT03: Chống xử lý 2 lần cho cùng một event_id (Duplicate Event - TC10)
 * ========================================================================== */
static bool test_ut03_duplicate_event_rejected(void)
{
    rtsfr_policy_init();
    rtsfr_policy_register_task(1U, 1U, RTSFR_CRIT_LOW);

    rtsfr_fault_event_t ev = make_fault(25U, 1U, 1U, 1000U, RTSFR_FAULT_NO_PROGRESS);
    rtsfr_resource_summary_t res = make_clean_res(1U, 1U);

    rtsfr_decision_t first = rtsfr_policy_evaluate(&ev, &res);
    EXPECT_TRUE(first.action == RTSFR_ACT_RESTART, "First event should RESTART");

    rtsfr_decision_t dup = rtsfr_policy_evaluate(&ev, &res);
    EXPECT_TRUE(dup.action == RTSFR_ACT_IGNORE, "Duplicate event_id must IGNORE");
    EXPECT_TRUE(dup.reason_code == RTSFR_POL_REASON_DUPLICATE_EVENT, "Reason DUPLICATE_EVENT");

    return true;
}

/* ==========================================================================
 * UT04: Task đang chờ hợp lệ (Legitimate Wait - F04 / F10 / TC04)
 * ========================================================================== */
static bool test_ut04_legitimate_wait_ignored(void)
{
    rtsfr_policy_init();
    rtsfr_policy_register_task(1U, 1U, RTSFR_CRIT_MEDIUM);

    rtsfr_fault_event_t ev = make_fault(30U, 1U, 1U, 1200U, RTSFR_FAULT_EXPECTED_WAIT);
    rtsfr_resource_summary_t res = make_clean_res(1U, 1U);

    rtsfr_decision_t dec = rtsfr_policy_evaluate(&ev, &res);
    EXPECT_TRUE(dec.action == RTSFR_ACT_IGNORE, "Expected wait must IGNORE");
    EXPECT_TRUE(dec.reason_code == RTSFR_POL_REASON_LEGITIMATE_WAIT, "Reason LEGITIMATE_WAIT");

    rtsfr_policy_task_state_t st;
    rtsfr_policy_get_task_state(1U, &st);
    EXPECT_TRUE(st.retry_count == 0U, "Legitimate wait must not consume retry budget");

    return true;
}

/* ==========================================================================
 * UT05: Lỗi nghiêm trọng không tin cậy (F09: Stack Overflow / HardFault / Corruption)
 * ========================================================================== */
static bool test_ut05_fatal_untrusted_faults_request_reset(void)
{
    rtsfr_policy_init();
    rtsfr_policy_register_task(1U, 1U, RTSFR_CRIT_LOW);
    rtsfr_policy_register_task(2U, 1U, RTSFR_CRIT_MEDIUM);
    rtsfr_policy_register_task(3U, 1U, RTSFR_CRIT_HIGH);

    rtsfr_fault_event_t ev1 = make_fault(41U, 1U, 1U, 200U, RTSFR_FAULT_STACK_OVERFLOW);
    rtsfr_resource_summary_t res1 = make_clean_res(1U, 1U);
    rtsfr_decision_t d1 = rtsfr_policy_evaluate(&ev1, &res1);
    EXPECT_TRUE(d1.action == RTSFR_ACT_REQUEST_RESET, "Stack overflow -> REQUEST_RESET");
    EXPECT_TRUE(d1.reason_code == RTSFR_POL_REASON_UNTRUSTED_STATE, "Reason UNTRUSTED_STATE");

    rtsfr_fault_event_t ev2 = make_fault(42U, 2U, 1U, 210U, RTSFR_FAULT_HARD_FAULT);
    rtsfr_resource_summary_t res2 = make_clean_res(2U, 1U);
    rtsfr_decision_t d2 = rtsfr_policy_evaluate(&ev2, &res2);
    EXPECT_TRUE(d2.action == RTSFR_ACT_REQUEST_RESET, "HardFault -> REQUEST_RESET");

    rtsfr_fault_event_t ev3 = make_fault(43U, 3U, 1U, 220U, RTSFR_FAULT_CORRUPTION);
    rtsfr_resource_summary_t res3 = make_clean_res(3U, 1U);
    rtsfr_decision_t d3 = rtsfr_policy_evaluate(&ev3, &res3);
    EXPECT_TRUE(d3.action == RTSFR_ACT_REQUEST_RESET, "Corruption -> REQUEST_RESET");

    return true;
}

/* ==========================================================================
 * UT06: Lỗi kẹt Kernel / Scheduler Stall (F08 / TC12)
 * ========================================================================== */
static bool test_ut06_kernel_stall_requests_reset(void)
{
    rtsfr_policy_init();
    rtsfr_policy_register_task(1U, 1U, RTSFR_CRIT_MEDIUM);

    rtsfr_fault_event_t ev = make_fault(50U, 1U, 1U, 300U, RTSFR_FAULT_KERNEL_STALL);
    rtsfr_resource_summary_t res = make_clean_res(1U, 1U);

    rtsfr_decision_t dec = rtsfr_policy_evaluate(&ev, &res);
    EXPECT_TRUE(dec.action == RTSFR_ACT_REQUEST_RESET, "Kernel stall -> REQUEST_RESET");
    EXPECT_TRUE(dec.reason_code == RTSFR_POL_REASON_KERNEL_STALL, "Reason KERNEL_STALL");

    return true;
}

/* ==========================================================================
 * UT07: Tạm hoãn (DEFER) khi Task đang trong tiến trình phục hồi (M2-06)
 * ========================================================================== */
static bool test_ut07_defer_when_recovery_in_progress(void)
{
    rtsfr_policy_init();
    rtsfr_policy_register_task(2U, 1U, RTSFR_CRIT_LOW);

    rtsfr_fault_event_t ev1 = make_fault(60U, 2U, 1U, 500U, RTSFR_FAULT_NO_PROGRESS);
    rtsfr_resource_summary_t res = make_clean_res(2U, 1U);

    rtsfr_decision_t d1 = rtsfr_policy_evaluate(&ev1, &res);
    EXPECT_TRUE(d1.action == RTSFR_ACT_RESTART, "First fault starts recovery (QUIESCING)");

    /* Sự kiện mới gửi tới khi M4 chưa báo kết quả xong */
    rtsfr_fault_event_t ev2 = make_fault(61U, 2U, 1U, 520U, RTSFR_FAULT_NO_PROGRESS);
    rtsfr_decision_t d2 = rtsfr_policy_evaluate(&ev2, &res);
    EXPECT_TRUE(d2.action == RTSFR_ACT_DEFER, "Must DEFER while recovery is in progress");
    EXPECT_TRUE(d2.reason_code == RTSFR_POL_REASON_RECOVERY_IN_PROGRESS, "Reason RECOVERY_IN_PROGRESS");
    EXPECT_TRUE(!rtsfr_policy_should_invoke_recovery(&d2), "DEFER must not call M4 request");

    return true;
}

/* ==========================================================================
 * UT08: Bỏ qua khi Task đã ở trạng thái SAFE_MODE
 * ========================================================================== */
static bool test_ut08_ignore_when_already_in_safe_mode(void)
{
    rtsfr_policy_init();
    rtsfr_policy_register_task(3U, 1U, RTSFR_CRIT_HIGH);
    rtsfr_policy_set_task_state(3U, 1U, RTSFR_TASK_SAFE_MODE);

    rtsfr_fault_event_t ev = make_fault(70U, 3U, 1U, 800U, RTSFR_FAULT_NO_PROGRESS);
    rtsfr_resource_summary_t res = make_clean_res(3U, 1U);

    rtsfr_decision_t dec = rtsfr_policy_evaluate(&ev, &res);
    EXPECT_TRUE(dec.action == RTSFR_ACT_IGNORE, "Already in SAFE_MODE -> IGNORE");
    EXPECT_TRUE(dec.reason_code == RTSFR_POL_REASON_ALREADY_IN_SAFE_MODE, "Reason ALREADY_IN_SAFE_MODE");

    return true;
}

/* ==========================================================================
 * UT09: Phục hồi có chọn lọc khi Task sạch không giữ tài nguyên (F01/F02/F03 - TC02/TC03)
 * ========================================================================== */
static bool test_ut09_clean_task_selective_restart(void)
{
    rtsfr_policy_init();
    rtsfr_policy_register_task(2U, 1U, RTSFR_CRIT_LOW);

    rtsfr_fault_event_t ev = make_fault(80U, 2U, 1U, 1000U, RTSFR_FAULT_BUSY_LOOP);
    rtsfr_resource_summary_t res = make_clean_res(2U, 1U);

    rtsfr_decision_t dec = rtsfr_policy_evaluate(&ev, &res);
    EXPECT_TRUE(dec.action == RTSFR_ACT_RESTART, "Clean busy-loop task -> RESTART");
    EXPECT_TRUE(dec.reason_code == RTSFR_POL_REASON_SAFE_NO_RESOURCE, "Reason SAFE_NO_RESOURCE");
    EXPECT_TRUE(rtsfr_policy_should_invoke_recovery(&dec), "RESTART must invoke M4");

    rtsfr_policy_task_state_t st;
    rtsfr_policy_get_task_state(2U, &st);
    EXPECT_TRUE(st.retry_count == 1U, "retry_count must increment to 1");
    EXPECT_TRUE(st.last_severity == RTSFR_SEV_TRANSIENT, "Severity TRANSIENT");

    return true;
}

/* ==========================================================================
 * UT10: Phục hồi khi Task giữ Managed Buffer thu hồi an toàn (F05 - TC05)
 * ========================================================================== */
static bool test_ut10_reclaimable_managed_buffer_restart(void)
{
    rtsfr_policy_init();
    rtsfr_policy_register_task(2U, 1U, RTSFR_CRIT_LOW);

    rtsfr_fault_event_t ev = make_fault(90U, 2U, 1U, 1500U, RTSFR_FAULT_NO_PROGRESS);
    rtsfr_resource_summary_t res = make_clean_res(2U, 1U);
    res.managed_buffers_owned = 2U;
    res.can_reclaim_all       = true;

    rtsfr_decision_t dec = rtsfr_policy_evaluate(&ev, &res);
    EXPECT_TRUE(dec.action == RTSFR_ACT_RESTART, "Safe managed buffer -> RESTART");
    EXPECT_TRUE(dec.reason_code == RTSFR_POL_REASON_SAFE_BUFFER_RECLAIM, "Reason SAFE_BUFFER_RECLAIM");

    rtsfr_policy_task_state_t st;
    rtsfr_policy_get_task_state(2U, &st);
    EXPECT_TRUE(st.last_severity == RTSFR_SEV_RECOVERABLE_RES, "Severity RECOVERABLE_RES");

    return true;
}

/* ==========================================================================
 * UT11: Chặn Restart khi Task đang giữ FreeRTOS Mutex (F06 - TC06 / Invariant I4)
 * ========================================================================== */
static bool test_ut11_mutex_held_blocks_restart_to_safe_mode(void)
{
    rtsfr_policy_init();
    rtsfr_policy_register_task(3U, 1U, RTSFR_CRIT_HIGH);

    rtsfr_fault_event_t ev = make_fault(100U, 3U, 1U, 2000U, RTSFR_FAULT_NO_PROGRESS);
    rtsfr_resource_summary_t res = make_clean_res(3U, 1U);
    res.mutex_held_count = 1U;

    rtsfr_decision_t dec = rtsfr_policy_evaluate(&ev, &res);
    EXPECT_TRUE(dec.action == RTSFR_ACT_SAFE_MODE, "Mutex held MUST enter SAFE_MODE");
    EXPECT_TRUE(dec.reason_code == RTSFR_POL_REASON_UNSAFE_MUTEX_HELD, "Reason UNSAFE_MUTEX_HELD");

    return true;
}

/* ==========================================================================
 * UT12: Chặn Restart khi có Unknown Resource hoặc lệch Generation của Snapshot
 * ========================================================================== */
static bool test_ut12_unknown_resource_or_gen_mismatch_safe_mode(void)
{
    rtsfr_policy_init();
    rtsfr_policy_register_task(1U, 2U, RTSFR_CRIT_MEDIUM);

    /* Case A: unknown_resource_count > 0 */
    rtsfr_fault_event_t ev1 = make_fault(110U, 1U, 2U, 2500U, RTSFR_FAULT_NO_PROGRESS);
    rtsfr_resource_summary_t res1 = make_clean_res(1U, 2U);
    res1.unknown_resource_count = 1U;

    rtsfr_decision_t d1 = rtsfr_policy_evaluate(&ev1, &res1);
    EXPECT_TRUE(d1.action == RTSFR_ACT_SAFE_MODE, "Unknown resource -> SAFE_MODE");
    EXPECT_TRUE(d1.reason_code == RTSFR_POL_REASON_UNSAFE_UNKNOWN_RES, "Reason UNSAFE_UNKNOWN_RES");

    /* Case B: Snapshot generation mismatch */
    rtsfr_policy_register_task(2U, 2U, RTSFR_CRIT_LOW);
    rtsfr_fault_event_t ev2 = make_fault(111U, 2U, 2U, 2600U, RTSFR_FAULT_NO_PROGRESS);
    rtsfr_resource_summary_t res2 = make_clean_res(2U, 1U); /* Old snapshot gen = 1 */

    rtsfr_decision_t d2 = rtsfr_policy_evaluate(&ev2, &res2);
    EXPECT_TRUE(d2.action == RTSFR_ACT_SAFE_MODE, "Snapshot gen mismatch -> SAFE_MODE");
    EXPECT_TRUE(d2.reason_code == RTSFR_POL_REASON_UNSAFE_UNKNOWN_RES, "Reason UNSAFE_UNKNOWN_RES");

    return true;
}

/* ==========================================================================
 * UT13: Chặn Restart khi đang có I/O hoặc DMA đang bay (has_inflight_io == true)
 * ========================================================================== */
static bool test_ut13_inflight_io_blocks_restart(void)
{
    rtsfr_policy_init();
    rtsfr_policy_register_task(2U, 1U, RTSFR_CRIT_LOW);

    rtsfr_fault_event_t ev = make_fault(120U, 2U, 1U, 3000U, RTSFR_FAULT_NO_PROGRESS);
    rtsfr_resource_summary_t res = make_clean_res(2U, 1U);
    res.has_inflight_io = true;

    rtsfr_decision_t dec = rtsfr_policy_evaluate(&ev, &res);
    EXPECT_TRUE(dec.action == RTSFR_ACT_SAFE_MODE, "In-flight IO -> SAFE_MODE");
    EXPECT_TRUE(dec.reason_code == RTSFR_POL_REASON_UNSAFE_INFLIGHT_IO, "Reason UNSAFE_INFLIGHT_IO");

    return true;
}

/* ==========================================================================
 * UT14: Chặn Restart khi M3 báo can_reclaim_all == false
 * ========================================================================== */
static bool test_ut14_cannot_reclaim_all_blocks_restart(void)
{
    rtsfr_policy_init();
    rtsfr_policy_register_task(2U, 1U, RTSFR_CRIT_LOW);

    rtsfr_fault_event_t ev = make_fault(130U, 2U, 1U, 3500U, RTSFR_FAULT_NO_PROGRESS);
    rtsfr_resource_summary_t res = make_clean_res(2U, 1U);
    res.managed_buffers_owned = 1U;
    res.can_reclaim_all       = false;

    rtsfr_decision_t dec = rtsfr_policy_evaluate(&ev, &res);
    EXPECT_TRUE(dec.action == RTSFR_ACT_SAFE_MODE, "can_reclaim_all == false -> SAFE_MODE");
    EXPECT_TRUE(dec.reason_code == RTSFR_POL_REASON_UNSAFE_CANNOT_RECLAIM, "Reason UNSAFE_CANNOT_RECLAIM");

    return true;
}

/* ==========================================================================
 * UT15: Cạn ngân sách Retry trong cửa sổ 60s với Task LOW/MEDIUM (F07 - TC07 / Invariant I8)
 * ========================================================================== */
static bool test_ut15_retry_budget_exhaustion_low_med_task(void)
{
    rtsfr_policy_init();
    rtsfr_policy_register_task(2U, 1U, RTSFR_CRIT_LOW); /* max_restarts = 2, window = 60000 */

    /* Lần lỗi 1 tại t = 1000 -> RESTART (gen 1 -> 2) */
    rtsfr_fault_event_t ev1 = make_fault(141U, 2U, 1U, 1000U, RTSFR_FAULT_NO_PROGRESS);
    rtsfr_resource_summary_t res1 = make_clean_res(2U, 1U);
    rtsfr_decision_t d1 = rtsfr_policy_evaluate(&ev1, &res1);
    EXPECT_TRUE(d1.action == RTSFR_ACT_RESTART, "1st fault -> RESTART");
    rtsfr_policy_record_result(2U, RTSFR_RECOVERY_OK, 1050U);

    /* Lần lỗi 2 tại t = 20000 -> RESTART (gen 2 -> 3) */
    rtsfr_fault_event_t ev2 = make_fault(142U, 2U, 2U, 20000U, RTSFR_FAULT_NO_PROGRESS);
    rtsfr_resource_summary_t res2 = make_clean_res(2U, 2U);
    rtsfr_decision_t d2 = rtsfr_policy_evaluate(&ev2, &res2);
    EXPECT_TRUE(d2.action == RTSFR_ACT_RESTART, "2nd fault -> RESTART");
    rtsfr_policy_record_result(2U, RTSFR_RECOVERY_OK, 20050U);

    /* Lần lỗi 3 tại t = 40000 (vẫn trong cửa sổ 60000 tính từ t = 1000) -> SAFE_MODE */
    rtsfr_fault_event_t ev3 = make_fault(143U, 2U, 3U, 40000U, RTSFR_FAULT_NO_PROGRESS);
    rtsfr_resource_summary_t res3 = make_clean_res(2U, 3U);
    rtsfr_decision_t d3 = rtsfr_policy_evaluate(&ev3, &res3);
    EXPECT_TRUE(d3.action == RTSFR_ACT_SAFE_MODE, "3rd fault in 60s window -> SAFE_MODE");
    EXPECT_TRUE(d3.reason_code == RTSFR_POL_REASON_RETRY_EXHAUSTED, "Reason RETRY_EXHAUSTED");

    return true;
}

/* ==========================================================================
 * UT16: Cạn ngân sách Retry với Task HIGH Criticality -> REQUEST_RESET
 * ========================================================================== */
static bool test_ut16_retry_budget_exhaustion_high_crit_task(void)
{
    rtsfr_policy_init();
    rtsfr_policy_register_task(3U, 1U, RTSFR_CRIT_HIGH);

    rtsfr_fault_event_t ev1 = make_fault(151U, 3U, 1U, 1000U, RTSFR_FAULT_NO_PROGRESS);
    rtsfr_resource_summary_t res1 = make_clean_res(3U, 1U);
    EXPECT_TRUE(rtsfr_policy_evaluate(&ev1, &res1).action == RTSFR_ACT_RESTART, "1st restart");
    rtsfr_policy_record_result(3U, RTSFR_RECOVERY_OK, 1020U);

    rtsfr_fault_event_t ev2 = make_fault(152U, 3U, 2U, 5000U, RTSFR_FAULT_NO_PROGRESS);
    rtsfr_resource_summary_t res2 = make_clean_res(3U, 2U);
    EXPECT_TRUE(rtsfr_policy_evaluate(&ev2, &res2).action == RTSFR_ACT_RESTART, "2nd restart");
    rtsfr_policy_record_result(3U, RTSFR_RECOVERY_OK, 5020U);

    rtsfr_fault_event_t ev3 = make_fault(153U, 3U, 3U, 10000U, RTSFR_FAULT_NO_PROGRESS);
    rtsfr_resource_summary_t res3 = make_clean_res(3U, 3U);
    rtsfr_decision_t d3 = rtsfr_policy_evaluate(&ev3, &res3);
    EXPECT_TRUE(d3.action == RTSFR_ACT_REQUEST_RESET, "HIGH crit exhausted -> REQUEST_RESET");
    EXPECT_TRUE(d3.reason_code == RTSFR_POL_REASON_HIGH_CRIT_EXHAUSTED, "Reason HIGH_CRIT_EXHAUSTED");

    return true;
}

/* ==========================================================================
 * UT17: Làm mới cửa sổ Retry khi qua 60s & Kiểm tra tràn số Tick Wrap-around
 * ========================================================================== */
static bool test_ut17_retry_window_reset_and_tick_wraparound(void)
{
    rtsfr_policy_init();
    rtsfr_policy_register_task(1U, 1U, RTSFR_CRIT_MEDIUM);

    /* Bắt đầu sát biên tràn 32-bit: 0xFFFF0000U */
    TickType_t t_start = 0xFFFF0000U;

    rtsfr_fault_event_t ev1 = make_fault(161U, 1U, 1U, t_start, RTSFR_FAULT_NO_PROGRESS);
    rtsfr_resource_summary_t res1 = make_clean_res(1U, 1U);
    EXPECT_TRUE(rtsfr_policy_evaluate(&ev1, &res1).action == RTSFR_ACT_RESTART, "Restart 1");
    rtsfr_policy_record_result(1U, RTSFR_RECOVERY_OK, t_start + 50U);

    rtsfr_fault_event_t ev2 = make_fault(162U, 1U, 2U, t_start + 10000U, RTSFR_FAULT_NO_PROGRESS);
    rtsfr_resource_summary_t res2 = make_clean_res(1U, 2U);
    EXPECT_TRUE(rtsfr_policy_evaluate(&ev2, &res2).action == RTSFR_ACT_RESTART, "Restart 2");
    rtsfr_policy_record_result(1U, RTSFR_RECOVERY_OK, t_start + 10050U);

    /*
     * Sau 70000 ticks (> 60000 window), bộ đếm TickType_t đã tràn qua 0!
     * Phép trừ không dấu vẫn tính đúng elapsed = 70000 >= 60000 -> Reset retry_count về 0!
     */
    TickType_t t_after_wrap = (TickType_t)(t_start + 70000U);
    rtsfr_fault_event_t ev3 = make_fault(163U, 1U, 3U, t_after_wrap, RTSFR_FAULT_NO_PROGRESS);
    rtsfr_resource_summary_t res3 = make_clean_res(1U, 3U);
    rtsfr_decision_t d3 = rtsfr_policy_evaluate(&ev3, &res3);
    EXPECT_TRUE(d3.action == RTSFR_ACT_RESTART, "After window expiry across wrap-around -> RESTART allowed");

    rtsfr_policy_task_state_t st;
    rtsfr_policy_get_task_state(1U, &st);
    EXPECT_TRUE(st.retry_count == 1U, "New window retry_count must be 1");

    return true;
}

/* ==========================================================================
 * UT18: Phối hợp vòng kín với rtsfr_recovery.c của Tú (Callback on_recovery_result)
 * ========================================================================== */
static bool test_ut18_m4_recovery_result_callback_integration(void)
{
    rtsfr_policy_init();
    rtsfr_policy_register_task(2U, 1U, RTSFR_CRIT_LOW);

    rtsfr_fault_event_t ev1 = make_fault(171U, 2U, 1U, 1000U, RTSFR_FAULT_NO_PROGRESS);
    rtsfr_resource_summary_t res1 = make_clean_res(2U, 1U);
    EXPECT_TRUE(rtsfr_policy_evaluate(&ev1, &res1).action == RTSFR_ACT_RESTART, "Start recovery");

    /* Case A: M4 báo hủy do báo động giả (RTSFR_REASON_FALSE_ALARM_ROLLBACK) -> Hoàn lại retry_count */
    rtsfr_recovery_result_t m4_res;
    memset(&m4_res, 0, sizeof(m4_res));
    m4_res.event_id       = 171U;
    m4_res.task_id        = 2U;
    m4_res.old_generation = 1U;
    m4_res.new_generation = 0U;
    m4_res.outcome        = RTSFR_RECOVERY_REJECTED;
    m4_res.reason_code    = (uint16_t)RTSFR_REASON_FALSE_ALARM_ROLLBACK;
    m4_res.finished_tick  = 1010U;

    rtsfr_policy_on_recovery_result(&m4_res);

    rtsfr_policy_task_state_t st;
    rtsfr_policy_get_task_state(2U, &st);
    EXPECT_TRUE(st.retry_count == 0U, "False alarm rollback must refund retry_count to 0");
    EXPECT_TRUE(st.state == RTSFR_TASK_HEALTHY, "State restored to HEALTHY");
    EXPECT_TRUE(st.active_generation == 1U, "Generation remains 1");

    /* Case B: M4 báo RTSFR_RECOVERY_OK với new_generation = 2 */
    rtsfr_fault_event_t ev2 = make_fault(172U, 2U, 1U, 2000U, RTSFR_FAULT_NO_PROGRESS);
    EXPECT_TRUE(rtsfr_policy_evaluate(&ev2, &res1).action == RTSFR_ACT_RESTART, "Start recovery 2");

    m4_res.event_id       = 172U;
    m4_res.old_generation = 1U;
    m4_res.new_generation = 2U;
    m4_res.outcome        = RTSFR_RECOVERY_OK;
    m4_res.reason_code    = (uint16_t)RTSFR_REASON_FIRST_PROGRESS;
    rtsfr_policy_on_recovery_result(&m4_res);

    rtsfr_policy_get_task_state(2U, &st);
    EXPECT_TRUE(st.active_generation == 2U, "Generation updated to 2");
    EXPECT_TRUE(st.state == RTSFR_TASK_HEALTHY, "State HEALTHY");

    /* Case C: M4 báo RTSFR_RECOVERY_REJECTED do RTSFR_REASON_SELF_RECOVERY_FORBIDDEN -> SAFE_MODE */
    rtsfr_fault_event_t ev3 = make_fault(173U, 2U, 2U, 3000U, RTSFR_FAULT_NO_PROGRESS);
    rtsfr_resource_summary_t res2 = make_clean_res(2U, 2U);
    EXPECT_TRUE(rtsfr_policy_evaluate(&ev3, &res2).action == RTSFR_ACT_RESTART, "Start recovery 3");

    m4_res.event_id       = 173U;
    m4_res.old_generation = 2U;
    m4_res.new_generation = 0U;
    m4_res.outcome        = RTSFR_RECOVERY_REJECTED;
    m4_res.reason_code    = (uint16_t)RTSFR_REASON_SELF_RECOVERY_FORBIDDEN;
    rtsfr_policy_on_recovery_result(&m4_res);

    rtsfr_policy_get_task_state(2U, &st);
    EXPECT_TRUE(st.state == RTSFR_TASK_SAFE_MODE, "Self recovery forbidden must latch SAFE_MODE");

    return true;
}

/* ==========================================================================
 * UT19 (M2-08): Phân tích độ nhạy tham số (Sensitivity Analysis)
 * Chạy chuỗi 6 sự kiện (gồm 1 chờ hợp lệ + 5 lỗi cách nhau 15 giây)
 * trên các cấu hình (max_restarts, retry_window_ms) khác nhau.
 * ========================================================================== */
static void run_m2_08_sensitivity_analysis(void)
{
    const uint8_t max_restarts_list[]    = {1U, 2U, 3U};
    const TickType_t window_ticks_list[] = {10000U, 30000U, 60000U};
    const TickType_t fault_ticks[]       = {1000U, 16000U, 31000U, 46000U, 61000U};

    printf("\n==============================================================================\n");
    printf(" M2-08: BANG PHAN TICH DO NHAY THAM SO RETRY POLICY (SENSITIVITY ANALYSIS)\n");
    printf(" Workload: 1 Legitimate Wait + 5 Transient Faults cach nhau 15.000 ms (15s)\n");
    printf("==============================================================================\n");
    printf("| max_restarts | retry_window_ms | False Alarm | Restart Count | SafeMode @ Fault# |\n");
    printf("|--------------|-----------------|-------------|---------------|-------------------|\n");

    for (size_t i = 0U; i < 3U; i++)
    {
        for (size_t j = 0U; j < 3U; j++)
        {
            uint8_t max_r        = max_restarts_list[i];
            TickType_t win_ticks = window_ticks_list[j];
            uint32_t restarts    = 0U;
            uint32_t false_alarm = 0U;
            uint32_t safe_at     = 0U;
            uint16_t gen         = 1U;

            rtsfr_policy_init();
            rtsfr_policy_register_task(2U, gen, RTSFR_CRIT_LOW);
            rtsfr_policy_set_retry_policy(2U, max_r, win_ticks);

            /* Bước 0: Gửi 1 sự kiện chờ hợp lệ (Expected Wait) */
            rtsfr_fault_event_t wait_ev = make_fault(1U, 2U, gen, 500U, RTSFR_FAULT_EXPECTED_WAIT);
            rtsfr_resource_summary_t wait_res = make_clean_res(2U, gen);
            if (rtsfr_policy_evaluate(&wait_ev, &wait_res).action != RTSFR_ACT_IGNORE)
            {
                false_alarm++;
            }

            /* Bước 1..5: Gửi 5 lỗi cách nhau 15 giây */
            for (size_t k = 0U; k < 5U; k++)
            {
                rtsfr_fault_event_t ev = make_fault((uint32_t)(k + 2U), 2U, gen,
                                                    fault_ticks[k], RTSFR_FAULT_NO_PROGRESS);
                rtsfr_resource_summary_t res = make_clean_res(2U, gen);
                rtsfr_decision_t dec = rtsfr_policy_evaluate(&ev, &res);

                if (dec.action == RTSFR_ACT_RESTART)
                {
                    restarts++;
                    rtsfr_policy_record_result(2U, RTSFR_RECOVERY_OK, fault_ticks[k] + 50U);
                    gen++;
                }
                else if ((dec.action == RTSFR_ACT_SAFE_MODE) && (safe_at == 0U))
                {
                    safe_at = (uint32_t)(k + 1U);
                }
            }

            if (safe_at == 0U)
            {
                printf("| %-12u | %-15u | %-11u | %-13u | %-17s |\n",
                       max_r, (unsigned)win_ticks, false_alarm, restarts, "Khong bi chan");
            }
            else
            {
                char buf[20];
                snprintf(buf, sizeof(buf), "Loi thu %u", (unsigned)safe_at);
                printf("| %-12u | %-15u | %-11u | %-13u | %-17s |\n",
                       max_r, (unsigned)win_ticks, false_alarm, restarts, buf);
            }
        }
    }
    printf("==============================================================================\n");
}

typedef bool (*unit_test_fn)(void);

typedef struct
{
    const char  *name;
    unit_test_fn fn;
} test_case_t;

int main(void)
{
    const test_case_t tests[] = {
        {"UT01: Invalid inputs, NULL, event_id=0 & task_id boundaries", test_ut01_invalid_inputs_and_boundaries},
        {"UT02: Stale generation event rejected (TC10 / Invariant I2)", test_ut02_stale_generation_rejected},
        {"UT03: Duplicate event_id rejected (Anti-double recovery)",    test_ut03_duplicate_event_rejected},
        {"UT04: Legitimate expected wait ignored (F04/F10 / TC04)",     test_ut04_legitimate_wait_ignored},
        {"UT05: Fatal untrusted faults request reset (F09)",            test_ut05_fatal_untrusted_faults_request_reset},
        {"UT06: Kernel stall fault requests reset (F08 / TC12)",        test_ut06_kernel_stall_requests_reset},
        {"UT07: Defer when recovery already in progress (M2-06)",       test_ut07_defer_when_recovery_in_progress},
        {"UT08: Ignore when task is already in SAFE_MODE",              test_ut08_ignore_when_already_in_safe_mode},
        {"UT09: Clean transient fault selective restart (F01/F02/TC02)",test_ut09_clean_task_selective_restart},
        {"UT10: Reclaimable managed buffer restart (F05 / TC05)",       test_ut10_reclaimable_managed_buffer_restart},
        {"UT11: Mutex held blocks restart -> SAFE_MODE (F06 / TC06)",   test_ut11_mutex_held_blocks_restart_to_safe_mode},
        {"UT12: Unknown resource or snapshot gen mismatch -> SAFE_MODE",test_ut12_unknown_resource_or_gen_mismatch_safe_mode},
        {"UT13: In-flight I/O blocks restart -> SAFE_MODE",             test_ut13_inflight_io_blocks_restart},
        {"UT14: can_reclaim_all == false blocks restart -> SAFE_MODE",  test_ut14_cannot_reclaim_all_blocks_restart},
        {"UT15: Retry budget exhausted (LOW/MED crit) -> SAFE_MODE",    test_ut15_retry_budget_exhaustion_low_med_task},
        {"UT16: Retry budget exhausted (HIGH crit) -> REQUEST_RESET",   test_ut16_retry_budget_exhaustion_high_crit_task},
        {"UT17: Retry window expiry & 32-bit Tick wrap-around safety",  test_ut17_retry_window_reset_and_tick_wraparound},
        {"UT18: Closed-loop callback integration with M4 recovery",     test_ut18_m4_recovery_result_callback_integration}
    };

    printf("==============================================================================\n");
    printf(" RTSFR MODULE 2 (FAULT CLASSIFICATION & POLICY) - HOST UNIT TEST SUITE\n");
    printf("==============================================================================\n");

    for (size_t i = 0U; i < sizeof(tests) / sizeof(tests[0]); i++)
    {
        g_tests_run++;
        if (tests[i].fn())
        {
            g_tests_passed++;
            printf("[PASS] %s\n", tests[i].name);
        }
        else
        {
            g_tests_failed++;
            printf("[FAIL] %s\n", tests[i].name);
        }
    }

    printf("------------------------------------------------------------------------------\n");
    printf(" TOTAL: %u | PASSED: %u | FAILED: %u\n",
           (unsigned)g_tests_run, (unsigned)g_tests_passed, (unsigned)g_tests_failed);
    printf("==============================================================================\n");

    run_m2_08_sensitivity_analysis();

    return (g_tests_failed == 0U) ? 0 : 1;
}
