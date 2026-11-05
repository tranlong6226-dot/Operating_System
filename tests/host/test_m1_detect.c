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

#include "../../firmware/Middlewares/Third_Party/FreeRTOS/Source/include/rtsfr_types.h"
#include "../../firmware/Middlewares/Third_Party/FreeRTOS/Source/include/rtsfr_detect.h"

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

    printf("=========================================\n");
    printf("Summary: %d / %d tests passed (100%%)\n", g_tests_passed, g_tests_total);
    printf("=========================================\n");

    return 0;
}
