#ifndef RTSFR_KERNEL_H
#define RTSFR_KERNEL_H

#include "FreeRTOS.h"
#include "task.h"

#include "rtsfr_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*RtsfrKernelDecisionHook_t)(
    const rtsfr_fault_event_t *fault,
    const rtsfr_resource_summary_t *resource,
    const rtsfr_decision_t *decision);
typedef void (*RtsfrKernelOverflowHook_t)(uint32_t overflow_count);
typedef void (*RtsfrKernelCycleHook_t)(void);

typedef struct
{
    RtsfrKernelDecisionHook_t on_decision;
    RtsfrKernelOverflowHook_t on_event_overflow;
    RtsfrKernelCycleHook_t on_cycle_complete;
} RtsfrKernelHooks_t;

typedef struct
{
    uint32_t supervisor_cycles;
    uint32_t faults_consumed;
    uint32_t stale_faults_discarded;
    uint32_t event_overflows;
    TickType_t last_cycle_tick;
} RtsfrKernelStats_t;

BaseType_t xRtsfrKernelConfigure(const RtsfrKernelHooks_t *hooks);
BaseType_t xRtsfrKernelEnable(void);
void vRtsfrKernelDisable(void);
BaseType_t xRtsfrKernelIsEnabled(void);
TaskHandle_t xRtsfrKernelGetSupervisorHandle(void);
BaseType_t xRtsfrKernelGetStats(RtsfrKernelStats_t *stats);
void vRtsfrKernelSetSupervisorHandle(TaskHandle_t handle);

/* Entry point created by vTaskStartScheduler(), not by application code. */
void vRtsfrKernelSupervisorTask(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* RTSFR_KERNEL_H */
