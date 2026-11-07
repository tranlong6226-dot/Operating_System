/* Real M4 implementation with deterministic RTOS doubles. Not a board/TC PASS. */
#include "rtsfr_recovery.h"
#include "rtsfr_m5_adapter.h"
#include "rtsfr_trace.h"
#include "mock_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static rtsfr_resource_summary_t resource;
static rtsfr_status_t snapshot_status, cleanup_status;
static unsigned int cleanup_calls;
static void entry(void *arg) { (void)arg; }
static rtsfr_status_t snapshot(uint8_t id, uint16_t generation, rtsfr_resource_summary_t *out)
{
    (void)id; (void)generation;
    if (snapshot_status == RTSFR_OK) *out = resource;
    return snapshot_status;
}
static rtsfr_status_t cleanup(uint8_t id, uint16_t generation)
{
    (void)id; (void)generation; ++cleanup_calls;
    if (cleanup_status == RTSFR_OK) resource.managed_buffers_owned = 0U;
    return cleanup_status;
}
static rtsfr_decision_t setup(void)
{
    rtsfr_recovery_hooks_t hooks;
    rtsfr_recovery_task_config_t config;
    rtsfr_decision_t d = {1U, 1U, 1U, RTSFR_ACT_RESTART, 0U};
    mock_reset(); rtsfr_trace_init(); rtsfr_m5_adapter_init();
    (void)memset(&resource, 0, sizeof(resource));
    resource.task_id = 1U; resource.generation = 1U; resource.can_reclaim_all = true;
    snapshot_status = cleanup_status = RTSFR_OK; cleanup_calls = 0U;
    (void)memset(&hooks, 0, sizeof(hooks));
    hooks.resource_snapshot = snapshot; hooks.resource_cleanup = cleanup;
    hooks.trace = rtsfr_m5_on_recovery_trace; hooks.record_result = rtsfr_m5_on_recovery_result;
    rtsfr_recovery_init(&hooks);
    (void)memset(&config, 0, sizeof(config));
    config.task_id = 1U; config.initial_generation = 1U; config.entry = entry;
    config.name = "target"; config.stack_depth = 128U; config.priority = 1U;
    config.verify_timeout_ticks = 50U;
    assert(rtsfr_recovery_register_task(&config, (void *)(uintptr_t)2U) == RTSFR_OK);
    return d;
}
static void to_verify(void)
{
    unsigned int guard = 12U;
    while (rtsfr_recovery_get_state() != RTSFR_REC_STATE_VERIFYING && guard-- != 0U)
        assert(rtsfr_recovery_process() == RTSFR_OK);
    assert(rtsfr_recovery_get_state() == RTSFR_REC_STATE_VERIFYING);
}
int main(void)
{
    rtsfr_decision_t d;
    rtsfr_recovery_result_t result;
    unsigned int cycles;
    d = setup(); assert(rtsfr_recovery_request(&d) == RTSFR_OK);
    to_verify();
    rtsfr_recovery_on_first_progress(1U, 1U); /* stale progress cannot verify new task */
    assert(rtsfr_recovery_process() == RTSFR_ERR_BUSY);
    rtsfr_recovery_on_first_progress(1U, 2U);
    assert(rtsfr_recovery_process() == RTSFR_OK);
    assert(rtsfr_recovery_get_result(1U, &result) == RTSFR_OK && result.outcome == RTSFR_RECOVERY_OK);
    assert(mock_creates == 1U && mock_deletes == 1U);
    assert(rtsfr_recovery_request(&d) == RTSFR_ERR_STALE);

    d = setup(); resource.mutex_held_count = 1U;
    assert(rtsfr_recovery_request(&d) == RTSFR_OK);
    assert(rtsfr_recovery_process() == RTSFR_OK);
    assert(rtsfr_recovery_process() == RTSFR_ERR_UNSAFE);
    assert(rtsfr_safe_mode_is_active() && mock_deletes == 0U && mock_creates == 0U && cleanup_calls == 0U);

    d = setup(); resource.unknown_resource_count = 1U;
    assert(rtsfr_recovery_request(&d) == RTSFR_OK);
    (void)rtsfr_recovery_process(); assert(rtsfr_recovery_process() == RTSFR_ERR_UNSAFE);

    d = setup(); resource.has_inflight_io = true;
    assert(rtsfr_recovery_request(&d) == RTSFR_OK);
    (void)rtsfr_recovery_process(); assert(rtsfr_recovery_process() == RTSFR_ERR_UNSAFE);

    d = setup(); snapshot_status = RTSFR_ERR_NOT_READY;
    assert(rtsfr_recovery_request(&d) == RTSFR_OK);
    (void)rtsfr_recovery_process(); assert(rtsfr_recovery_process() == RTSFR_ERR_UNSAFE);

    d = setup(); resource.managed_buffers_owned = 1U;
    assert(rtsfr_recovery_request(&d) == RTSFR_OK); to_verify();
    assert(cleanup_calls == 1U && resource.managed_buffers_owned == 0U);

    d = setup(); resource.managed_buffers_owned = 1U; cleanup_status = RTSFR_ERR_UNSAFE;
    assert(rtsfr_recovery_request(&d) == RTSFR_OK);
    (void)rtsfr_recovery_process(); (void)rtsfr_recovery_process();
    assert(rtsfr_recovery_process() == RTSFR_ERR_UNSAFE && mock_deletes == 0U);

    d = setup(); mock_create_result = pdFAIL;
    assert(rtsfr_recovery_request(&d) == RTSFR_OK);
    for (cycles = 0U; cycles < 4U; ++cycles) (void)rtsfr_recovery_process();
    assert(rtsfr_recovery_get_result(1U, &result) == RTSFR_OK && result.outcome == RTSFR_RECOVERY_CREATE_FAILED);

    d = setup(); assert(rtsfr_recovery_request(&d) == RTSFR_OK);
    (void)rtsfr_recovery_process();
    assert(rtsfr_recovery_cancel_pending(1U, 1U, 1U) == RTSFR_OK);
    assert(rtsfr_recovery_process() == RTSFR_ERR_INTERNAL);
    assert(mock_resumes == 1U && mock_deletes == 0U && !rtsfr_safe_mode_is_active());

    d = setup(); assert(rtsfr_recovery_request(&d) == RTSFR_OK); to_verify();
    mock_tick = 51U;
    assert(rtsfr_recovery_process() == RTSFR_ERR_TIMEOUT && rtsfr_safe_mode_is_active());

    /* Host stress validates API/lifecycle counts, not heap_4 or actual scheduler. */
    d = setup();
    for (cycles = 0U; cycles < 100U; ++cycles) {
        d.event_id = cycles + 1U; d.generation = (uint16_t)(cycles + 1U);
        resource.generation = d.generation;
        assert(rtsfr_recovery_request(&d) == RTSFR_OK); to_verify();
        rtsfr_recovery_on_first_progress(1U, (uint16_t)(d.generation + 1U));
        assert(rtsfr_recovery_process() == RTSFR_OK);
    }
    assert(mock_creates == 100U && mock_deletes == 100U && mock_depth == 0U);
    puts("PASS recovery contract: safe/unsafe, buffer, missing snapshot, OOM, cancellation, stale progress, timeout, 100 mocked cycles");
    return 0;
}
