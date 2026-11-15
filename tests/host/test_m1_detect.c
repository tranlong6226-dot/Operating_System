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

#include "FreeRTOS.h"
#include "task.h"
#include "rtsfr_types.h"
#include "rtsfr_detect.h"

/* Mock tick variable for host testing */
static TickType_t g_mock_tick = 0U;

TickType_t xTaskGetTickCount(void)
{
    return g_mock_tick;
}

static TaskHandle_t s_mock_current_handle = NULL;

TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    return s_mock_current_handle;
}

void rtsfr_resource_on_new_generation(uint8_t task_id, uint16_t generation)
{
    (void)task_id;
    (void)generation;
}

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
        .task_id = 1,
        .task_name = "SensorTask",
        .timeout_ticks = 200,
        .criticality = RTSFR_CRIT_MEDIUM
    };

    void *fake_handle = (void *)0x1234;
    s_mock_current_handle = fake_handle;
    assert(rtsfr_register_task(&cfg, fake_handle) == RTSFR_OK);

    rtsfr_task_record_t rec;
    assert(rtsfr_detect_get_record(1, &rec) == RTSFR_OK);
    assert(rec.task_id == 1);
    assert(rec.generation == 1);
    assert(rec.task_handle == fake_handle);
    assert(rec.timeout_ticks == 200);
    assert(rec.last_progress_tick == 100);
    assert(rec.state == RTSFR_TASK_HEALTHY);
    assert(rec.fault_pending == false);
}

/* Test 2: Normal Progress Reporting */
static void test_progress_reporting(void)
{
    void *fake_handle = (void *)0x1234;
    s_mock_current_handle = fake_handle;

    g_mock_tick = 150U;
    assert(rtsfr_report_progress(1) == RTSFR_OK);

    rtsfr_task_record_t rec;
    assert(rtsfr_detect_get_record(1, &rec) == RTSFR_OK);
    assert(rec.last_progress_tick == 150);
    assert(rec.progress_seq == 1);

    /* Caller verification check: unauthorized caller must fail */
    s_mock_current_handle = (void *)0x9999;
    assert(rtsfr_report_progress(1) == RTSFR_ERR_STALE);

    /* Restore authorized caller */
    s_mock_current_handle = fake_handle;
    g_mock_tick = 200U;
    assert(rtsfr_report_progress(1) == RTSFR_OK);
    assert(rtsfr_detect_get_record(1, &rec) == RTSFR_OK);
    assert(rec.last_progress_tick == 200);
    assert(rec.progress_seq == 2);
}

/* Test 3: No-Progress Timeout Detection (F01) */
static void test_timeout_detection(void)
{
    assert(rtsfr_detect_init() == RTSFR_OK);
    g_mock_tick = 1000U;

    rtsfr_task_config_t cfg = {
        .task_id = 1,
        .task_name = "CommTask",
        .timeout_ticks = 500,
        .criticality = RTSFR_CRIT_HIGH
    };
    void *fake_handle = (void *)0x5678;
    s_mock_current_handle = fake_handle;
    assert(rtsfr_register_task(&cfg, fake_handle) == RTSFR_OK);

    /* Advance tick by 400 ticks (Within limit: 1400 - 1000 = 400 <= 500) */
    g_mock_tick = 1400U;
    assert(rtsfr_detect_scan(g_mock_tick) == RTSFR_OK);

    rtsfr_fault_event_t event;
    assert(rtsfr_detect_pop_event(&event) == false); /* Should not fault yet */

    /* Advance tick past timeout: 1000 + 501 = 1501 */
    g_mock_tick = 1501U;
    assert(rtsfr_detect_scan(g_mock_tick) == RTSFR_OK);

    /* Event must be generated */
    assert(rtsfr_detect_pop_event(&event) == true);
    assert(event.task_id == 1);
    assert(event.generation == 1);
    assert(event.fault_type == RTSFR_FAULT_NO_PROGRESS);
    assert(event.observed_tick == 1501);

    /* Second scan shouldn't generate duplicate event */
    assert(rtsfr_detect_scan(1502U) == RTSFR_OK);
    assert(rtsfr_detect_pop_event(&event) == false);
}

/* Test 4: Expected Wait Contract (No false positive) */
static void test_expected_wait_contract(void)
{
    assert(rtsfr_detect_init() == RTSFR_OK);
    g_mock_tick = 2000U;

    rtsfr_task_config_t cfg = {
        .task_id = 2,
        .task_name = "SensorTask",
        .timeout_ticks = 200,
        .criticality = RTSFR_CRIT_MEDIUM
    };
    void *fake_handle = (void *)0x9999;
    s_mock_current_handle = fake_handle;
    assert(rtsfr_register_task(&cfg, fake_handle) == RTSFR_OK);

    /* Task declares a long legitimate wait (1000 ticks) */
    assert(rtsfr_begin_expected_wait(2, 1000) == RTSFR_OK);

    /* Advance tick by 500 (Normally would timeout at 200, but in expected wait!) */
    g_mock_tick = 2500U;
    assert(rtsfr_detect_scan(g_mock_tick) == RTSFR_OK);

    rtsfr_fault_event_t event;
    assert(rtsfr_detect_pop_event(&event) == false); /* Must NOT fault */

    /* Task wakes up on time at 2800 */
    g_mock_tick = 2800U;
    assert(rtsfr_end_expected_wait(2) == RTSFR_OK);

    rtsfr_task_record_t rec;
    assert(rtsfr_detect_get_record(2, &rec) == RTSFR_OK);
    assert(rec.state == RTSFR_TASK_HEALTHY);
    assert(rec.last_progress_tick == 2800);
}

/* Test 5: Expected Wait Overrun (F04) */
static void test_expected_wait_overrun(void)
{
    assert(rtsfr_detect_init() == RTSFR_OK);
    g_mock_tick = 3000U;

    rtsfr_task_config_t cfg = {
        .task_id = 2,
        .task_name = "SensorTask",
        .timeout_ticks = 200,
        .criticality = RTSFR_CRIT_MEDIUM
    };
    void *fake_handle = (void *)0x9999;
    s_mock_current_handle = fake_handle;
    assert(rtsfr_register_task(&cfg, fake_handle) == RTSFR_OK);

    /* Declare wait of 500 ticks (Deadline: 3500) */
    assert(rtsfr_begin_expected_wait(2, 500) == RTSFR_OK);

    /* Task fails to wake up by 3501 */
    g_mock_tick = 3501U;
    assert(rtsfr_detect_scan(g_mock_tick) == RTSFR_OK);

    rtsfr_fault_event_t event;
    assert(rtsfr_detect_pop_event(&event) == true);
    assert(event.task_id == 2);
    assert(event.fault_type == RTSFR_FAULT_WAIT_TIMEOUT);
}

/* Test 6: Generation Rollover & Stale Event Invalidation */
static void test_generation_rollover(void)
{
    assert(rtsfr_detect_init() == RTSFR_OK);
    g_mock_tick = 4000U;

    rtsfr_task_config_t cfg = {
        .task_id = 1,
        .task_name = "ControlTask",
        .timeout_ticks = 100,
        .criticality = RTSFR_CRIT_HIGH
    };
    void *old_handle = (void *)0x1111;
    s_mock_current_handle = old_handle;
    assert(rtsfr_register_task(&cfg, old_handle) == RTSFR_OK);

    /* Trigger fault in Generation 1 */
    g_mock_tick = 4150U;
    assert(rtsfr_detect_scan(g_mock_tick) == RTSFR_OK);

    rtsfr_fault_event_t event;
    assert(rtsfr_detect_pop_event(&event) == true);
    assert(event.task_id == 1);
    assert(event.generation == 1);

    /* Now Module 4 recovers task to Gen 2 with new handle */
    void *new_handle = (void *)0x2222;
    s_mock_current_handle = new_handle;
    rtsfr_detect_on_new_generation(1, new_handle, 2U);

    /* Registry should reflect Gen 2 */
    rtsfr_task_record_t rec;
    assert(rtsfr_detect_get_record(1, &rec) == RTSFR_OK);
    assert(rec.generation == 2);
    assert(rec.task_handle == new_handle);
    assert(rec.state == RTSFR_TASK_HEALTHY);
    assert(rec.fault_pending == false);

    /* Old task handle cannot report progress or begin expected wait after new generation */
    s_mock_current_handle = old_handle;
    assert(rtsfr_report_progress(1) == RTSFR_ERR_STALE);
    assert(rtsfr_begin_expected_wait(1, 100) == RTSFR_ERR_STALE);

    /* Authorized new task handle can report progress normally */
    s_mock_current_handle = new_handle;
    assert(rtsfr_report_progress(1) == RTSFR_OK);

    /* Stale fault report with Gen 1 must be rejected */
    assert(rtsfr_detect_report_fault(1, 1U, 999U, RTSFR_FAULT_SELF_REPORTED) == RTSFR_ERR_STALE);

    /* Fault report with current Gen 2 accepted */
    assert(rtsfr_detect_report_fault(1, 2U, 999U, RTSFR_FAULT_SELF_REPORTED) == RTSFR_OK);
    assert(rtsfr_detect_pop_event(&event) == true);
    assert(event.task_id == 1);
    assert(event.generation == 2);
    assert(event.event_id == 999U);
}

/* Test 7: Tick Wrap-around Safety */
static void test_tick_wraparound(void)
{
    assert(rtsfr_detect_init() == RTSFR_OK);

    /* Set mock tick very close to 0xFFFFFFFF (32-bit unsigned overflow point) */
    g_mock_tick = 0xFFFFFFF0U;

    rtsfr_task_config_t cfg = {
        .task_id = 3,
        .task_name = "EdgeTask",
        .timeout_ticks = 100,
        .criticality = RTSFR_CRIT_MEDIUM
    };
    void *fake_handle = (void *)0x3333;
    s_mock_current_handle = fake_handle;
    assert(rtsfr_register_task(&cfg, fake_handle) == RTSFR_OK);

    /* Tick wraps around past 0 to 0x00000020 (elapsed = 0x10 + 0x20 = 48 < 100) */
    g_mock_tick = 0x00000020U;
    assert(rtsfr_detect_scan(g_mock_tick) == RTSFR_OK);

    rtsfr_fault_event_t event;
    assert(rtsfr_detect_pop_event(&event) == false); /* Still within 100 ticks */

    /* Advance tick past 100 ticks: 0xFFFFFFF0 + 101 = 0x00000055 */
    g_mock_tick = 0x00000055U;
    assert(rtsfr_detect_scan(g_mock_tick) == RTSFR_OK);

    assert(rtsfr_detect_pop_event(&event) == true);
    assert(event.task_id == 3);
    assert(event.fault_type == RTSFR_FAULT_NO_PROGRESS);
}

/* Test 8: Input Validation and Boundary Edge Cases */
static void test_invalid_inputs_and_edge_cases(void)
{
    assert(rtsfr_detect_init() == RTSFR_OK);

    /* NULL config pointer */
    assert(rtsfr_register_task(NULL, (void *)0x1) == RTSFR_ERR_INVALID);

    /* NULL task handle */
    rtsfr_task_config_t cfg_valid = {
        .task_id = 1,
        .task_name = "Valid",
        .timeout_ticks = 100,
        .criticality = RTSFR_CRIT_LOW
    };
    assert(rtsfr_register_task(&cfg_valid, NULL) == RTSFR_ERR_INVALID);

    /* Task ID 0 is invalid */
    rtsfr_task_config_t cfg_zero_id = {
        .task_id = 0,
        .task_name = "ZeroID",
        .timeout_ticks = 100,
        .criticality = RTSFR_CRIT_LOW
    };
    assert(rtsfr_register_task(&cfg_zero_id, (void *)0x1) == RTSFR_ERR_INVALID);

    /* Out of bound task ID */
    rtsfr_task_config_t cfg_oob = {
        .task_id = configRTSFR_MAX_TRACKED_TASKS + 1U,
        .task_name = "OOB",
        .timeout_ticks = 100,
        .criticality = RTSFR_CRIT_LOW
    };
    assert(rtsfr_register_task(&cfg_oob, (void *)0x1) == RTSFR_ERR_INVALID);

    /* Zero timeout ticks is invalid */
    rtsfr_task_config_t cfg_zero_to = {
        .task_id = 1,
        .task_name = "ZeroTimeout",
        .timeout_ticks = 0,
        .criticality = RTSFR_CRIT_LOW
    };
    assert(rtsfr_register_task(&cfg_zero_to, (void *)0x1) == RTSFR_ERR_INVALID);

    /* Unregistered operations on task 1 */
    s_mock_current_handle = (void *)0x1;
    assert(rtsfr_report_progress(1) == RTSFR_ERR_STALE);
    assert(rtsfr_begin_expected_wait(1, 50) == RTSFR_ERR_STALE);
    assert(rtsfr_end_expected_wait(1) == RTSFR_ERR_STALE);

    /* Out of bounds / zero task_id operations */
    assert(rtsfr_report_progress(0) == RTSFR_ERR_INVALID);
    assert(rtsfr_begin_expected_wait(0, 50) == RTSFR_ERR_INVALID);
    assert(rtsfr_end_expected_wait(0) == RTSFR_ERR_INVALID);
    assert(rtsfr_report_progress(configRTSFR_MAX_TRACKED_TASKS + 1U) == RTSFR_ERR_INVALID);
    assert(rtsfr_begin_expected_wait(configRTSFR_MAX_TRACKED_TASKS + 1U, 50) == RTSFR_ERR_INVALID);
    assert(rtsfr_end_expected_wait(configRTSFR_MAX_TRACKED_TASKS + 1U) == RTSFR_ERR_INVALID);

    /* Null pointer for pop event */
    assert(rtsfr_detect_pop_event(NULL) == false);

    /* Null pointer / invalid ID for get_record */
    rtsfr_task_record_t rec;
    assert(rtsfr_detect_get_record(configRTSFR_MAX_TRACKED_TASKS + 1U, &rec) == RTSFR_ERR_INVALID);
    assert(rtsfr_detect_get_record(0, &rec) == RTSFR_ERR_INVALID);
    assert(rtsfr_detect_get_record(1, NULL) == RTSFR_ERR_INVALID);
    assert(rtsfr_detect_get_record(1, &rec) == RTSFR_ERR_NOT_FOUND);

    /* Calling on_new_generation with NULL handle, 0 generation, or on unregistered task does nothing */
    rtsfr_detect_on_new_generation(1, NULL, 1);
    assert(rtsfr_detect_get_record(1, &rec) == RTSFR_ERR_NOT_FOUND);
    rtsfr_detect_on_new_generation(1, (void *)0x99, 0);
    assert(rtsfr_detect_get_record(1, &rec) == RTSFR_ERR_NOT_FOUND);
    rtsfr_detect_on_new_generation(0, (void *)0x99, 1);
    assert(rtsfr_detect_get_record(1, &rec) == RTSFR_ERR_NOT_FOUND);
    rtsfr_detect_on_new_generation(1, (void *)0x99, 1);
    assert(rtsfr_detect_get_record(1, &rec) == RTSFR_ERR_NOT_FOUND);

    /* Task delete with NULL */
    rtsfr_detect_on_task_delete(NULL);

    /* Task ID from handle */
    assert(rtsfr_detect_task_id_from_handle(NULL) == RTSFR_TASK_ID_NONE);
    assert(rtsfr_detect_task_id_from_handle((void *)0x9999) == RTSFR_TASK_ID_NONE);
}

/* Test 9: State Machine Guardrails */
static void test_state_machine_guardrails(void)
{
    assert(rtsfr_detect_init() == RTSFR_OK);
    g_mock_tick = 5000U;

    rtsfr_task_config_t cfg = {
        .task_id = 1,
        .task_name = "GuardedTask",
        .timeout_ticks = 100,
        .criticality = RTSFR_CRIT_MEDIUM
    };
    void *hTask = (void *)0x4444;
    s_mock_current_handle = hTask;
    assert(rtsfr_register_task(&cfg, hTask) == RTSFR_OK);

    /* Double registration is rejected */
    assert(rtsfr_register_task(&cfg, hTask) == RTSFR_ERR_BUSY);

    /* Task enters expected wait of 50 ticks */
    assert(rtsfr_begin_expected_wait(1, 50) == RTSFR_OK);

    /* Wait deadline overruns at 5051 */
    g_mock_tick = 5051U;
    assert(rtsfr_detect_scan(g_mock_tick) == RTSFR_OK);

    rtsfr_task_record_t rec;
    assert(rtsfr_detect_get_record(1, &rec) == RTSFR_OK);
    assert(rec.state == RTSFR_TASK_FAULTY);

    /* Faulty task tries to end expected wait -> rejected because state != EXPECTED_WAIT */
    assert(rtsfr_end_expected_wait(1) == RTSFR_ERR_STALE);
    assert(rtsfr_detect_get_record(1, &rec) == RTSFR_OK);
    assert(rec.state == RTSFR_TASK_FAULTY);

    /* Faulty task tries to enter expected wait -> rejected because state != HEALTHY */
    assert(rtsfr_begin_expected_wait(1, 100) == RTSFR_ERR_STALE);

    /* Delete task -> state transitions to RTSFR_TASK_QUIESCING */
    rtsfr_detect_on_task_delete(hTask);
    assert(rtsfr_detect_get_record(1, &rec) == RTSFR_OK);
    assert(rec.state == RTSFR_TASK_QUIESCING);
    assert(rec.task_handle == NULL);

    /* Quiescing task cannot report progress or begin wait */
    assert(rtsfr_report_progress(1) == RTSFR_ERR_STALE);
    assert(rtsfr_begin_expected_wait(1, 100) == RTSFR_ERR_STALE);
    assert(rtsfr_end_expected_wait(1) == RTSFR_ERR_STALE);

    /* Pop the fault event to clean up */
    rtsfr_fault_event_t event;
    assert(rtsfr_detect_pop_event(&event) == true);
    assert(event.task_id == 1);
    assert(event.fault_type == RTSFR_FAULT_WAIT_TIMEOUT);
}

/* Test 10: Circular Buffer Boundary and Queue-Full Resilience */
static void test_circular_buffer_and_queue_full_recovery(void)
{
    assert(rtsfr_detect_init() == RTSFR_OK);
    g_mock_tick = 6000U;

    rtsfr_task_config_t cfg = {
        .task_id = 1,
        .task_name = "QueueTask",
        .timeout_ticks = 100,
        .criticality = RTSFR_CRIT_LOW
    };
    void *hTask = (void *)0x7777;
    s_mock_current_handle = hTask;
    assert(rtsfr_register_task(&cfg, hTask) == RTSFR_OK);

    /* Fill event queue up to capacity (configRTSFR_EVENT_QUEUE_SIZE = 8) */
    for (uint32_t i = 1U; i <= configRTSFR_EVENT_QUEUE_SIZE; i++) {
        assert(rtsfr_detect_report_fault(1, 1, 100U + i, RTSFR_FAULT_SELF_REPORTED) == RTSFR_OK);
        /* Report progress resets fault_pending so next fault can be reported */
        s_mock_current_handle = hTask;
        assert(rtsfr_report_progress(1) == RTSFR_OK);
    }

    /* Queue is full now. Next fault should overflow */
    assert(rtsfr_detect_report_fault(1, 1, 200U, RTSFR_FAULT_SELF_REPORTED) == RTSFR_ERR_NO_RESOURCE);
    assert(rtsfr_detect_event_overflow_count() == 1U);

    /* Pop one event to make room (FIFO ordering check: should be first event id 101) */
    rtsfr_fault_event_t event;
    assert(rtsfr_detect_pop_event(&event) == true);
    assert(event.event_id == 101U);
    assert(event.task_id == 1);

    /* Now we have room for 1 event */
    s_mock_current_handle = hTask;
    assert(rtsfr_report_progress(1) == RTSFR_OK);
    assert(rtsfr_detect_report_fault(1, 1, 201U, RTSFR_FAULT_SELF_REPORTED) == RTSFR_OK);

    /* Pop all remaining events (7 old + 1 new = 8 total) in FIFO order */
    for (uint32_t i = 2U; i <= configRTSFR_EVENT_QUEUE_SIZE; i++) {
        assert(rtsfr_detect_pop_event(&event) == true);
        assert(event.event_id == 100U + i);
    }
    /* And the newly inserted event */
    assert(rtsfr_detect_pop_event(&event) == true);
    assert(event.event_id == 201U);

    /* Queue should now be completely empty */
    assert(rtsfr_detect_pop_event(&event) == false);
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

    return (g_tests_passed == g_tests_total) ? 0 : 1;
}
