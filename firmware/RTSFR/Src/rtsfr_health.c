#include "rtsfr_health.h"
#include "rtsfr_m5_config.h"
#include <string.h>
static TaskHandle_t supervisor_handle;
static rtsfr_health_sample_t health;
static uint32_t refreshed_sequence;
bool rtsfr_health_check(const rtsfr_health_sample_t *s, TickType_t now,
                        TickType_t timeout, uint32_t last)
{
    if ((s == NULL) || !s->valid || !s->scheduler_running || s->reset_requested ||
        !s->invariants_verified || !s->resources_consistent || (timeout == 0U) ||
        (s->supervisor_sequence == 0U) || (s->supervisor_sequence == last) ||
        ((uint32_t)(s->supervisor_sequence - last) > 0x7FFFFFFFUL) ||
        ((TickType_t)(now - s->supervisor_tick) > timeout)) return false;
    if (s->safe_mode) return s->safe_outputs_confirmed;
    if (s->recovery_in_progress)
        return (s->recovery_budget_ticks != 0U) &&
            ((TickType_t)(now - s->recovery_started_tick) <= s->recovery_budget_ticks);
    return s->all_tasks_healthy;
}
void rtsfr_health_init(void)
{
    (void)memset(&health, 0, sizeof(health)); supervisor_handle = NULL;
    refreshed_sequence = 0U;
}
rtsfr_status_t rtsfr_health_bind_supervisor(TaskHandle_t supervisor)
{
    rtsfr_status_t result = RTSFR_OK;
    if (supervisor == NULL) return RTSFR_ERR_INVALID;
    taskENTER_CRITICAL();
    if ((supervisor_handle != NULL) && (supervisor_handle != supervisor)) result = RTSFR_ERR_BUSY;
    else supervisor_handle = supervisor;
    taskEXIT_CRITICAL();
    return result;
}
rtsfr_status_t rtsfr_health_publish(const rtsfr_health_sample_t *sample)
{
    if (sample == NULL) return RTSFR_ERR_INVALID;
    if ((supervisor_handle == NULL) || (xTaskGetCurrentTaskHandle() != supervisor_handle))
        return RTSFR_ERR_NOT_READY;
    taskENTER_CRITICAL(); health = *sample; taskEXIT_CRITICAL();
    return RTSFR_OK;
}
bool rtsfr_watchdog_health_gate(void)
{
    bool result;
    taskENTER_CRITICAL();
    result = rtsfr_health_check(&health, xTaskGetTickCount(),
                                 pdMS_TO_TICKS(RTSFR_HEALTH_TIMEOUT_MS), refreshed_sequence);
    taskEXIT_CRITICAL();
    return result;
}
void rtsfr_health_note_refresh(void)
{
    taskENTER_CRITICAL(); refreshed_sequence = health.supervisor_sequence; taskEXIT_CRITICAL();
}
