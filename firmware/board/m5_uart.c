#include "m5_uart.h"

#include <stdio.h>
#include <string.h>

#include "main.h"

extern UART_HandleTypeDef huart1;

static char g_rx[RTSFR_RX_CAPACITY];
static uint16_t g_head;
static uint16_t g_tail;
static uint16_t g_count;
static uint32_t g_rx_dropped;
static uint32_t g_tx_errors;
static char g_run_id[32] = "boot";
static rtsfr_uart_format_t g_format;

static const char *prv_task_name(uint8_t task_id)
{
    static const char *const names[] = {"SYSTEM", "SENSOR", "COMM", "CONTROL"};
    return (task_id <= 3U) ? names[task_id] : "UNKNOWN";
}

static const char *prv_demo_name(uint8_t test_id)
{
    switch (test_id)
    {
    case 1U: return "D01 NORMAL OPERATION";
    case 2U: return "D02 SELECTIVE RECOVERY";
    case 5U: return "D03 RESOURCE-AWARE RECOVERY";
    case 6U: return "D04 UNSAFE RECOVERY PREVENTION";
    case 12U: return "D05 IWDG WATCHDOG FALLBACK";
    default: return "INTERNAL TEST";
    }
}

static const char *prv_action_name(uint32_t action)
{
    static const char *const names[] = {
        "IGNORE", "OBSERVE", "RESTART", "DEFER", "SAFE_MODE", "RESET"};
    return (action < 6U) ? names[action] : "UNKNOWN";
}

static const char *prv_policy_reason(uint16_t reason)
{
    switch (reason)
    {
    case 0x0301U: return "NO_DANGEROUS_RESOURCE";
    case 0x0302U: return "BUFFER_RECLAIMABLE";
    case 0x0401U: return "MUTEX_HELD";
    case 0x0402U: return "UNKNOWN_RESOURCE";
    case 0x0403U: return "IO_IN_FLIGHT";
    case 0x0404U: return "RESOURCE_NOT_RECLAIMABLE";
    case 0x0405U: return "RETRY_EXHAUSTED";
    case 0x0407U: return "RECOVERY_FAILED";
    case 0x0408U: return "EVENT_QUEUE_OVERFLOW";
    case 0x0502U: return "KERNEL_STALL";
    default: return "OTHER";
    }
}

static const char *prv_fault_name(uint16_t reason)
{
    static const char *const names[] = {
        "NONE", "NO_PROGRESS", "BUSY_LOOP", "SELF_REPORTED",
        "EXPECTED_WAIT", "WAIT_TIMEOUT", "REPEATED_FAILURE",
        "KERNEL_STALL", "STACK_OVERFLOW", "HARD_FAULT", "CORRUPTION"};
    return (reason < (sizeof(names) / sizeof(names[0])))
               ? names[reason]
               : "UNKNOWN";
}

static const char *prv_scenario_name(uint16_t scenario)
{
    static const char *const names[] = {
        "NONE", "STOP_PROGRESS", "BUSY_LOOP", "SELF_REPORT",
        "EXPECTED_WAIT", "BUFFER_HELD", "MUTEX_HELD",
        "SELF_REPORT_STALL", "KERNEL_STALL"};
    return (scenario < (sizeof(names) / sizeof(names[0])))
               ? names[scenario]
               : "UNKNOWN";
}

static const char *prv_recovery_state(uint32_t state)
{
    static const char *const names[] = {
        "IDLE", "QUIESCING", "REVALIDATING", "CLEANUP", "DELETING",
        "WAIT_KERNEL_CLEANUP", "RECREATING", "VERIFYING", "COMPLETE",
        "SAFE_MODE"};
    return (state < (sizeof(names) / sizeof(names[0])))
               ? names[state]
               : "UNKNOWN";
}

static const char *prv_recovery_tag(uint32_t state)
{
    switch (state)
    {
    case 1U: return "QUIESCE";
    case 2U: return "RESOURCE";
    case 3U: return "CLEANUP";
    case 4U: return "DELETE";
    case 5U: return "KERNEL";
    case 6U: return "RECREATE";
    case 7U: return "VERIFY";
    case 8U: return "RECOVERY";
    case 9U: return "SYSTEM";
    default: return "RECOVERY";
    }
}

static const char *prv_task_state(uint16_t flags)
{
    static const char *const names[] = {
        "UNREGISTERED", "REGISTERED", "HEALTHY", "EXPECTED_WAIT", "SUSPECT",
        "FAULTY", "QUIESCING", "CLEANUP", "RECREATE", "VERIFYING", "SAFE_MODE"};
    uint16_t state = flags & 0x00FFU;
    return (state < (sizeof(names) / sizeof(names[0])))
               ? names[state]
               : "UNKNOWN";
}

static void prv_log_terminal(const rtsfr_trace_event_t *event)
{
    char line[224];
    uint32_t mutexes;
    uint32_t buffers;
    uint32_t inflight;
    uint32_t reclaimable;

    switch (event->event)
    {
    case M5_BOOT:
        (void)snprintf(line, sizeof(line),
                       "[BOOT]     tick=%lu firmware=%s\r\n",
                       (unsigned long)event->tick, RTSFR_M5_VERSION);
        break;
    case M5_RESET_CAUSE:
        (void)snprintf(line, sizeof(line),
                       "[BOOT]     reset_cause=%s raw=0x%08lX\r\n",
                       (event->reason != 0U) ? "IWDG" : "POWER_ON",
                       (unsigned long)event->value);
        break;
    case M5_TEST_STARTED:
        (void)snprintf(line, sizeof(line),
                       "\r\n=== %s ===\r\n[DEMO]     started tick=%lu\r\n",
                       prv_demo_name(event->test_id),
                       (unsigned long)event->tick);
        break;
    case M5_INJECT:
        (void)snprintf(line, sizeof(line),
                       "[INJECT]   tick=%lu task=%s gen=%u fault=%s\r\n",
                       (unsigned long)event->tick,
                       prv_task_name(event->task_id),
                       (unsigned int)event->generation,
                       prv_scenario_name(event->reason));
        break;
    case M5_FAULT_DETECTED:
        (void)snprintf(line, sizeof(line),
                       "[FAULT]    tick=%lu task=%s gen=%u reason=%s\r\n",
                       (unsigned long)event->tick,
                       prv_task_name(event->task_id),
                       (unsigned int)event->generation,
                       prv_fault_name(event->reason));
        break;
    case M5_RESOURCE_CHECK:
        mutexes = event->value & 0xFFU;
        buffers = (event->value >> 8U) & 0xFFU;
        inflight = (event->value >> 24U) & 0x01U;
        reclaimable = (event->value >> 25U) & 0x01U;
        (void)snprintf(line, sizeof(line),
                       "[RESOURCE] tick=%lu task=%s mutex=%lu buffer=%lu "
                       "inflight=%s reclaimable=%s\r\n",
                       (unsigned long)event->tick,
                       prv_task_name(event->task_id),
                       (unsigned long)mutexes, (unsigned long)buffers,
                       inflight ? "YES" : "NO",
                       reclaimable ? "YES" : "NO");
        break;
    case M5_POLICY:
        (void)snprintf(line, sizeof(line),
                       "[POLICY]   tick=%lu task=%s action=%s reason=%s\r\n",
                       (unsigned long)event->tick,
                       prv_task_name(event->task_id),
                       prv_action_name(event->value),
                       prv_policy_reason(event->reason));
        break;
    case M5_RECOVERY_STEP:
        (void)snprintf(line, sizeof(line),
                       "[%-8s] tick=%lu task=%s gen=%u state=%s reason=%u\r\n",
                       prv_recovery_tag(event->value),
                       (unsigned long)event->tick,
                       prv_task_name(event->task_id),
                       (unsigned int)event->generation,
                       prv_recovery_state(event->value),
                       (unsigned int)event->reason);
        break;
    case M5_FIRST_PROGRESS:
        (void)snprintf(line, sizeof(line),
                       "[VERIFY]   tick=%lu task=%s gen=%u result=FIRST_PROGRESS\r\n",
                       (unsigned long)event->tick,
                       prv_task_name(event->task_id),
                       (unsigned int)event->generation);
        break;
    case M5_RECOVERY_OK:
        (void)snprintf(line, sizeof(line),
                       "[VERIFY]   tick=%lu task=%s gen=%u result=RECOVERY_OK "
                       "latency=%lu_ticks\r\n",
                       (unsigned long)event->tick,
                       prv_task_name(event->task_id),
                       (unsigned int)event->generation,
                       (unsigned long)event->value);
        break;
    case M5_RECOVERY_FAILED:
        (void)snprintf(line, sizeof(line),
                       "[RECOVERY] tick=%lu task=%s result=FAILED reason=%u\r\n",
                       (unsigned long)event->tick,
                       prv_task_name(event->task_id),
                       (unsigned int)event->reason);
        break;
    case M5_SAFE_MODE:
        (void)snprintf(line, sizeof(line),
                       "[SYSTEM]   tick=%lu state=SAFE_MODE task=%s "
                       "reason=%s restart=DENIED\r\n",
                       (unsigned long)event->tick,
                       prv_task_name(event->task_id),
                       prv_policy_reason(event->reason));
        break;
    case M5_WATCHDOG_STARTED:
        (void)snprintf(line, sizeof(line),
                       "[IWDG]     tick=%lu state=ACTIVE reload=%lu\r\n",
                       (unsigned long)event->tick,
                       (unsigned long)event->value);
        break;
    case M5_VERIFY_PASS:
    case M5_VERIFY_FAIL:
        (void)snprintf(line, sizeof(line),
                       "[VERIFY]   tick=%lu result=%s reason=%u\r\n",
                       (unsigned long)event->tick,
                       (event->event == M5_VERIFY_PASS) ? "PASS" : "FAIL",
                       (unsigned int)event->reason);
        break;
    case M5_TEST_PASS:
    case M5_TEST_FAIL:
        (void)snprintf(line, sizeof(line),
                       "[RESULT]   %s result=%s duration=%lu_ticks reason=%u\r\n\r\n",
                       prv_demo_name(event->test_id),
                       (event->event == M5_TEST_PASS) ? "PASS" : "FAIL",
                       (unsigned long)event->value,
                       (unsigned int)event->reason);
        break;
    case M5_NOT_READY:
        (void)snprintf(line, sizeof(line),
                       "[ERROR]    %s NOT_READY status=%u\r\n",
                       prv_demo_name(event->test_id),
                       (unsigned int)event->reason);
        break;
    case M5_TRACE_DROPPED:
        (void)snprintf(line, sizeof(line),
                       "[WARNING]  trace_dropped=%lu\r\n",
                       (unsigned long)event->value);
        break;
    case M5_SNAPSHOT_BEGIN:
        (void)snprintf(line, sizeof(line),
                       "\r\n[STATUS]   tick=%lu tasks=%lu resources=%u\r\n",
                       (unsigned long)event->tick,
                       (unsigned long)event->value,
                       (unsigned int)event->reason);
        break;
    case M5_TASK_SNAPSHOT:
        (void)snprintf(line, sizeof(line),
                       "  %-7s Gen=%u Count=%lu %-13s Resource=%s\r\n",
                       prv_task_name(event->task_id),
                       (unsigned int)event->generation,
                       (unsigned long)event->value,
                       prv_task_state(event->reason),
                       ((event->reason & 0x0400U) != 0U) ? "SAFE" : "UNSAFE");
        break;
    case M5_RESOURCE_SNAPSHOT:
        (void)snprintf(line, sizeof(line),
                       "  RESOURCE type=%s id=0x%08lX owner=%s gen=%u state=%u\r\n",
                       (event->value == 2U) ? "BUFFER" : "MUTEX",
                       (unsigned long)event->event_id,
                       prv_task_name(event->task_id),
                       (unsigned int)event->generation,
                       (unsigned int)event->reason);
        break;
    case M5_SYSTEM_SNAPSHOT:
        (void)snprintf(line, sizeof(line),
                       "  SYSTEM safe_mode=%s recovery=%s reset_requested=%s\r\n",
                       (event->reason != 0U) ? "YES" : "NO",
                       prv_recovery_state(event->value),
                       (event->task_id != 0U) ? "YES" : "NO");
        break;
    case M5_SNAPSHOT_END:
        (void)snprintf(line, sizeof(line), "\r\n");
        break;
    case M5_STATUS:
    case M5_TASK_CREATE:
    case M5_TASK_DELETE:
        return;
    default:
        (void)snprintf(line, sizeof(line),
                       "[EVENT]    tick=%lu event=%s task=%s gen=%u reason=%u value=%lu\r\n",
                       (unsigned long)event->tick,
                       rtsfr_trace_name(event->event),
                       prv_task_name(event->task_id),
                       (unsigned int)event->generation,
                       (unsigned int)event->reason,
                       (unsigned long)event->value);
        break;
    }
    rtsfr_m5_uart_write(line);
}

void rtsfr_m5_uart_init(void)
{
    g_head = 0U;
    g_tail = 0U;
    g_count = 0U;
    g_rx_dropped = 0U;
    g_tx_errors = 0U;
    g_format = RTSFR_UART_FORMAT_TERMINAL;
    HAL_NVIC_SetPriority(USART1_IRQn,
                         configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, 0U);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    SET_BIT(USART1->CR1, USART_CR1_RXNEIE);
}

void rtsfr_m5_uart_irq(void)
{
    uint32_t status = USART1->SR;
    if ((status & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE |
                   USART_SR_NE)) != 0U)
    {
        char byte = (char)USART1->DR;
        UBaseType_t mask = taskENTER_CRITICAL_FROM_ISR();
        if ((status & (USART_SR_ORE | USART_SR_FE | USART_SR_NE)) != 0U)
        {
            g_rx_dropped++;
        }
        else if (g_count == RTSFR_RX_CAPACITY)
        {
            g_rx_dropped++;
        }
        else
        {
            g_rx[g_head] = byte;
            g_head = (uint16_t)((g_head + 1U) % RTSFR_RX_CAPACITY);
            g_count++;
        }
        taskEXIT_CRITICAL_FROM_ISR(mask);
    }
}

bool rtsfr_m5_uart_getchar(char *out)
{
    bool found = false;
    if (out == NULL)
    {
        return false;
    }
    taskENTER_CRITICAL();
    if (g_count != 0U)
    {
        *out = g_rx[g_tail];
        g_tail = (uint16_t)((g_tail + 1U) % RTSFR_RX_CAPACITY);
        g_count--;
        found = true;
    }
    taskEXIT_CRITICAL();
    return found;
}

void rtsfr_m5_uart_write(const char *text)
{
    if ((text != NULL) &&
        (HAL_UART_Transmit(&huart1, (uint8_t *)(uintptr_t)text,
                           (uint16_t)strlen(text), 100U) != HAL_OK))
    {
        g_tx_errors++;
    }
}

rtsfr_status_t rtsfr_m5_uart_set_run_id(const char *run_id)
{
    size_t index;
    size_t length;
    if (run_id == NULL)
    {
        return RTSFR_ERR_INVALID;
    }
    length = strlen(run_id);
    if ((length == 0U) || (length >= sizeof(g_run_id)))
    {
        return RTSFR_ERR_INVALID;
    }
    for (index = 0U; index < length; index++)
    {
        if (!(((run_id[index] >= 'a') && (run_id[index] <= 'z')) ||
              ((run_id[index] >= 'A') && (run_id[index] <= 'Z')) ||
              ((run_id[index] >= '0') && (run_id[index] <= '9')) ||
              (run_id[index] == '_') || (run_id[index] == '-')))
        {
            return RTSFR_ERR_INVALID;
        }
    }
    (void)memcpy(g_run_id, run_id, length + 1U);
    return RTSFR_OK;
}

void rtsfr_m5_uart_log(const rtsfr_trace_event_t *event)
{
    char line[224];
    if (event == NULL)
    {
        return;
    }
    if (g_format == RTSFR_UART_FORMAT_TERMINAL)
    {
        prv_log_terminal(event);
        return;
    }
    (void)snprintf(line, sizeof(line),
                   "%s,%s,%u,%lu,%lu,%u,%u,%s,%u,%lu,%lu\r\n",
                   g_run_id, RTSFR_M5_VERSION,
                   (unsigned int)event->test_id,
                   (unsigned long)event->tick,
                   (unsigned long)event->event_id,
                   (unsigned int)event->task_id,
                   (unsigned int)event->generation,
                   rtsfr_trace_name(event->event),
                   (unsigned int)event->reason,
                   (unsigned long)event->value,
                   (unsigned long)event->cycles);
    rtsfr_m5_uart_write(line);
}

void rtsfr_m5_uart_set_format(rtsfr_uart_format_t format)
{
    g_format = format;
}

rtsfr_uart_format_t rtsfr_m5_uart_get_format(void)
{
    return g_format;
}

void rtsfr_m5_uart_write_format_header(void)
{
    if (g_format == RTSFR_UART_FORMAT_CSV)
    {
        rtsfr_m5_uart_write(
            "run_id,version,test_id,tick,event_id,task_id,generation,event,"
            "reason,value,cycles\r\n");
    }
    else
    {
        rtsfr_m5_uart_write(
            "\r\n==================================================\r\n"
            " RTSFR TERMINAL DEMO - STM32 FreeRTOS\r\n"
            " Commands: demo d01 | d02 | d03 | d04 | d05\r\n"
            "           status | verify | help | format csv\r\n"
            "==================================================\r\n");
    }
}

uint32_t rtsfr_m5_uart_rx_dropped(void)
{
    uint32_t result;
    taskENTER_CRITICAL();
    result = g_rx_dropped;
    taskEXIT_CRITICAL();
    return result;
}

uint32_t rtsfr_m5_uart_tx_errors(void)
{
    return g_tx_errors;
}
