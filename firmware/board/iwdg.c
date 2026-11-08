#include "iwdg.h"
#include "rtsfr_m5_config.h"
#include "rtsfr_health.h"
#include "rtsfr_trace.h"
#include "stm32f1xx.h"
static uint32_t reset_cause;
static TaskHandle_t refresh_owner;
static bool started;
void rtsfr_iwdg_capture_reset_cause(void)
{
    reset_cause = RCC->CSR; RCC->CSR |= RCC_CSR_RMVF;
}
uint32_t rtsfr_iwdg_reset_cause(void) { return reset_cause; }
bool rtsfr_iwdg_was_reset(void) { return (reset_cause & RCC_CSR_IWDGRSTF) != 0U; }
bool rtsfr_iwdg_started(void) { return started; }
static rtsfr_status_t start_hardware(void)
{
    uint32_t attempts = 2000000U;
    if (!RTSFR_IWDG_CONFIG_CONFIRMED) return RTSFR_ERR_NOT_READY;
    if ((RTSFR_IWDG_PRESCALER_CODE > 6U) || (RTSFR_IWDG_RELOAD > 4095U)) return RTSFR_ERR_INVALID;
    if (started) return RTSFR_ERR_BUSY;
    RCC->CSR |= RCC_CSR_LSION;
    while ((RCC->CSR & RCC_CSR_LSIRDY) == 0U)
        if (--attempts == 0U) return RTSFR_ERR_TIMEOUT;
    IWDG->KR = 0x5555U;
    IWDG->PR = RTSFR_IWDG_PRESCALER_CODE; IWDG->RLR = RTSFR_IWDG_RELOAD;
    attempts = 2000000U;
    while (IWDG->SR != 0U)
        if (--attempts == 0U) return RTSFR_ERR_TIMEOUT;
    IWDG->KR = 0xAAAAU; IWDG->KR = 0xCCCCU;
    started = true; refresh_owner = xTaskGetCurrentTaskHandle();
    rtsfr_m5_emit(M5_WATCHDOG_STARTED, 0U, 0U, 0U, 0U, 0U, RTSFR_IWDG_RELOAD);
    return RTSFR_OK;
}
rtsfr_status_t rtsfr_iwdg_start(void)
{
    if (!rtsfr_watchdog_health_gate()) return RTSFR_ERR_NOT_READY;
    return start_hardware();
}
bool rtsfr_iwdg_poll(void)
{
    bool refreshed = false;
    if (!started || (xTaskGetCurrentTaskHandle() != refresh_owner)) return false;
    /* Atomic gate + write + sequence consumption prevent replay/races. */
    taskENTER_CRITICAL();
    if (rtsfr_watchdog_health_gate()) {
        IWDG->KR = 0xAAAAU; rtsfr_health_note_refresh(); refreshed = true;
    }
    taskEXIT_CRITICAL();
    return refreshed;
}
rtsfr_status_t rtsfr_iwdg_board_reset_test(void)
{
#if RTSFR_ENABLE_FAULT_INJECTION
    rtsfr_status_t status = start_hardware();
    if (status != RTSFR_OK) return status;
    __disable_irq();
    for (;;) { __NOP(); } /* IWDG must reset actual hardware. */
#else
    return RTSFR_ERR_NOT_READY;
#endif
}
