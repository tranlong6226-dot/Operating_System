#include "iwdg.h"

#include "m5_uart.h"
#include "rtsfr_health.h"
#include "rtsfr_m5_config.h"
#include "rtsfr_trace.h"
#include "stm32f1xx.h"

static uint32_t g_reset_cause;
static TaskHandle_t g_refresh_owner;
static bool g_started;
static bool g_test12_passed;

#define RTSFR_IWDG_TEST12_MAGIC 0x125AU
#define RTSFR_IWDG_STARTUP_TIMEOUT_MS 100U
#define RTSFR_IWDG_UPDATE_FLAGS (IWDG_SR_PVU | IWDG_SR_RVU)

static void prv_backup_access(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN;
    PWR->CR |= PWR_CR_DBP;
}

void rtsfr_iwdg_capture_reset_cause(void)
{
    g_reset_cause = RCC->CSR;
    prv_backup_access();
    g_test12_passed =
        ((g_reset_cause & RCC_CSR_IWDGRSTF) != 0U) &&
        ((uint16_t)BKP->DR1 == RTSFR_IWDG_TEST12_MAGIC);
    BKP->DR1 = 0U;
    RCC->CSR |= RCC_CSR_RMVF;
}

uint32_t rtsfr_iwdg_reset_cause(void) { return g_reset_cause; }
bool rtsfr_iwdg_was_reset(void)
{
    return (g_reset_cause & RCC_CSR_IWDGRSTF) != 0U;
}
bool rtsfr_iwdg_test12_passed(void) { return g_test12_passed; }
bool rtsfr_iwdg_started(void) { return g_started; }

static rtsfr_status_t prv_start_hardware(void)
{
    uint32_t started_tick;
    if (!RTSFR_IWDG_CONFIG_CONFIRMED)
    {
        return RTSFR_ERR_NOT_READY;
    }
    if ((RTSFR_IWDG_PRESCALER_CODE > 6U) ||
        (RTSFR_IWDG_RELOAD > 4095U))
    {
        return RTSFR_ERR_INVALID;
    }
    if (g_started)
    {
        return RTSFR_ERR_BUSY;
    }
    RCC->CSR |= RCC_CSR_LSION;
    started_tick = HAL_GetTick();
    while ((RCC->CSR & RCC_CSR_LSIRDY) == 0U)
    {
        if ((uint32_t)(HAL_GetTick() - started_tick) >
            RTSFR_IWDG_STARTUP_TIMEOUT_MS)
        {
            return RTSFR_ERR_TIMEOUT;
        }
    }

    /* STM32F1 requires the IWDG to be running while PR/RLR updates are
       transferred through the LSI clock domain. Once enabled, IWDG cannot
       be stopped until the next reset. */
    IWDG->KR = 0xCCCCU;
    IWDG->KR = 0x5555U;
    IWDG->PR = RTSFR_IWDG_PRESCALER_CODE;
    IWDG->RLR = RTSFR_IWDG_RELOAD;
    started_tick = HAL_GetTick();
    while ((IWDG->SR & RTSFR_IWDG_UPDATE_FLAGS) != 0U)
    {
        if ((uint32_t)(HAL_GetTick() - started_tick) >
            RTSFR_IWDG_STARTUP_TIMEOUT_MS)
        {
            return RTSFR_ERR_TIMEOUT;
        }
    }
    IWDG->KR = 0xAAAAU;
    g_started = true;
    g_refresh_owner = xTaskGetCurrentTaskHandle();
    rtsfr_m5_emit(M5_WATCHDOG_STARTED, 0U, 0U, 0U, 0U, 0U,
                  RTSFR_IWDG_RELOAD);
    return RTSFR_OK;
}

rtsfr_status_t rtsfr_iwdg_start(void)
{
    if (!rtsfr_watchdog_health_gate())
    {
        return RTSFR_ERR_NOT_READY;
    }
    return prv_start_hardware();
}

bool rtsfr_iwdg_poll(void)
{
    bool refreshed = false;
    if (!g_started || (xTaskGetCurrentTaskHandle() != g_refresh_owner))
    {
        return false;
    }
    taskENTER_CRITICAL();
    if (rtsfr_watchdog_health_gate())
    {
        IWDG->KR = 0xAAAAU;
        rtsfr_health_note_refresh();
        refreshed = true;
    }
    taskEXIT_CRITICAL();
    return refreshed;
}

rtsfr_status_t rtsfr_iwdg_board_reset_test(void)
{
#if RTSFR_ENABLE_FAULT_INJECTION
    rtsfr_trace_event_t event;
    rtsfr_status_t status = prv_start_hardware();
    if (status != RTSFR_OK)
    {
        return status;
    }
    while (rtsfr_trace_pop(&event))
    {
        rtsfr_m5_uart_log(&event);
    }
    prv_backup_access();
    BKP->DR1 = RTSFR_IWDG_TEST12_MAGIC;
    __disable_irq();
    for (;;)
    {
        __NOP();
    }
#else
    return RTSFR_ERR_NOT_READY;
#endif
}
