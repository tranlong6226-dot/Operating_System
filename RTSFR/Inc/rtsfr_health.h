#ifndef RTSFR_HEALTH_H
#define RTSFR_HEALTH_H
#include "rtsfr_types.h"
typedef struct {
    bool valid, scheduler_running, all_tasks_healthy, invariants_verified;
    bool resources_consistent, recovery_in_progress, safe_mode;
    bool safe_outputs_confirmed, reset_requested;
    TickType_t supervisor_tick, recovery_started_tick, recovery_budget_ticks;
    uint32_t supervisor_sequence;
} rtsfr_health_sample_t;
/* Pure gate. Caller consumes sequence only after actually refreshing IWDG. */
bool rtsfr_health_check(const rtsfr_health_sample_t *sample, TickType_t now,
                        TickType_t timeout, uint32_t last_refreshed_sequence);
/* Init before scheduler; supervisor identity cannot be replaced at runtime. */
void rtsfr_health_init(void);
rtsfr_status_t rtsfr_health_bind_supervisor(TaskHandle_t supervisor);
/* Only the bound supervisor may publish its own evidence (task context). */
rtsfr_status_t rtsfr_health_publish(const rtsfr_health_sample_t *sample);
bool rtsfr_watchdog_health_gate(void);
void rtsfr_health_note_refresh(void);
#endif
