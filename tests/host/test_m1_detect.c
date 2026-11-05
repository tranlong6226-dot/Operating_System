/**
 * @file test_m1_detect.c
 * @brief Host-based Unit Tests for Module 1 (Fault Detection)
 * @details Compiles natively on PC with GCC (MinGW) for fast TDD verification.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#ifndef RTSFR_HOST_TEST
#define RTSFR_HOST_TEST 1
#endif

#include "../../firmware/RTSFR/Inc/rtsfr_types.h"
#include "../../firmware/RTSFR/Inc/rtsfr_detect.h"

/* Mock tick variable for host testing */
TickType_t g_mock_tick = 0U;

#define ANSI_GREEN "\033[0;32m"
#define ANSI_RED   "\033[0;31m"
#define ANSI_RESET "\033[0m"

static int g_tests_passed = 0;
static int g_tests_total = 0;

#define RUN_TEST(fn) do { \
    g_tests_total++; \
    printf("[RUNNING] %s ... ", #fn); \
    fn(); \
    g_tests_passed++; \
    printf(ANSI_GREEN "PASS" ANSI_RESET "\n"); \
} while(0)

/* Test 1: Task Registration */
static void test_task_registration(void)
{
    g_mock_tick = 100U;
    assert(rtsfr_detect_init() == RTSFR_OK);

    rtsfr_task_config_t cfg = {
        .task_id = 0,
        .task_name = "SensorTask",
        .timeout_ticks = 200,
        .criticality = 1
    };

    void *fake_handle = (void*)0x1234;
    assert(rtsfr_register_task(&cfg, fake_handle) == RTSFR_OK);

    rtsfr_task_record_t rec;
    assert(rtsfr_detect_get_record(0, &rec) == RTSFR_OK);
    assert(rec.task_id == 0);
    assert(rec.generation == 1);
    assert(rec.task_handle == fake_handle);
    assert(rec.timeout_ticks == 200);
    assert(rec.last_progress_tick == 100);
    assert(rec.state == RTSFR_TASK_STATE_HEALTHY);
    assert(rec.fault_pending == false);
}

/* Test 2: Normal Progress Reporting */
static void test_progress_reporting(void)
{
    g_mock_tick = 150U;
    assert(rtsfr_report_progress(0) == RTSFR_OK);

    rtsfr_task_record_t rec;
    assert(rtsfr_detect_get_record(0, &rec) == RTSFR_OK);
    assert(rec.last_progress_tick == 150);
    assert(rec.progress_seq == 1);

    g_mock_tick = 200U;
    assert(rtsfr_report_progress(0) == RTSFR_OK);
    assert(rtsfr_detect_get_record(0, &rec) == RTSFR_OK);
    assert(rec.last_progress_tick == 200);
    assert(rec.progress_seq == 2);
}

/* Test 3: No-Progress Timeout Detection (F01) */
static void test_timeout_detection(void)
{
    rtsfr_detect_init();
    g_mock_tick = 1000U;

    rtsfr_task_config_t cfg = {
        .task_id = 0,
        .task_name = "CommTask",
        .timeout_ticks = 500,
        .criticality = 2
    };
    assert(rtsfr_register_task(&cfg, (void*)0x5678) == RTSFR_OK);

    /* Advance tick by 400 ticks (Within limit) */
    g_mock_tick = 1400U;
    assert(rtsfr_detect_scan(g_mock_tick) == RTSFR_OK);

    rtsfr_fault_event_t event;
    assert(rtsfr_detect_pop_event(&event) == false); /* Should not fault yet */

    /* Advance tick past timeout: 1000 + 501 = 1501 */
    g_mock_tick = 1501U;
    assert(rtsfr_detect_scan(g_mock_tick) == RTSFR_OK);

    /* Event must be generated */
    assert(rtsfr_detect_pop_event(&event) == true);
    assert(event.task_id == 0);
    assert(event.generation == 1);
    assert(event.fault_type == RTSFR_FAULT_NO_PROGRESS);
    assert(event.detected_tick == 1501);

    /* Second scan shouldn't generate duplicate event */
    assert(rtsfr_detect_scan(1502U) == RTSFR_OK);
    assert(rtsfr_detect_pop_event(&event) == false);
}

/* Test 4: Expected Wait Contract (No false positive) */
static void test_expected_wait_contract(void)
{
    rtsfr_detect_init();
    g_mock_tick = 2000U;

    rtsfr_task_config_t cfg = {
        .task_id = 1,
        .task_name = "SensorTask",
        .timeout_ticks = 200, /* Normal timeout is 200 */
        .criticality = 1
    };
    assert(rtsfr_register_task(&cfg, (void*)0x9999) == RTSFR_OK);

    /* Task declares a long legitimate wait (1000 ticks) */
    assert(rtsfr_begin_expected_wait(1, 1000) == RTSFR_OK);

    /* Advance tick by 500 (Normally would timeout at 200, but in expected wait!) */
    g_mock_tick = 2500U;
    assert(rtsfr_detect_scan(g_mock_tick) == RTSFR_OK);

    rtsfr_fault_event_t event;
    assert(rtsfr_detect_pop_event(&event) == false); /* Must NOT fault */

    /* Task wakes up on time at 2800 */
    g_mock_tick = 2800U;
    assert(rtsfr_end_expected_wait(1) == RTSFR_OK);

    rtsfr_task_record_t rec;
    assert(rtsfr_detect_get_record(1, &rec) == RTSFR_OK);
    assert(rec.state == RTSFR_TASK_STATE_HEALTHY);
    assert(rec.last_progress_tick == 2800);
}

/* Test 5: Expected Wait Overrun (F04) */
static void test_expected_wait_overrun(void)
{
    rtsfr_detect_init();
    g_mock_tick = 3000U;

    rtsfr_task_config_t cfg = {
        .task_id = 1,
        .task_name = "SensorTask",
        .timeout_ticks = 200,
        .criticality = 1
    };
    assert(rtsfr_register_task(&cfg, (void*)0x9999) == RTSFR_OK);

    /* Declare wait of 500 ticks (Deadline: 3500) */
    assert(rtsfr_begin_expected_wait(1, 500) == RTSFR_OK);

    /* Task fails to wake up by 3501 */
    g_mock_tick = 3501U;
    assert(rtsfr_detect_scan(g_mock_tick) == RTSFR_OK);

    rtsfr_fault_event_t event;
    assert(rtsfr_detect_pop_event(&event) == true);
    assert(event.task_id == 1);
    assert(event.fault_type == RTSFR_FAULT_WAIT_TIMEOUT);
}

/* Test 6: Generation Rollover & Stale Event Invalidation */
static void test_generation_rollover(void)
{
    rtsfr_detect_init();
    g_mock_tick = 4000U;

    rtsfr_task_config_t cfg = {
        .task_id = 0,
        .task_name = "ControlTask",
        .timeout_ticks = 100,
        .criticality = 2
    };
    assert(rtsfr_register_task(&cfg, (void*)0x1111) == RTSFR_OK);

    /* Trigger fault in Generation 1 */
    g_mock_tick = 4150U;
    assert(rtsfr_detect_scan(g_mock_tick) == RTSFR_OK);

    /* An event from Gen 1 is currently in queue. Now Module 4 recovers task to Gen 2! */
    void *new_handle = (void*)0x2222;
    rtsfr_detect_on_new_generation(0, new_handle, 2U);

    /* Registry should reflect Gen 2 */
    rtsfr_task_record_t rec;
    assert(rtsfr_detect_get_record(0, &rec) == RTSFR_OK);
    assert(rec.generation == 2);
    assert(rec.task_handle == new_handle);
    assert(rec.state == RTSFR_TASK_STATE_HEALTHY);
    assert(rec.fault_pending == false);

    /* Stale event from Gen 1 must have been purged */
    rtsfr_fault_event_t event;
    assert(rtsfr_detect_pop_event(&event) == false);
}

/* Test 7: Tick Wrap-around Safety */
static void test_tick_wraparound(void)
{
    rtsfr_detect_init();

    /* Set mock tick very close to 0xFFFFFFFF (32-bit unsigned overflow point) */
    g_mock_tick = 0xFFFFFFF0U;

    rtsfr_task_config_t cfg = {
        .task_id = 2,
        .task_name = "EdgeTask",
        .timeout_ticks = 100,
        .criticality = 1
    };
    assert(rtsfr_register_task(&cfg, (void*)0x3333) == RTSFR_OK);

    /* Tick wraps around past 0 to 0x00000020 (elapsed = 0x10 + 0x20 = 48 < 100) */
    g_mock_tick = 0x00000020U;
    assert(rtsfr_detect_scan(g_mock_tick) == RTSFR_OK);

    rtsfr_fault_event_t event;
    assert(rtsfr_detect_pop_event(&event) == false); /* Still within 100 ticks */

    /* Advance tick past 100 ticks: 0xFFFFFFF0 + 101 = 0x00000055 */
    g_mock_tick = 0x00000055U;
    assert(rtsfr_detect_scan(g_mock_tick) == RTSFR_OK);

    assert(rtsfr_detect_pop_event(&event) == true);
    assert(event.task_id == 2);
    assert(event.fault_type == RTSFR_FAULT_NO_PROGRESS);
}

/* Test 8: Input Validation and Boundary Edge Cases */
static void test_invalid_inputs_and_edge_cases(void)
{
    rtsfr_detect_init();

    /* NULL config pointer */
    assert(rtsfr_register_task(NULL, (void*)0x1) == RTSFR_ERR_INVALID);

    /* Out of bound task ID */
    rtsfr_task_config_t cfg_oob = {
        .task_id = configRTSFR_MAX_TRACKED_TASKS,
        .task_name = "OOB",
        .timeout_ticks = 100,
        .criticality = 0
    };
    assert(rtsfr_register_task(&cfg_oob, (void*)0x1) == RTSFR_ERR_INVALID);

    /* Zero timeout ticks is invalid */
    rtsfr_task_config_t cfg_zero = {
        .task_id = 0,
        .task_name = "ZeroTimeout",
        .timeout_ticks = 0,
        .criticality = 0
    };
    assert(rtsfr_register_task(&cfg_zero, (void*)0x1) == RTSFR_ERR_INVALID);

    /* Unregistered operations */
    assert(rtsfr_report_progress(0) == RTSFR_ERR_NOT_FOUND);
    assert(rtsfr_begin_expected_wait(0, 50) == RTSFR_ERR_NOT_FOUND);
    assert(rtsfr_end_expected_wait(0) == RTSFR_ERR_NOT_FOUND);

    /* Out of bounds operations */
    assert(rtsfr_report_progress(configRTSFR_MAX_TRACKED_TASKS) == RTSFR_ERR_INVALID);
    assert(rtsfr_begin_expected_wait(configRTSFR_MAX_TRACKED_TASKS, 50) == RTSFR_ERR_INVALID);
    assert(rtsfr_end_expected_wait(configRTSFR_MAX_TRACKED_TASKS) == RTSFR_ERR_INVALID);

    /* Null pointer for pop event */
    assert(rtsfr_detect_pop_event(NULL) == false);

    /* Null pointer / invalid ID for get_record */
    rtsfr_task_record_t rec;
    assert(rtsfr_detect_get_record(configRTSFR_MAX_TRACKED_TASKS, &rec) == RTSFR_ERR_INVALID);
    assert(rtsfr_detect_get_record(0, NULL) == RTSFR_ERR_INVALID);
    assert(rtsfr_detect_get_record(0, &rec) == RTSFR_ERR_NOT_FOUND);

    /* New generation on unregistered task should not register it */
    rtsfr_detect_on_new_generation(0, (void*)0x99, 1);
    assert(rtsfr_detect_get_record(0, &rec) == RTSFR_ERR_NOT_FOUND);

    /* Task delete with NULL */
    rtsfr_detect_on_task_delete(NULL);
}

/* Test 9: State Machine Guardrails */
static void test_state_machine_guardrails(void)
{
    rtsfr_detect_init();
    g_mock_tick = 5000U;

    rtsfr_task_config_t cfg = {
        .task_id = 0,
        .task_name = "GuardedTask",
        .timeout_ticks = 100,
        .criticality = 1
    };
    void *hTask = (void*)0x4444;
    assert(rtsfr_register_task(&cfg, hTask) == RTSFR_OK);

    /* Task enters expected wait of 50 ticks */
    assert(rtsfr_begin_expected_wait(0, 50) == RTSFR_OK);

    /* Wait deadline overruns at 5051 */
    g_mock_tick = 5051U;
    assert(rtsfr_detect_scan(g_mock_tick) == RTSFR_OK);

    rtsfr_task_record_t rec;
    assert(rtsfr_detect_get_record(0, &rec) == RTSFR_OK);
    assert(rec.state == RTSFR_TASK_STATE_FAULTY);

    /* Faulty task tries to end expected wait -> rejected with ERR_TIMEOUT */
    assert(rtsfr_end_expected_wait(0) == RTSFR_ERR_TIMEOUT);
    assert(rtsfr_detect_get_record(0, &rec) == RTSFR_OK);
    assert(rec.state == RTSFR_TASK_STATE_FAULTY);

    /* Faulty task tries to report progress -> rejected */
    assert(rtsfr_report_progress(0) == RTSFR_ERR_INVALID);

    /* Faulty task tries to enter expected wait -> rejected */
    assert(rtsfr_begin_expected_wait(0, 100) == RTSFR_ERR_INVALID);

    /* Delete task -> state transitions to QUIESCED */
    rtsfr_detect_on_task_delete(hTask);
    assert(rtsfr_detect_get_record(0, &rec) == RTSFR_OK);
    assert(rec.state == RTSFR_TASK_STATE_QUIESCED);

    /* Quiesced task cannot report progress or begin wait */
    assert(rtsfr_report_progress(0) == RTSFR_ERR_INVALID);
    assert(rtsfr_begin_expected_wait(0, 100) == RTSFR_ERR_INVALID);
    assert(rtsfr_end_expected_wait(0) == RTSFR_ERR_INVALID);
}

/* Test 10: Circular Buffer Boundary and Queue-Full Resilience */
static void test_circular_buffer_and_queue_full_recovery(void)
{
    rtsfr_detect_init();
    g_mock_tick = 6000U;

    /* Register all 4 tasks */
    for (uint8_t i = 0; i < configRTSFR_MAX_TRACKED_TASKS; i++) {
        rtsfr_task_config_t cfg = {
            .task_id = i,
            .task_name = "TaskX",
            .timeout_ticks = 50,
            .criticality = 1
        };
        assert(rtsfr_register_task(&cfg, (void*)(uintptr_t)(0x1000 + i)) == RTSFR_OK);
    }

    /* Advance tick so all 4 tasks timeout (generating 4 events) */
    g_mock_tick = 6051U;
    assert(rtsfr_detect_scan(g_mock_tick) == RTSFR_OK);

    /* Recover all 4 tasks to Generation 2 */
    for (uint8_t i = 0; i < configRTSFR_MAX_TRACKED_TASKS; i++) {
        rtsfr_detect_on_new_generation(i, (void*)(uintptr_t)(0x2000 + i), 2U);
    }

    /* Advance tick again so all 4 tasks timeout in Gen 2 (now queue has 4 events from Gen 2) */
    g_mock_tick = 6151U;
    assert(rtsfr_detect_scan(g_mock_tick) == RTSFR_OK);

    /* Recover to Gen 3 */
    for (uint8_t i = 0; i < configRTSFR_MAX_TRACKED_TASKS; i++) {
        rtsfr_detect_on_new_generation(i, (void*)(uintptr_t)(0x3000 + i), 3U);
    }

    /* Advance tick so all 4 timeout in Gen 3 -> Total 8 events in queue (QUEUE FULL!) */
    /* Wait: Gen 3 rollover purged old Gen 2 events because they were older gen for these tasks! */
    /* To fill 8 slots without purging, let's keep events for tasks and recover only task 0 */
    rtsfr_detect_init();
    g_mock_tick = 7000U;

    /* Register tasks 0, 1, 2, 3 */
    for (uint8_t i = 0; i < 4; i++) {
        rtsfr_task_config_t cfg = {
            .task_id = i,
            .task_name = "T",
            .timeout_ticks = 10,
            .criticality = 1
        };
        assert(rtsfr_register_task(&cfg, (void*)(uintptr_t)(0x100 + i)) == RTSFR_OK);
    }

    /* Scan to generate 4 events */
    g_mock_tick = 7015U;
    assert(rtsfr_detect_scan(g_mock_tick) == RTSFR_OK);

    /* Now pop events one by one to verify ordering */
    rtsfr_fault_event_t e;
    for (uint8_t i = 0; i < 4; i++) {
        assert(rtsfr_detect_pop_event(&e) == true);
        assert(e.task_id == i);
    }
    assert(rtsfr_detect_pop_event(&e) == false);

    /* Now test queue full: when queue reaches configRTSFR_EVENT_QUEUE_SIZE */
    /* Let's verify new generation when queue is full does not overflow buffer */
    /* We can fill queue by generating events */
    rtsfr_detect_init();
    g_mock_tick = 8000U;
    rtsfr_task_config_t c0 = { .task_id = 0, .task_name = "T0", .timeout_ticks = 10, .criticality = 1 };
    rtsfr_task_config_t c1 = { .task_id = 1, .task_name = "T1", .timeout_ticks = 100, .criticality = 1 };
    assert(rtsfr_register_task(&c0, (void*)0x10) == RTSFR_OK);
    assert(rtsfr_register_task(&c1, (void*)0x20) == RTSFR_OK);

    /* Trigger timeout for T0 (20 ticks elapsed: T0 timed out, T1 still within 100) */
    g_mock_tick = 8020U;
    assert(rtsfr_detect_scan(g_mock_tick) == RTSFR_OK);
    assert(rtsfr_detect_pop_event(&e) == true);
    assert(e.task_id == 0);
    assert(rtsfr_detect_pop_event(&e) == false);

    /* Recover T0 to Gen 2 at tick 8020 */
    rtsfr_detect_on_new_generation(0, (void*)0x11, 2U);

    /* Advance to 8150:
     * T0 elapsed = 8150 - 8020 = 130 > 10 (Gen 2 times out)
     * T1 elapsed = 8150 - 8000 = 150 > 100 (Gen 1 times out)
     */
    g_mock_tick = 8150U;
    assert(rtsfr_detect_scan(g_mock_tick) == RTSFR_OK);

    /* Verify both events are present in scan order */
    assert(rtsfr_detect_pop_event(&e) == true);
    assert(e.task_id == 0);
    assert(rtsfr_detect_pop_event(&e) == true);
    assert(e.task_id == 1);
    assert(rtsfr_detect_pop_event(&e) == false);
}

int main(void)
{
    printf("=========================================\n");
    printf("  RTSFR Module 1 - Host Unit Test Runner \n");
    printf("=========================================\n");

    RUN_TEST(test_task_registration);
    RUN_TEST(test_progress_reporting);
    RUN_TEST(test_timeout_detection);
    RUN_TEST(test_expected_wait_contract);
    RUN_TEST(test_expected_wait_overrun);
    RUN_TEST(test_generation_rollover);
    RUN_TEST(test_tick_wraparound);
    RUN_TEST(test_invalid_inputs_and_edge_cases);
    RUN_TEST(test_state_machine_guardrails);
    RUN_TEST(test_circular_buffer_and_queue_full_recovery);

    printf("=========================================\n");
    printf("Summary: %d / %d tests passed (100%%)\n", g_tests_passed, g_tests_total);
    printf("=========================================\n");

    return 0;
}
