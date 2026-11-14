#include "m5_command.h"

#include <stdlib.h>
#include <string.h>

#include "cycle_counter.h"
#include "fault_scenarios.h"
#include "iwdg.h"
#include "m5_test_runner.h"
#include "m5_uart.h"
#include "rtsfr_m5_adapter.h"
#include "rtsfr_system.h"
#include "rtsfr_trace.h"

static char g_command[RTSFR_COMMAND_CAPACITY];
static size_t g_command_length;
static bool g_discarding;
static uint32_t g_last_trace_dropped;
static uint32_t g_last_rx_dropped;
static uint32_t g_last_tx_errors;

static bool prv_number(const char *text,
                       unsigned long maximum,
                       unsigned long *out)
{
    char *end;
    unsigned long value;
    if ((text == NULL) || (*text == '\0') || (*text == '-') ||
        (*text == '+'))
    {
        return false;
    }
    value = strtoul(text, &end, 10);
    if ((*end != '\0') || (value > maximum))
    {
        return false;
    }
    *out = value;
    return true;
}

static uint8_t prv_demo_test_id(const char *text)
{
    if (text == NULL)
    {
        return 0U;
    }
    if ((strcmp(text, "d01") == 0) || (strcmp(text, "D01") == 0))
    {
        return 1U;
    }
    if ((strcmp(text, "d02") == 0) || (strcmp(text, "D02") == 0))
    {
        return 2U;
    }
    if ((strcmp(text, "d03") == 0) || (strcmp(text, "D03") == 0))
    {
        return 5U;
    }
    if ((strcmp(text, "d04") == 0) || (strcmp(text, "D04") == 0))
    {
        return 6U;
    }
    if ((strcmp(text, "d05") == 0) || (strcmp(text, "D05") == 0))
    {
        return 12U;
    }
    return 0U;
}

static void prv_execute(void)
{
    char *verb = strtok(g_command, " ");
    char *first = strtok(NULL, " ");
    char *second = strtok(NULL, " ");
    char *third = strtok(NULL, " ");
    char *extra = strtok(NULL, " ");
    unsigned long x;
    unsigned long y;
    unsigned long z;
    rtsfr_status_t status = RTSFR_ERR_INVALID;

    if (verb == NULL)
    {
        return;
    }
    if ((strcmp(verb, "help") == 0) && (first == NULL))
    {
        rtsfr_m5_uart_write(
            "# demo d01..d05 | status | verify | format terminal|csv\r\n"
            "# advanced: run ID | test 1..14 | inject f08 | "
            "inject 1..7 TASK GENERATION | disarm | watchdog-start\r\n");
        return;
    }
    if ((strcmp(verb, "status") == 0) && (first == NULL))
    {
        rtsfr_system_metrics_t metrics;
        rtsfr_m5_emit(M5_STATUS, 0U, 0U, 0U, 0U, 0U,
                      rtsfr_trace_dropped());
        rtsfr_m5_emit(M5_STATUS, 0U, 0U, 0U, 0U, 1U,
                      rtsfr_m5_uart_rx_dropped());
        rtsfr_m5_emit(M5_STATUS, 0U, 0U, 0U, 0U, 2U,
                      rtsfr_m5_uart_tx_errors());
        rtsfr_m5_emit(M5_STATUS, 0U, 0U, 0U, 0U, 3U,
                      rtsfr_iwdg_started() ? 1U : 0U);
        rtsfr_m5_emit(M5_STATUS, 0U, 0U, 0U, 0U, 5U,
                      (uint32_t)xPortGetFreeHeapSize());
        rtsfr_m5_emit(M5_STATUS, 0U, 0U, 0U, 0U, 6U,
                      (uint32_t)xPortGetMinimumEverFreeHeapSize());
        if (rtsfr_system_get_metrics(&metrics) == RTSFR_OK)
        {
            rtsfr_m5_emit(M5_STATUS, 0U, 0U, 1U, 0U, 10U,
                          metrics.task_progress[0]);
            rtsfr_m5_emit(M5_STATUS, 0U, 0U, 2U, 0U, 10U,
                          metrics.task_progress[1]);
            rtsfr_m5_emit(M5_STATUS, 0U, 0U, 3U, 0U, 10U,
                          metrics.task_progress[2]);
            rtsfr_m5_emit(M5_STATUS, 0U, 0U, 1U, 0U, 11U,
                          metrics.task_stack_high_water[0]);
            rtsfr_m5_emit(M5_STATUS, 0U, 0U, 2U, 0U, 11U,
                          metrics.task_stack_high_water[1]);
            rtsfr_m5_emit(M5_STATUS, 0U, 0U, 3U, 0U, 11U,
                          metrics.task_stack_high_water[2]);
            rtsfr_m5_emit(M5_STATUS, 0U, 0U, 0U, 0U, 12U,
                          uxTaskGetStackHighWaterMark(NULL));
            rtsfr_m5_emit(M5_STATUS, 0U, 0U, 0U, 0U, 13U,
                          metrics.monitor_polls);
            rtsfr_m5_emit(M5_STATUS, 0U, 0U, 0U, 0U, 14U,
                          metrics.monitor_last_cycles);
            rtsfr_m5_emit(M5_STATUS, 0U, 0U, 0U, 0U, 15U,
                          metrics.monitor_max_cycles);
            rtsfr_m5_emit(M5_STATUS, 0U, 0U, 0U, 0U, 16U,
                          metrics.event_overflows);
            rtsfr_m5_emit(M5_STATUS, 0U, 0U, 0U, 0U, 17U,
                          metrics.resource_anomalies);
            rtsfr_m5_emit(M5_STATUS, 0U, 0U, 0U, 0U, 18U,
                          metrics.successful_recoveries);
            rtsfr_m5_emit(M5_STATUS, 0U, 0U, 0U, 0U, 19U,
                          metrics.unsafe_restarts_prevented);
            rtsfr_m5_emit(M5_STATUS, 0U, 0U, 0U, 0U, 20U,
                          metrics.false_alarms_rolled_back);
        }
        (void)rtsfr_system_emit_snapshot();
        return;
    }
    if ((strcmp(verb, "format") == 0) && (first != NULL) &&
        (second == NULL))
    {
        if (strcmp(first, "terminal") == 0)
        {
            rtsfr_m5_uart_set_format(RTSFR_UART_FORMAT_TERMINAL);
            rtsfr_m5_uart_write_format_header();
            return;
        }
        if (strcmp(first, "csv") == 0)
        {
            rtsfr_m5_uart_set_format(RTSFR_UART_FORMAT_CSV);
            rtsfr_m5_uart_write_format_header();
            return;
        }
    }
    else if ((strcmp(verb, "demo") == 0) && (first != NULL) &&
             (second == NULL))
    {
        uint8_t test_id = prv_demo_test_id(first);
        if (test_id != 0U)
        {
            status = rtsfr_m5_uart_set_run_id(first);
            if (status == RTSFR_OK)
            {
                (void)rtsfr_m5_test_start(test_id);
            }
            return;
        }
    }
    else if ((strcmp(verb, "run") == 0) && (first != NULL) &&
        (second == NULL))
    {
        rtsfr_trace_event_t event;
        if (rtsfr_m5_test_active() != 0U)
        {
            status = RTSFR_ERR_BUSY;
        }
        else
        {
            while (rtsfr_trace_pop(&event))
            {
                rtsfr_m5_uart_log(&event);
            }
            status = rtsfr_m5_uart_set_run_id(first);
        }
    }
    else if ((strcmp(verb, "verify") == 0) && (first == NULL))
    {
        (void)rtsfr_verify_invariants();
        return;
    }
    else if ((strcmp(verb, "test") == 0) && (second == NULL) &&
             prv_number(first, 14U, &x) && (x > 0U))
    {
        (void)rtsfr_m5_test_start((uint8_t)x);
        return;
    }
    else if ((strcmp(verb, "inject") == 0) && (first != NULL) &&
             (strcmp(first, "f08") == 0) && (second == NULL))
    {
        (void)rtsfr_m5_test_start(12U);
        return;
    }
    else if ((strcmp(verb, "inject") == 0) && (extra == NULL) &&
             prv_number(first, 7U, &x) &&
             prv_number(second, RTSFR_VERIFY_MAX_TASKS, &y) &&
             prv_number(third, 65535U, &z))
    {
        status = rtsfr_fault_arm((uint8_t)x, (uint8_t)y, (uint16_t)z);
    }
    else if ((strcmp(verb, "disarm") == 0) && (first == NULL))
    {
        rtsfr_fault_disarm();
        status = RTSFR_OK;
    }
    else if ((strcmp(verb, "watchdog-start") == 0) && (first == NULL))
    {
        status = rtsfr_iwdg_start();
    }

    rtsfr_m5_emit((status == RTSFR_OK) ? M5_STATUS : M5_NOT_READY,
                  0U, 0U, 0U, 0U, (uint16_t)status, 0U);
}

void rtsfr_m5_command_feed(char byte)
{
    if ((byte == '\r') || (byte == '\n'))
    {
        if (!g_discarding && (g_command_length != 0U))
        {
            g_command[g_command_length] = '\0';
            prv_execute();
        }
        g_command_length = 0U;
        g_discarding = false;
        return;
    }
    if (g_discarding)
    {
        return;
    }
    if ((byte == '\b') || (byte == 127))
    {
        if (g_command_length != 0U)
        {
            g_command_length--;
        }
        return;
    }
    if (((unsigned char)byte < 32U) || ((unsigned char)byte > 126U))
    {
        return;
    }
    if (g_command_length == (sizeof(g_command) - 1U))
    {
        g_discarding = true;
        g_command_length = 0U;
        rtsfr_m5_emit(M5_NOT_READY, 0U, 0U, 0U, 0U,
                      RTSFR_ERR_INVALID, RTSFR_COMMAND_CAPACITY);
        return;
    }
    g_command[g_command_length++] = byte;
}

void rtsfr_m5_service_init(void)
{
    rtsfr_trace_init();
    rtsfr_m5_adapter_init();
    rtsfr_m5_uart_init();
    g_command_length = 0U;
    g_discarding = false;
    g_last_trace_dropped = 0U;
    g_last_rx_dropped = 0U;
    g_last_tx_errors = 0U;
    rtsfr_m5_uart_write_format_header();
    rtsfr_m5_emit(M5_BOOT, 0U, 0U, 0U, 0U, RTSFR_M5_TEST_BUILD,
                  SystemCoreClock);
    rtsfr_m5_emit(M5_RESET_CAUSE, 0U, 0U, 0U, 0U,
                  rtsfr_iwdg_was_reset() ? 1U : 0U,
                  rtsfr_iwdg_reset_cause());
    if (rtsfr_iwdg_test12_passed())
    {
        rtsfr_m5_emit(M5_TEST_PASS, 12U, 0U, 0U, 0U, 0U,
                      rtsfr_iwdg_reset_cause());
    }
    rtsfr_m5_emit(M5_STATUS, 0U, 0U, 0U, 0U, 4U,
                  rtsfr_cycles_init() ? 1U : 0U);
}

void rtsfr_m5_service_poll(void)
{
    char byte;
    unsigned int budget = 128U;
    rtsfr_trace_event_t event;
    uint32_t dropped;

    dropped = rtsfr_m5_uart_rx_dropped();
    if (dropped != g_last_rx_dropped)
    {
        g_last_rx_dropped = dropped;
        g_discarding = true;
        g_command_length = 0U;
        rtsfr_m5_emit(M5_STATUS, 0U, 0U, 0U, 0U, 1U, dropped);
    }
    while ((budget-- != 0U) && rtsfr_m5_uart_getchar(&byte))
    {
        rtsfr_m5_command_feed(byte);
    }
    rtsfr_m5_test_poll();
    (void)rtsfr_iwdg_poll();
    budget = 4U;
    while ((budget-- != 0U) && rtsfr_trace_pop(&event))
    {
        rtsfr_m5_uart_log(&event);
    }
    dropped = rtsfr_trace_dropped();
    if (dropped != g_last_trace_dropped)
    {
        g_last_trace_dropped = dropped;
        rtsfr_m5_emit(M5_TRACE_DROPPED, rtsfr_m5_test_active(), 0U,
                      0U, 0U, 0U, dropped);
    }
    dropped = rtsfr_m5_uart_tx_errors();
    if (dropped != g_last_tx_errors)
    {
        g_last_tx_errors = dropped;
        rtsfr_m5_emit(M5_STATUS, 0U, 0U, 0U, 0U, 2U, dropped);
    }
}
