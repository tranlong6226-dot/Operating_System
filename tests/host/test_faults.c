#include "fault_scenarios.h"
#include "rtsfr_m5_adapter.h"
#include "rtsfr_trace.h"
#include "m5_test_runner.h"
#include "mock_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned int reported;
static rtsfr_status_t validate(uint8_t id, uint16_t generation, bool caller)
{
    (void)caller; return (id == 1U && generation == 1U) ? RTSFR_OK : RTSFR_ERR_STALE;
}
static rtsfr_status_t report(uint8_t id, uint16_t generation, uint32_t event)
{
    (void)id; (void)generation; assert(event != 0U); ++reported; return RTSFR_OK;
}
int main(void)
{
    rtsfr_m5_bindings_t b;
    mock_reset(); rtsfr_trace_init(); rtsfr_m5_adapter_init();
    (void)memset(&b, 0, sizeof(b)); b.validate_instance = validate; b.report_fault = report;
    assert(rtsfr_fault_arm(1U, 1U, 1U) == RTSFR_ERR_NOT_READY);
    assert(rtsfr_m5_bind(&b) == RTSFR_OK);
#if RTSFR_ENABLE_FAULT_INJECTION
    assert(rtsfr_fault_arm(1U, 1U, 2U) == RTSFR_ERR_STALE);
    assert(rtsfr_fault_arm(1U, 1U, 1U) == RTSFR_OK);
    assert(rtsfr_fault_checkpoint(1U, 2U, M5_POINT_WORK));
    assert(!rtsfr_fault_checkpoint(1U, 1U, M5_POINT_WORK));
    assert(rtsfr_fault_correlation(1U, 1U) != 0U);
    assert(rtsfr_fault_arm(1U, 1U, 1U) == RTSFR_ERR_BUSY);
    rtsfr_fault_disarm(); assert(rtsfr_fault_checkpoint(1U, 1U, M5_POINT_WORK));
    assert(rtsfr_fault_arm(3U, 1U, 1U) == RTSFR_OK);
    assert(rtsfr_fault_checkpoint(1U, 1U, M5_POINT_WORK) && reported == 1U);
    assert(rtsfr_m5_test_start(2U) == RTSFR_ERR_NOT_READY);
#else
    assert(rtsfr_fault_arm(1U, 1U, 1U) == RTSFR_ERR_NOT_READY);
    assert(rtsfr_fault_checkpoint(1U, 1U, M5_POINT_MUTEX_HELD));
    assert(rtsfr_fault_correlation(1U, 1U) == 0U);
    assert(reported == 0U);
#endif
    assert(mock_depth == 0U);
    puts("PASS faults: missing dependencies, generation guard, release gating, no fabricated test pass");
    return 0;
}
