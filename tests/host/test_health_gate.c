#include "rtsfr_health.h"
#include "mock_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static rtsfr_health_sample_t good(void)
{
    rtsfr_health_sample_t s;
    (void)memset(&s, 0, sizeof(s));
    s.valid = s.scheduler_running = s.all_tasks_healthy = s.invariants_verified = s.resources_consistent = true;
    s.supervisor_tick = 100U; s.supervisor_sequence = 1U; return s;
}
int main(void)
{
    rtsfr_health_sample_t s = good();
    mock_reset(); rtsfr_health_init();
    assert(!rtsfr_watchdog_health_gate());
    assert(rtsfr_health_check(&s, 110U, 500U, 0U));
    assert(!rtsfr_health_check(&s, 110U, 500U, 1U));
    assert(!rtsfr_health_check(&s, 110U, 500U, 2U));
    assert(!rtsfr_health_check(&s, 601U, 500U, 0U));
    s.reset_requested = true; assert(!rtsfr_health_check(&s, 110U, 500U, 0U));
    s = good(); s.resources_consistent = false; assert(!rtsfr_health_check(&s, 110U, 500U, 0U));
    s = good(); s.safe_mode = true; assert(!rtsfr_health_check(&s, 110U, 500U, 0U));
    s.safe_outputs_confirmed = true; assert(rtsfr_health_check(&s, 110U, 500U, 0U));
    s = good(); s.recovery_in_progress = true; s.recovery_started_tick = 90U; s.recovery_budget_ticks = 50U;
    assert(rtsfr_health_check(&s, 120U, 500U, 0U));
    assert(!rtsfr_health_check(&s, 141U, 500U, 0U));
    s = good(); s.supervisor_tick = UINT32_MAX - 10U;
    assert(rtsfr_health_check(&s, 10U, 500U, 0U));
    s = good();
    assert(rtsfr_health_publish(&s) == RTSFR_ERR_NOT_READY);
    assert(rtsfr_health_bind_supervisor(mock_current) == RTSFR_OK);
    assert(rtsfr_health_publish(&s) == RTSFR_OK); mock_tick = 110U;
    assert(rtsfr_watchdog_health_gate()); rtsfr_health_note_refresh(); assert(!rtsfr_watchdog_health_gate());
    mock_current = (void *)(uintptr_t)2U;
    assert(rtsfr_health_publish(&s) == RTSFR_ERR_NOT_READY);
    assert(rtsfr_health_bind_supervisor(mock_current) == RTSFR_ERR_BUSY);
    assert(mock_depth == 0U);
    puts("PASS health: missing evidence, replay, stale heartbeat, safe mode, bounded recovery, wrap, caller identity");
    return 0;
}
