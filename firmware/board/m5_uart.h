#ifndef M5_UART_H
#define M5_UART_H

#include "rtsfr_trace.h"

typedef enum
{
    RTSFR_UART_FORMAT_TERMINAL = 0,
    RTSFR_UART_FORMAT_CSV
} rtsfr_uart_format_t;

void rtsfr_m5_uart_init(void);
void rtsfr_m5_uart_irq(void);
bool rtsfr_m5_uart_getchar(char *out);
void rtsfr_m5_uart_write(const char *text);
void rtsfr_m5_uart_log(const rtsfr_trace_event_t *event);
void rtsfr_m5_uart_set_format(rtsfr_uart_format_t format);
rtsfr_uart_format_t rtsfr_m5_uart_get_format(void);
void rtsfr_m5_uart_write_format_header(void);
rtsfr_status_t rtsfr_m5_uart_set_run_id(const char *run_id);
uint32_t rtsfr_m5_uart_rx_dropped(void);
uint32_t rtsfr_m5_uart_tx_errors(void);

#endif /* M5_UART_H */
