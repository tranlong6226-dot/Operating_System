#include "rtsfr_health.h"

#include <string.h>

#include "rtsfr_m5_config.h"

static TaskHandle_t g_supervisor;
static rtsfr_health_sample_t g_health;
static uint32_t g_refreshed_sequence;

bool rtsfr_health_check(const rtsfr_health_sample_t *sample,
                        TickType_t now,
                        TickType_t timeout,
                        uint32_t last_refreshed_sequence)
{
    if ((sample == NULL) || !sample->valid ||
        !sample->scheduler_running || sample->reset_requested ||
        !sample->invariants_verified || !sample->resources_consistent ||
        (timeout == 0U) || (sample->supervisor_sequence == 0U) ||
        (sample->supervisor_sequence == last_refreshed_sequence) ||
        ((uint32_t)(sample->supervisor_sequence -
                    last_refreshed_sequence) > 0x7FFFFFFFUL) ||
        ((TickType_t)(now - sample->supervisor_tick) > timeout))
    {
        return false;
    }
    if (sample->safe_mode)
    {
        return sample->safe_outputs_confirmed;
    }
    if (sample->recovery_in_progress)
    {
        return (sample->recovery_budget_ticks != 0U) &&
               ((TickType_t)(now - sample->recovery_started_tick) <=
                sample->recovery_budget_ticks);
    }
    return sample->all_tasks_healthy;
}

void rtsfr_health_init(void)
{
    (void)memset(&g_health, 0, sizeof(g_health));
    g_supervisor = NULL;
    g_refreshed_sequence = 0U;
}

rtsfr_status_t rtsfr_health_bind_supervisor(TaskHandle_t supervisor)
{
    rtsfr_status_t status = RTSFR_OK;
    if (supervisor == NULL)
    {
        return RTSFR_ERR_INVALID;
    }
    taskENTER_CRITICAL();
    if ((g_supervisor != NULL) && (g_supervisor != supervisor))
    {
        status = RTSFR_ERR_BUSY;
    }
    else
    {
        g_supervisor = supervisor;
    }
    taskEXIT_CRITICAL();
    return status;
}

rtsfr_status_t rtsfr_health_publish(const rtsfr_health_sample_t *sample)
{
    if (sample == NULL)
    {
        return RTSFR_ERR_INVALID;
    }
    if ((g_supervisor == NULL) ||
        (xTaskGetCurrentTaskHandle() != g_supervisor))
    {
        return RTSFR_ERR_NOT_READY;
    }
    taskENTER_CRITICAL();
    g_health = *sample;
    taskEXIT_CRITICAL();
    return RTSFR_OK;
}

bool rtsfr_watchdog_health_gate(void)
{
    bool result;
    taskENTER_CRITICAL();
    result = rtsfr_health_check(&g_health, xTaskGetTickCount(),
                                pdMS_TO_TICKS(RTSFR_HEALTH_TIMEOUT_MS),
                                g_refreshed_sequence);
    taskEXIT_CRITICAL();
    return result;
}

void rtsfr_health_note_refresh(void)
{
    taskENTER_CRITICAL();
    g_refreshed_sequence = g_health.supervisor_sequence;
    taskEXIT_CRITICAL();
}
