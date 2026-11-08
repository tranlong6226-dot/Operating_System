#include "m5_command.h"
#include "main.h"
#include "fault_scenarios.h"
#include "m5_test_runner.h"
#include "m5_uart.h"
#include "iwdg.h"
#include "cycle_counter.h"
#include "rtsfr_m5_adapter.h"
#include "rtsfr_trace.h"
#include <string.h>
#include <stdlib.h>
static char command[RTSFR_COMMAND_CAPACITY];
static size_t length;
static bool discarding;
static uint32_t last_dropped, last_rx_dropped, last_tx_errors;
static bool number(const char *text, unsigned long max, unsigned long *out)
{
    char *end;
    unsigned long value;
    if ((text == NULL) || (*text == '\0') || (*text == '-') || (*text == '+')) return false;
    value = strtoul(text, &end, 10);
    if ((*end != '\0') || (value > max)) return false;
    *out = value; return true;
}
static void execute(void)
{
    char *verb = strtok(command, " ");
    char *a = strtok(NULL, " "), *b = strtok(NULL, " "), *c = strtok(NULL, " ");
    char *extra = strtok(NULL, " ");
    unsigned long x, y, z;
    rtsfr_status_t status = RTSFR_ERR_INVALID;
    if (verb == NULL) return;
    if ((strcmp(verb, "help") == 0) && a == NULL) {
        rtsfr_m5_uart_write("# help | status | run ID | verify | test 1..14 | inject 1..6 TASK GENERATION | disarm | watchdog-start | iwdg-reset\r\n");
        return;
    }
    if ((strcmp(verb, "status") == 0) && a == NULL) {
        rtsfr_m5_emit(M5_STATUS, 0U, 0U, 0U, 0U, 0U, rtsfr_trace_dropped());
        rtsfr_m5_emit(M5_STATUS, 0U, 0U, 0U, 0U, 1U, rtsfr_m5_uart_rx_dropped());
        rtsfr_m5_emit(M5_STATUS, 0U, 0U, 0U, 0U, 2U, rtsfr_m5_uart_tx_errors());
        rtsfr_m5_emit(M5_STATUS, 0U, 0U, 0U, 0U, 3U, rtsfr_iwdg_started() ? 1U : 0U);
        return;
    }
    if ((strcmp(verb, "run") == 0) && a != NULL && b == NULL) {
        if (rtsfr_m5_test_active() != 0U) status = RTSFR_ERR_BUSY;
        else {
            rtsfr_trace_event_t e;
            while (rtsfr_trace_pop(&e)) rtsfr_m5_uart_log(&e);
            status = rtsfr_m5_uart_set_run_id(a);
        }
    } else if ((strcmp(verb, "verify") == 0) && a == NULL) {
        (void)rtsfr_verify_invariants(); return;
    } else if ((strcmp(verb, "test") == 0) && b == NULL && number(a, 14U, &x) && x > 0U) {
        (void)rtsfr_m5_test_start((uint8_t)x); return;
    } else if ((strcmp(verb, "inject") == 0) && extra == NULL && number(a, 6U, &x) &&
               number(b, RTSFR_VERIFY_MAX_TASKS, &y) && number(c, 65535U, &z)) {
        status = rtsfr_fault_arm((uint8_t)x, (uint8_t)y, (uint16_t)z);
    } else if ((strcmp(verb, "disarm") == 0) && a == NULL) {
        rtsfr_fault_disarm(); status = RTSFR_OK;
    } else if ((strcmp(verb, "watchdog-start") == 0) && a == NULL) {
        status = rtsfr_iwdg_start();
    } else if ((strcmp(verb, "iwdg-reset") == 0) && a == NULL) {
        /* Manual primitive test is deliberately distinct from integrated TC12. */
        rtsfr_m5_uart_write("# BOARD_IWDG_RESET: NOT TC12; waiting for hardware reset if configured\r\n");
        status = rtsfr_iwdg_board_reset_test();
    }
    rtsfr_m5_emit(status == RTSFR_OK ? M5_STATUS : M5_NOT_READY,
                   0U, 0U, 0U, 0U, (uint16_t)status, 0U);
}
void rtsfr_m5_command_feed(char byte)
{
    if ((byte == '\r') || (byte == '\n')) {
        if (!discarding && length != 0U) { command[length] = '\0'; execute(); }
        length = 0U; discarding = false; return;
    }
    if (discarding) return;
    if ((byte == '\b') || (byte == 127)) { if (length != 0U) --length; return; }
    if ((unsigned char)byte < 32U || (unsigned char)byte > 126U) return;
    if (length == sizeof(command) - 1U) {
        discarding = true; length = 0U;
        rtsfr_m5_emit(M5_NOT_READY, 0U, 0U, 0U, 0U, RTSFR_ERR_INVALID, RTSFR_COMMAND_CAPACITY);
        return;
    }
    command[length++] = byte;
}
void rtsfr_m5_service_init(void)
{
    rtsfr_m5_adapter_init(); rtsfr_m5_uart_init();
    length = 0U; discarding = false;
    last_dropped = last_rx_dropped = last_tx_errors = 0U;
    rtsfr_m5_uart_write("run_id,version,test_id,tick,event_id,task_id,generation,event,reason,value,cycles\r\n");
    rtsfr_m5_uart_write("# M5_ONLY: M1/M2/M3 bindings absent until provided by owners; hardware results NOT_RUN\r\n");
    rtsfr_m5_emit(M5_BOOT, 0U, 0U, 0U, 0U, RTSFR_M5_TEST_BUILD, SystemCoreClock);
    rtsfr_m5_emit(M5_RESET_CAUSE, 0U, 0U, 0U, 0U, rtsfr_iwdg_was_reset() ? 1U : 0U, rtsfr_iwdg_reset_cause());
    rtsfr_m5_emit(M5_STATUS, 0U, 0U, 0U, 0U, 4U, rtsfr_cycles_init() ? 1U : 0U);
}
void rtsfr_m5_service_poll(void)
{
    char byte;
    unsigned int budget = 128U;
    rtsfr_trace_event_t e;
    uint32_t dropped_now;
    dropped_now = rtsfr_m5_uart_rx_dropped();
    if (dropped_now != last_rx_dropped) {
        last_rx_dropped = dropped_now;
        discarding = true; length = 0U;
        rtsfr_m5_emit(M5_STATUS, 0U, 0U, 0U, 0U, 1U, dropped_now);
    }
    while (budget-- != 0U && rtsfr_m5_uart_getchar(&byte)) rtsfr_m5_command_feed(byte);
    rtsfr_m5_test_poll(); (void)rtsfr_iwdg_poll();
    budget = 4U; /* UART never runs in a kernel hook. */
    while (budget-- != 0U && rtsfr_trace_pop(&e)) rtsfr_m5_uart_log(&e);
    dropped_now = rtsfr_trace_dropped();
    if (dropped_now != last_dropped) {
        last_dropped = dropped_now;
        rtsfr_m5_emit(M5_TRACE_DROPPED, rtsfr_m5_test_active(), 0U, 0U, 0U, 0U, dropped_now);
    }
    dropped_now = rtsfr_m5_uart_tx_errors();
    if (dropped_now != last_tx_errors) {
        last_tx_errors = dropped_now;
        rtsfr_m5_emit(M5_STATUS, 0U, 0U, 0U, 0U, 2U, dropped_now);
    }
}
