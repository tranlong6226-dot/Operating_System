#include "rtsfr_verify.h"
#include "mock_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static rtsfr_verify_snapshot_t good(void)
{
    rtsfr_verify_snapshot_t s;
    (void)memset(&s, 0, sizeof(s));
    s.registry_complete = s.resources_complete = true; s.task_count = 1U;
    s.tasks[0].task_id = 1U; s.tasks[0].generation = 1U; s.tasks[0].active = true;
    s.tasks[0].resources_safe = true; s.tasks[0].retry_limit = 2U;
    s.resource_count = 1U; s.resources[0].resource_id = 1U;
    s.resources[0].owner_id = 1U; s.resources[0].owner_generation = 1U;
    s.resources[0].state = M5_RESOURCE_OWNED; return s;
}
int main(void)
{
    rtsfr_verify_snapshot_t s = good();
    rtsfr_verify_report_t r;
    mock_reset();
    assert(rtsfr_verify_check(&s, &r) == RTSFR_OK);
    s.resources_complete = false;
    assert(rtsfr_verify_check(&s, &r) == RTSFR_ERR_NOT_READY);
    s = good(); s.task_count = 4U;
    assert(rtsfr_verify_check(&s, &r) == RTSFR_ERR_UNSAFE);
    s = good(); s.task_count = 2U; s.tasks[1] = s.tasks[0];
    assert(rtsfr_verify_check(&s, &r) == RTSFR_ERR_UNSAFE && r.reason == M5_INVARIANT_DUPLICATE_TASK);
    s = good(); s.resources[0].owner_generation = 2U;
    assert(rtsfr_verify_check(&s, &r) == RTSFR_ERR_UNSAFE && r.reason == M5_INVARIANT_OWNER);
    s = good(); s.resources[0].state = M5_RESOURCE_FREE;
    assert(rtsfr_verify_check(&s, &r) == RTSFR_ERR_UNSAFE);
    s = good(); s.tasks[0].restarting = true; s.tasks[0].resources_safe = false;
    assert(rtsfr_verify_check(&s, &r) == RTSFR_ERR_UNSAFE && r.reason == M5_INVARIANT_UNSAFE_RESTART);
    s = good(); s.tasks[0].recovery_ok = true;
    assert(rtsfr_verify_check(&s, &r) == RTSFR_ERR_UNSAFE && r.reason == M5_INVARIANT_EARLY_SUCCESS);
    s = good(); s.tasks[0].restarting = true; s.tasks[0].retries = 3U;
    assert(rtsfr_verify_check(&s, &r) == RTSFR_ERR_UNSAFE && r.reason == M5_INVARIANT_RETRY_LIMIT);
    s = good(); s.resource_count = 2U; s.resources[1] = s.resources[0];
    assert(rtsfr_verify_check(&s, &r) == RTSFR_ERR_UNSAFE && r.reason == M5_INVARIANT_RESOURCE_ID);
    rtsfr_verify_bind(NULL); assert(!rtsfr_verify_invariants());
    assert(rtsfr_verify_last_report().reason == M5_INVARIANT_MISSING_DATA);
    assert(mock_depth == 0U);
    puts("PASS verify: missing data, bounds, ownership, generation, unsafe restart, early success, retry");
    return 0;
}
