#ifndef RTSFR_HEALTH_H
#define RTSFR_HEALTH_H

#include "rtsfr_types.h"

typedef struct
{
    bool valid;
    bool scheduler_running;
    bool all_tasks_healthy;
    bool invariants_verified;
    bool resources_consistent;
    bool recovery_in_progress;
    bool safe_mode;
    bool safe_outputs_confirmed;
    bool reset_requested;
    TickType_t supervisor_tick;
    TickType_t recovery_started_tick;
    TickType_t recovery_budget_ticks;
    uint32_t supervisor_sequence;
} rtsfr_health_sample_t;

bool rtsfr_health_check(const rtsfr_health_sample_t *sample,
                        TickType_t now,
                        TickType_t timeout,
                        uint32_t last_refreshed_sequence);
void rtsfr_health_init(void);
rtsfr_status_t rtsfr_health_bind_supervisor(TaskHandle_t supervisor);
rtsfr_status_t rtsfr_health_publish(const rtsfr_health_sample_t *sample);
bool rtsfr_watchdog_health_gate(void);
void rtsfr_health_note_refresh(void);

#endif /* RTSFR_HEALTH_H */
