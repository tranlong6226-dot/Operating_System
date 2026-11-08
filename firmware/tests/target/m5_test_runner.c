#include "m5_test_runner.h"

#include "rtsfr_m5_adapter.h"
#include "rtsfr_m5_config.h"
#include "rtsfr_trace.h"

static uint8_t g_active_test;
static TickType_t g_test_started;

rtsfr_status_t rtsfr_m5_test_start(uint8_t test_id)
{
#if RTSFR_ENABLE_FAULT_INJECTION
    rtsfr_status_t status;
    if ((test_id == 0U) || (test_id > 14U))
    {
        return RTSFR_ERR_INVALID;
    }
    if (g_active_test != 0U)
    {
        return RTSFR_ERR_BUSY;
    }
    g_active_test = test_id;
    g_test_started = xTaskGetTickCount();
    rtsfr_m5_set_test_id(test_id);
    status = rtsfr_m5_start_test(test_id);
    if (status != RTSFR_OK)
    {
        g_active_test = 0U;
        rtsfr_m5_set_test_id(0U);
        rtsfr_m5_emit(M5_NOT_READY, test_id, 0U, 0U, 0U,
                      (uint16_t)status, 0U);
        return status;
    }
    rtsfr_m5_emit(M5_TEST_STARTED, test_id, 0U, 0U, 0U, 0U, 0U);
    return RTSFR_OK;
#else
    rtsfr_m5_emit(M5_NOT_READY, test_id, 0U, 0U, 0U,
                  RTSFR_ERR_NOT_READY, 0U);
    return RTSFR_ERR_NOT_READY;
#endif
}

rtsfr_status_t rtsfr_m5_test_finish(uint8_t test_id,
                                    bool passed,
                                    uint16_t reason)
{
    if ((g_active_test == 0U) || (test_id != g_active_test))
    {
        return RTSFR_ERR_STALE;
    }
    rtsfr_m5_emit(passed ? M5_TEST_PASS : M5_TEST_FAIL, test_id, 0U,
                  0U, 0U, reason,
                  (uint32_t)(TickType_t)(xTaskGetTickCount() -
                                          g_test_started));
    g_active_test = 0U;
    rtsfr_m5_set_test_id(0U);
    return RTSFR_OK;
}

void rtsfr_m5_test_poll(void)
{
    if ((g_active_test != 0U) &&
        ((TickType_t)(xTaskGetTickCount() - g_test_started) >
         pdMS_TO_TICKS(600000U)))
    {
        (void)rtsfr_m5_test_finish(g_active_test, false,
                                   RTSFR_ERR_TIMEOUT);
    }
}

uint8_t rtsfr_m5_test_active(void)
{
    return g_active_test;
}
