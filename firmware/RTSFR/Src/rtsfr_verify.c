#include "rtsfr_verify.h"

#include <string.h>

#include "rtsfr_trace.h"

static rtsfr_verify_provider_fn g_provider;
static rtsfr_verify_report_t g_last_report;

static rtsfr_status_t prv_fail(rtsfr_verify_report_t *report,
                               rtsfr_invariant_reason_t reason,
                               uint16_t index,
                               rtsfr_status_t status)
{
    report->reason = reason;
    report->index = index;
    return status;
}

rtsfr_status_t rtsfr_verify_check(const rtsfr_verify_snapshot_t *snapshot,
                                  rtsfr_verify_report_t *report)
{
    uint8_t index;
    uint8_t previous;
    bool owner_found;

    if ((snapshot == NULL) || (report == NULL))
    {
        return RTSFR_ERR_INVALID;
    }
    report->reason = M5_INVARIANT_OK;
    report->index = 0U;
    if (!snapshot->registry_complete || !snapshot->resources_complete)
    {
        return prv_fail(report, M5_INVARIANT_MISSING_DATA, 0U,
                        RTSFR_ERR_NOT_READY);
    }
    if ((snapshot->task_count == 0U) ||
        (snapshot->task_count > RTSFR_VERIFY_MAX_TASKS) ||
        (snapshot->resource_count > RTSFR_VERIFY_MAX_RESOURCES))
    {
        return prv_fail(report, M5_INVARIANT_BOUNDS, 0U, RTSFR_ERR_UNSAFE);
    }
    for (index = 0U; index < snapshot->task_count; index++)
    {
        const rtsfr_verify_task_t *task = &snapshot->tasks[index];
        if ((task->task_id == 0U) ||
            (task->task_id > RTSFR_VERIFY_MAX_TASKS))
        {
            return prv_fail(report, M5_INVARIANT_TASK_ID, index,
                            RTSFR_ERR_UNSAFE);
        }
        if (task->active && (task->generation == 0U))
        {
            return prv_fail(report, M5_INVARIANT_GENERATION, index,
                            RTSFR_ERR_UNSAFE);
        }
        for (previous = 0U; previous < index; previous++)
        {
            if (snapshot->tasks[previous].task_id == task->task_id)
            {
                return prv_fail(report, M5_INVARIANT_DUPLICATE_TASK, index,
                                RTSFR_ERR_UNSAFE);
            }
        }
        if (task->restarting && !task->resources_safe)
        {
            return prv_fail(report, M5_INVARIANT_UNSAFE_RESTART, index,
                            RTSFR_ERR_UNSAFE);
        }
        if (task->recovery_ok && (!task->active || !task->first_progress))
        {
            return prv_fail(report, M5_INVARIANT_EARLY_SUCCESS, index,
                            RTSFR_ERR_UNSAFE);
        }
        if (task->restarting && (task->retries > task->retry_limit))
        {
            return prv_fail(report, M5_INVARIANT_RETRY_LIMIT, index,
                            RTSFR_ERR_UNSAFE);
        }
    }
    for (index = 0U; index < snapshot->resource_count; index++)
    {
        const rtsfr_verify_resource_t *resource =
            &snapshot->resources[index];
        if (resource->resource_id == 0U)
        {
            return prv_fail(report, M5_INVARIANT_RESOURCE_ID, index,
                            RTSFR_ERR_UNSAFE);
        }
        for (previous = 0U; previous < index; previous++)
        {
            if (snapshot->resources[previous].resource_id ==
                resource->resource_id)
            {
                return prv_fail(report, M5_INVARIANT_RESOURCE_ID, index,
                                RTSFR_ERR_UNSAFE);
            }
        }
        if ((unsigned int)resource->state >
            (unsigned int)M5_RESOURCE_QUARANTINED)
        {
            return prv_fail(report, M5_INVARIANT_RESOURCE_STATE, index,
                            RTSFR_ERR_UNSAFE);
        }
        if (resource->state == M5_RESOURCE_FREE)
        {
            if ((resource->owner_id != 0U) ||
                (resource->owner_generation != 0U))
            {
                return prv_fail(report, M5_INVARIANT_OWNER, index,
                                RTSFR_ERR_UNSAFE);
            }
        }
        else if (resource->state != M5_RESOURCE_QUARANTINED)
        {
            owner_found = false;
            for (previous = 0U; previous < snapshot->task_count; previous++)
            {
                if (snapshot->tasks[previous].active &&
                    (snapshot->tasks[previous].task_id ==
                     resource->owner_id) &&
                    (snapshot->tasks[previous].generation ==
                     resource->owner_generation))
                {
                    owner_found = true;
                }
            }
            if (!owner_found)
            {
                return prv_fail(report, M5_INVARIANT_OWNER, index,
                                RTSFR_ERR_UNSAFE);
            }
        }
    }
    return RTSFR_OK;
}

void rtsfr_verify_bind(rtsfr_verify_provider_fn provider)
{
    g_provider = provider;
}

bool rtsfr_verify_invariants(void)
{
    rtsfr_verify_snapshot_t snapshot;
    rtsfr_status_t status = RTSFR_ERR_NOT_READY;
    (void)memset(&snapshot, 0, sizeof(snapshot));
    g_last_report.reason = M5_INVARIANT_MISSING_DATA;
    g_last_report.index = 0U;
    if (g_provider != NULL)
    {
        status = g_provider(&snapshot);
        if (status == RTSFR_OK)
        {
            status = rtsfr_verify_check(&snapshot, &g_last_report);
        }
    }
    rtsfr_m5_emit((status == RTSFR_OK)
                      ? M5_VERIFY_PASS
                      : ((status == RTSFR_ERR_NOT_READY)
                             ? M5_NOT_READY
                             : M5_VERIFY_FAIL),
                  0U, 0U, 0U, 0U, (uint16_t)g_last_report.reason,
                  g_last_report.index);
    return status == RTSFR_OK;
}

rtsfr_verify_report_t rtsfr_verify_last_report(void)
{
    return g_last_report;
}
