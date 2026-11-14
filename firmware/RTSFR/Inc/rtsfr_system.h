#ifndef RTSFR_SYSTEM_H
#define RTSFR_SYSTEM_H

#include "rtsfr_types.h"

typedef struct
{
    uint32_t task_progress[configRTSFR_MAX_TASKS];
    UBaseType_t task_stack_high_water[configRTSFR_MAX_TASKS];
    uint32_t monitor_polls;
    uint32_t monitor_last_cycles;
    uint32_t monitor_max_cycles;
    uint32_t event_overflows;
    uint16_t resource_anomalies;
    uint16_t successful_recoveries;
    uint16_t unsafe_restarts_prevented;
    uint16_t false_alarms_rolled_back;
} rtsfr_system_metrics_t;

rtsfr_status_t rtsfr_system_init(void);
void rtsfr_system_poll(void);
bool rtsfr_system_is_ready(void);
rtsfr_status_t rtsfr_system_get_metrics(rtsfr_system_metrics_t *out);
rtsfr_status_t rtsfr_system_emit_snapshot(void);

#endif /* RTSFR_SYSTEM_H */
