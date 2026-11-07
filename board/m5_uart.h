#ifndef M5_UART_H
#define M5_UART_H
#include "rtsfr_trace.h"
void rtsfr_m5_uart_init(void);
/* Called by USART1 IRQ instead of HAL_UART_IRQHandler while M5 owns RX. */
void rtsfr_m5_uart_irq(void);
/* Bootstrap-task only. Bounded UART transmission is outside kernel hooks. */
bool rtsfr_m5_uart_getchar(char *out);
void rtsfr_m5_uart_write(const char *text);
void rtsfr_m5_uart_log(const rtsfr_trace_event_t *event);
rtsfr_status_t rtsfr_m5_uart_set_run_id(const char *run_id);
uint32_t rtsfr_m5_uart_rx_dropped(void);
uint32_t rtsfr_m5_uart_tx_errors(void);
#endif
