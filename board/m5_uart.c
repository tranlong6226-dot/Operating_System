#include "m5_uart.h"
#include "main.h"
#include <stdio.h>
#include <string.h>
extern UART_HandleTypeDef huart1;
static char rx[RTSFR_RX_CAPACITY];
static uint16_t head, tail, count;
static uint32_t rx_dropped, tx_errors;
static char run_id[32] = "boot";
void rtsfr_m5_uart_init(void)
{
    head = tail = count = 0U; rx_dropped = tx_errors = 0U;
    HAL_NVIC_SetPriority(USART1_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, 0U);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    SET_BIT(USART1->CR1, USART_CR1_RXNEIE);
}
void rtsfr_m5_uart_irq(void)
{
    uint32_t sr = USART1->SR;
    if ((sr & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) != 0U) {
        char byte = (char)USART1->DR; /* SR then DR clears RX/errors. */
        UBaseType_t mask = taskENTER_CRITICAL_FROM_ISR();
        if ((sr & (USART_SR_ORE | USART_SR_FE | USART_SR_NE)) != 0U) ++rx_dropped;
        else if (count == RTSFR_RX_CAPACITY) ++rx_dropped;
        else { rx[head] = byte; head = (uint16_t)((head + 1U) % RTSFR_RX_CAPACITY); ++count; }
        taskEXIT_CRITICAL_FROM_ISR(mask);
    }
}
bool rtsfr_m5_uart_getchar(char *out)
{
    bool found = false;
    if (out == NULL) return false;
    taskENTER_CRITICAL();
    if (count != 0U) { *out = rx[tail]; tail = (uint16_t)((tail + 1U) % RTSFR_RX_CAPACITY); --count; found = true; }
    taskEXIT_CRITICAL(); return found;
}
void rtsfr_m5_uart_write(const char *text)
{
    if ((text != NULL) && (HAL_UART_Transmit(&huart1, (uint8_t *)(uintptr_t)text,
        (uint16_t)strlen(text), 100U) != HAL_OK)) ++tx_errors;
}
rtsfr_status_t rtsfr_m5_uart_set_run_id(const char *id)
{
    size_t i, n;
    if (id == NULL) return RTSFR_ERR_INVALID;
    n = strlen(id);
    if ((n == 0U) || (n >= sizeof(run_id))) return RTSFR_ERR_INVALID;
    for (i = 0U; i < n; ++i)
        if (!((id[i] >= 'a' && id[i] <= 'z') || (id[i] >= 'A' && id[i] <= 'Z') ||
              (id[i] >= '0' && id[i] <= '9') || id[i] == '_' || id[i] == '-')) return RTSFR_ERR_INVALID;
    (void)memcpy(run_id, id, n + 1U); return RTSFR_OK;
}
void rtsfr_m5_uart_log(const rtsfr_trace_event_t *e)
{
    char line[224];
    if (e == NULL) return;
    (void)snprintf(line, sizeof(line), "%s,%s,%u,%lu,%lu,%u,%u,%s,%u,%lu,%lu\r\n",
        run_id, RTSFR_M5_VERSION, (unsigned int)e->test_id, (unsigned long)e->tick,
        (unsigned long)e->event_id, (unsigned int)e->task_id, (unsigned int)e->generation,
        rtsfr_trace_name(e->event), (unsigned int)e->reason, (unsigned long)e->value,
        (unsigned long)e->cycles);
    rtsfr_m5_uart_write(line);
}
uint32_t rtsfr_m5_uart_rx_dropped(void)
{
    uint32_t result; taskENTER_CRITICAL(); result = rx_dropped; taskEXIT_CRITICAL(); return result;
}
uint32_t rtsfr_m5_uart_tx_errors(void) { return tx_errors; }
