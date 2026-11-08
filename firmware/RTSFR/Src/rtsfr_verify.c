#include "rtsfr_verify.h"
#include "rtsfr_trace.h"
#include <string.h>
static rtsfr_verify_provider_fn snapshot_provider;
static rtsfr_verify_report_t last_report;
static rtsfr_status_t fail(rtsfr_verify_report_t *r, rtsfr_invariant_reason_t reason,
                            uint16_t index, rtsfr_status_t status)
{
    r->reason = reason; r->index = index; return status;
}
rtsfr_status_t rtsfr_verify_check(const rtsfr_verify_snapshot_t *s,
                                 rtsfr_verify_report_t *r)
{
    uint8_t i, j;
    bool owner_found;
    if ((s == NULL) || (r == NULL)) return RTSFR_ERR_INVALID;
    r->reason = M5_INVARIANT_OK; r->index = 0U;
    if (!s->registry_complete || !s->resources_complete)
        return fail(r, M5_INVARIANT_MISSING_DATA, 0U, RTSFR_ERR_NOT_READY);
    if ((s->task_count == 0U) || (s->task_count > RTSFR_VERIFY_MAX_TASKS) ||
        (s->resource_count > RTSFR_VERIFY_MAX_RESOURCES))
        return fail(r, M5_INVARIANT_BOUNDS, 0U, RTSFR_ERR_UNSAFE);
    for (i = 0U; i < s->task_count; ++i) {
        const rtsfr_verify_task_t *t = &s->tasks[i];
        if ((t->task_id == 0U) || (t->task_id > RTSFR_VERIFY_MAX_TASKS))
            return fail(r, M5_INVARIANT_TASK_ID, i, RTSFR_ERR_UNSAFE);
        if (t->active && (t->generation == 0U))
            return fail(r, M5_INVARIANT_GENERATION, i, RTSFR_ERR_UNSAFE);
        for (j = 0U; j < i; ++j)
            if (s->tasks[j].task_id == t->task_id)
                return fail(r, M5_INVARIANT_DUPLICATE_TASK, i, RTSFR_ERR_UNSAFE);
        if (t->restarting && !t->resources_safe)
            return fail(r, M5_INVARIANT_UNSAFE_RESTART, i, RTSFR_ERR_UNSAFE);
        if (t->recovery_ok && (!t->active || !t->first_progress))
            return fail(r, M5_INVARIANT_EARLY_SUCCESS, i, RTSFR_ERR_UNSAFE);
        if (t->restarting && (t->retries > t->retry_limit))
            return fail(r, M5_INVARIANT_RETRY_LIMIT, i, RTSFR_ERR_UNSAFE);
    }
    for (i = 0U; i < s->resource_count; ++i) {
        const rtsfr_verify_resource_t *resource = &s->resources[i];
        if (resource->resource_id == 0U)
            return fail(r, M5_INVARIANT_RESOURCE_ID, i, RTSFR_ERR_UNSAFE);
        for (j = 0U; j < i; ++j)
            if (s->resources[j].resource_id == resource->resource_id)
                return fail(r, M5_INVARIANT_RESOURCE_ID, i, RTSFR_ERR_UNSAFE);
        if ((unsigned int)resource->state > (unsigned int)M5_RESOURCE_QUARANTINED)
            return fail(r, M5_INVARIANT_RESOURCE_STATE, i, RTSFR_ERR_UNSAFE);
        if (resource->state == M5_RESOURCE_FREE) {
            if ((resource->owner_id != 0U) || (resource->owner_generation != 0U))
                return fail(r, M5_INVARIANT_OWNER, i, RTSFR_ERR_UNSAFE);
        } else if (resource->state != M5_RESOURCE_QUARANTINED) {
            owner_found = false;
            for (j = 0U; j < s->task_count; ++j)
                if (s->tasks[j].active && (s->tasks[j].task_id == resource->owner_id) &&
                    (s->tasks[j].generation == resource->owner_generation)) owner_found = true;
            if (!owner_found) return fail(r, M5_INVARIANT_OWNER, i, RTSFR_ERR_UNSAFE);
        }
    }
    return RTSFR_OK;
}
void rtsfr_verify_bind(rtsfr_verify_provider_fn provider) { snapshot_provider = provider; }
bool rtsfr_verify_invariants(void)
{
    rtsfr_verify_snapshot_t snapshot;
    rtsfr_status_t status = RTSFR_ERR_NOT_READY;
    (void)memset(&snapshot, 0, sizeof(snapshot));
    last_report.reason = M5_INVARIANT_MISSING_DATA; last_report.index = 0U;
    if (snapshot_provider != NULL) {
        status = snapshot_provider(&snapshot);
        if (status == RTSFR_OK) status = rtsfr_verify_check(&snapshot, &last_report);
    }
    rtsfr_m5_emit(status == RTSFR_OK ? M5_VERIFY_PASS :
                   (status == RTSFR_ERR_NOT_READY ? M5_NOT_READY : M5_VERIFY_FAIL),
                   0U, 0U, 0U, 0U, (uint16_t)last_report.reason, last_report.index);
    return status == RTSFR_OK;
}
rtsfr_verify_report_t rtsfr_verify_last_report(void) { return last_report; }
