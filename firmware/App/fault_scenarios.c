#include "fault_scenarios.h"

#include "m5_test_runner.h"
#include "rtsfr_m5_adapter.h"
#include "rtsfr_m5_config.h"
#include "rtsfr_trace.h"

#if RTSFR_ENABLE_FAULT_INJECTION
static struct
{
    bool armed;
    bool triggered;
    uint8_t scenario;
    uint8_t task_id;
    uint16_t generation;
    uint32_t event_id;
} g_fault;
static uint32_t g_next_event_id = 0x80000000UL;
#endif

rtsfr_status_t rtsfr_fault_arm(uint8_t scenario,
                               uint8_t task_id,
                               uint16_t generation)
{
#if RTSFR_ENABLE_FAULT_INJECTION
    rtsfr_status_t status;
    if ((scenario < M5_FAULT_STOP_PROGRESS) ||
        (scenario > M5_FAULT_SELF_REPORT_STALL))
    {
        return RTSFR_ERR_INVALID;
    }
    status = rtsfr_m5_validate(task_id, generation, false);
    if (status != RTSFR_OK)
    {
        return status;
    }
    taskENTER_CRITICAL();
    if (g_fault.armed)
    {
        taskEXIT_CRITICAL();
        return RTSFR_ERR_BUSY;
    }
    g_fault.scenario = scenario;
    g_fault.task_id = task_id;
    g_fault.generation = generation;
    g_fault.event_id = g_next_event_id++;
    if (g_next_event_id == 0U)
    {
        g_next_event_id = 0x80000000UL;
    }
    g_fault.triggered = false;
    g_fault.armed = true;
    taskEXIT_CRITICAL();
    return RTSFR_OK;
#else
    (void)scenario;
    (void)task_id;
    (void)generation;
    return RTSFR_ERR_NOT_READY;
#endif
}

void rtsfr_fault_disarm(void)
{
#if RTSFR_ENABLE_FAULT_INJECTION
    taskENTER_CRITICAL();
    g_fault.armed = false;
    g_fault.triggered = false;
    taskEXIT_CRITICAL();
#endif
}

uint32_t rtsfr_fault_correlation(uint8_t task_id, uint16_t generation)
{
    uint32_t result = 0U;
#if RTSFR_ENABLE_FAULT_INJECTION
    taskENTER_CRITICAL();
    if (g_fault.triggered && (g_fault.task_id == task_id) &&
        (g_fault.generation == generation))
    {
        result = g_fault.event_id;
    }
    taskEXIT_CRITICAL();
#else
    (void)task_id;
    (void)generation;
#endif
    return result;
}

bool rtsfr_fault_checkpoint(uint8_t task_id,
                            uint16_t generation,
                            rtsfr_fault_point_t point)
{
#if RTSFR_ENABLE_FAULT_INJECTION
    uint8_t scenario;
    uint32_t event_id;
    bool first;
    if (rtsfr_m5_validate(task_id, generation, true) != RTSFR_OK)
    {
        return true;
    }
    taskENTER_CRITICAL();
    if (!g_fault.armed || (g_fault.task_id != task_id) ||
        (g_fault.generation != generation))
    {
        taskEXIT_CRITICAL();
        return true;
    }
    scenario = g_fault.scenario;
    event_id = g_fault.event_id;
    if (((scenario == M5_FAULT_BUFFER_HELD) &&
         (point != M5_POINT_BUFFER_HELD)) ||
        ((scenario == M5_FAULT_MUTEX_HELD) &&
         (point != M5_POINT_MUTEX_HELD)) ||
        ((scenario == M5_FAULT_SELF_REPORT_STALL) &&
         (point != M5_POINT_WORK)))
    {
        taskEXIT_CRITICAL();
        return true;
    }
    first = !g_fault.triggered;
    g_fault.triggered = true;
    if ((scenario == M5_FAULT_SELF_REPORT) ||
        (scenario == M5_FAULT_SELF_REPORT_STALL) ||
        (scenario == M5_FAULT_EXPECTED_WAIT))
    {
        g_fault.armed = false;
    }
    taskEXIT_CRITICAL();
    if (first)
    {
        rtsfr_m5_emit(M5_INJECT, rtsfr_m5_test_active(), event_id,
                      task_id, generation, scenario, 0U);
    }
    if ((scenario == M5_FAULT_BUSY_LOOP) ||
        (scenario == M5_FAULT_BUFFER_HELD) ||
        (scenario == M5_FAULT_MUTEX_HELD))
    {
        for (;;)
        {
            taskYIELD();
        }
    }
    if ((scenario == M5_FAULT_SELF_REPORT) ||
        (scenario == M5_FAULT_SELF_REPORT_STALL))
    {
        rtsfr_status_t status =
            rtsfr_m5_report_fault(task_id, generation, event_id);
        if (status != RTSFR_OK)
        {
            rtsfr_m5_emit(M5_NOT_READY, rtsfr_m5_test_active(), event_id,
                          task_id, generation, (uint16_t)status, scenario);
        }
        return scenario == M5_FAULT_SELF_REPORT;
    }
    if (scenario == M5_FAULT_EXPECTED_WAIT)
    {
        vTaskDelay(pdMS_TO_TICKS(1000U));
        return true;
    }
    return false;
#else
    (void)task_id;
    (void)generation;
    (void)point;
    return true;
#endif
}

void rtsfr_test_inject(uint8_t test_id)
{
    (void)rtsfr_m5_test_start(test_id);
}
