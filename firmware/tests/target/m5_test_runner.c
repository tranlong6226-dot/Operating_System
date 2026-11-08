#include "m5_test_runner.h"
#include "rtsfr_m5_adapter.h"
#include "rtsfr_trace.h"
#include "rtsfr_m5_config.h"
static uint8_t active;
static TickType_t started;
rtsfr_status_t rtsfr_m5_test_start(uint8_t test_id)
{
#if RTSFR_ENABLE_FAULT_INJECTION
    rtsfr_status_t status;
    if ((test_id == 0U) || (test_id > 14U)) return RTSFR_ERR_INVALID;
    if (active != 0U) return RTSFR_ERR_BUSY;
    active = test_id; started = xTaskGetTickCount(); rtsfr_m5_set_test_id(test_id);
    status = rtsfr_m5_start_test(test_id);
    if (status != RTSFR_OK) {
        active = 0U; rtsfr_m5_set_test_id(0U);
        rtsfr_m5_emit(M5_NOT_READY, test_id, 0U, 0U, 0U, (uint16_t)status, 0U);
        return status;
    }
    rtsfr_m5_emit(M5_TEST_STARTED, test_id, 0U, 0U, 0U, 0U, 0U);
    return RTSFR_OK;
#else
    rtsfr_m5_emit(M5_NOT_READY, test_id, 0U, 0U, 0U, RTSFR_ERR_NOT_READY, 0U);
    return RTSFR_ERR_NOT_READY;
#endif
}
rtsfr_status_t rtsfr_m5_test_finish(uint8_t test_id, bool passed, uint16_t reason)
{
    if ((active == 0U) || (test_id != active)) return RTSFR_ERR_STALE;
    rtsfr_m5_emit(passed ? M5_TEST_PASS : M5_TEST_FAIL, test_id, 0U, 0U, 0U,
                   reason, (uint32_t)(TickType_t)(xTaskGetTickCount() - started));
    active = 0U; rtsfr_m5_set_test_id(0U); return RTSFR_OK;
}
void rtsfr_m5_test_poll(void)
{
    /* TC01 needs 5 minutes; stress runs may take longer. Timeout is a failure,
       never inferred success. TC12 may reset without a finish event. */
    if ((active != 0U) && ((TickType_t)(xTaskGetTickCount() - started) > pdMS_TO_TICKS(600000U)))
        (void)rtsfr_m5_test_finish(active, false, RTSFR_ERR_TIMEOUT);
}
uint8_t rtsfr_m5_test_active(void) { return active; }
