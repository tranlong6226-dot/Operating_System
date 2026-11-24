/*
 * RTSFR kernel supervisor.
 *
 * This translation unit is part of the FreeRTOS kernel build.  It owns the
 * Detection -> Resource -> Policy -> Recovery pipeline; application code only
 * registers monitored work and supplies board/UI callbacks.
 */
#include "rtsfr_kernel.h"

#include <string.h>

#include "rtsfr_detect.h"
#include "rtsfr_policy.h"
#include "rtsfr_recovery.h"
#include "rtsfr_resource.h"
#include "rtsfr_safe_mode.h"

#ifndef configRTSFR_SUPERVISOR_PERIOD_MS
#define configRTSFR_SUPERVISOR_PERIOD_MS 10U
#endif

static RtsfrKernelHooks_t xKernelHooks;
static RtsfrKernelStats_t xKernelStats;
static TaskHandle_t xSupervisorHandle;
static volatile BaseType_t xConfigured;
static volatile BaseType_t xEnabled;
static uint32_t ulObservedOverflowCount;

static void prvHandleFault(const rtsfr_fault_event_t *fault)
{
    rtsfr_task_record_t record;
    rtsfr_resource_summary_t resource;
    rtsfr_decision_t decision;

    if (rtsfr_detect_get_record(fault->task_id, &record) != RTSFR_OK)
    {
        xKernelStats.stale_faults_discarded++;
        return;
    }

    /* A late event must never restart a new generation, nor restart a task
     * that has already made progress after the detector snapshot. */
    if ((record.generation != fault->generation) ||
        ((record.state == RTSFR_TASK_HEALTHY) &&
         (record.progress_seq > fault->evidence)))
    {
        xKernelStats.stale_faults_discarded++;
        return;
    }

    if (rtsfr_resource_snapshot(fault->task_id,
                                fault->generation,
                                &resource) != RTSFR_OK)
    {
        (void)memset(&resource, 0, sizeof(resource));
        resource.task_id = fault->task_id;
        resource.generation = fault->generation;
        resource.unknown_resource_count = 1U;
        resource.can_reclaim_all = false;
    }

    decision = rtsfr_policy_evaluate(fault, &resource);
    if (xKernelHooks.on_decision != NULL)
    {
        xKernelHooks.on_decision(fault, &resource, &decision);
    }

    if (rtsfr_policy_should_invoke_recovery(&decision))
    {
        (void)rtsfr_recovery_request(&decision);
    }
    xKernelStats.faults_consumed++;
}

static void prvKernelCycle(void)
{
    rtsfr_fault_event_t fault;
    rtsfr_recovery_state_t recovery_state;
    uint32_t overflow_count;

    (void)rtsfr_detect_scan(xTaskGetTickCount());
    overflow_count = rtsfr_detect_event_overflow_count();
    if (overflow_count != ulObservedOverflowCount)
    {
        ulObservedOverflowCount = overflow_count;
        xKernelStats.event_overflows = overflow_count;
        (void)rtsfr_safe_mode_enter(0xF0000000UL,
                                    RTSFR_TASK_ID_NONE,
                                    0U,
                                    RTSFR_POL_REASON_EVENT_QUEUE_OVERFLOW);
        if (xKernelHooks.on_event_overflow != NULL)
        {
            xKernelHooks.on_event_overflow(overflow_count);
        }
    }

    while (rtsfr_detect_pop_event(&fault))
    {
        prvHandleFault(&fault);
    }

    recovery_state = rtsfr_recovery_get_state();
    if ((recovery_state >= RTSFR_REC_STATE_QUIESCING) &&
        (recovery_state <= RTSFR_REC_STATE_VERIFYING))
    {
        (void)rtsfr_recovery_process();
    }

    if (xKernelHooks.on_cycle_complete != NULL)
    {
        xKernelHooks.on_cycle_complete();
    }

    xKernelStats.last_cycle_tick = xTaskGetTickCount();
    if (xKernelStats.supervisor_cycles != UINT32_MAX)
    {
        xKernelStats.supervisor_cycles++;
    }
}

BaseType_t xRtsfrKernelConfigure(const RtsfrKernelHooks_t *hooks)
{
    if (hooks == NULL)
    {
        return pdFAIL;
    }

    taskENTER_CRITICAL();
    xKernelHooks = *hooks;
    (void)memset(&xKernelStats, 0, sizeof(xKernelStats));
    ulObservedOverflowCount = rtsfr_detect_event_overflow_count();
    xConfigured = pdTRUE;
    taskEXIT_CRITICAL();
    return pdPASS;
}

BaseType_t xRtsfrKernelEnable(void)
{
    if (xConfigured == pdFALSE)
    {
        return pdFAIL;
    }
    xEnabled = pdTRUE;
    return pdPASS;
}

void vRtsfrKernelDisable(void)
{
    xEnabled = pdFALSE;
}

BaseType_t xRtsfrKernelIsEnabled(void)
{
    return xEnabled;
}

TaskHandle_t xRtsfrKernelGetSupervisorHandle(void)
{
    return xSupervisorHandle;
}

void vRtsfrKernelSetSupervisorHandle(TaskHandle_t handle)
{
    xSupervisorHandle = handle;
}

BaseType_t xRtsfrKernelGetStats(RtsfrKernelStats_t *stats)
{
    if (stats == NULL)
    {
        return pdFAIL;
    }
    taskENTER_CRITICAL();
    *stats = xKernelStats;
    taskEXIT_CRITICAL();
    return pdPASS;
}

void vRtsfrKernelSupervisorTask(void *argument)
{
    TickType_t last_wake;
    (void)argument;

    xSupervisorHandle = xTaskGetCurrentTaskHandle();
    last_wake = xTaskGetTickCount();
    for (;;)
    {
        if (xEnabled != pdFALSE)
        {
            prvKernelCycle();
        }
        vTaskDelayUntil(&last_wake,
                        pdMS_TO_TICKS(configRTSFR_SUPERVISOR_PERIOD_MS));
    }
}
