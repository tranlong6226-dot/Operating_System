#include "rtsfr_trace.h"

#include <string.h>

#include "cycle_counter.h"

static rtsfr_trace_event_t g_trace_ring[RTSFR_TRACE_CAPACITY];
static uint32_t g_trace_head;
static uint32_t g_trace_tail;
static uint32_t g_trace_used;
static uint32_t g_trace_dropped;

static void prv_push(const rtsfr_trace_event_t *event)
{
    if (event == NULL)
    {
        return;
    }
    if (g_trace_used == RTSFR_TRACE_CAPACITY)
    {
        if (g_trace_dropped != UINT32_MAX)
        {
            g_trace_dropped++;
        }
        return;
    }
    g_trace_ring[g_trace_head] = *event;
    g_trace_head = (g_trace_head + 1U) % RTSFR_TRACE_CAPACITY;
    g_trace_used++;
}

void rtsfr_trace_init(void)
{
    taskENTER_CRITICAL();
    g_trace_head = 0U;
    g_trace_tail = 0U;
    g_trace_used = 0U;
    g_trace_dropped = 0U;
    taskEXIT_CRITICAL();
}

void rtsfr_trace_event(const rtsfr_trace_event_t *event)
{
    taskENTER_CRITICAL();
    prv_push(event);
    taskEXIT_CRITICAL();
}

void rtsfr_trace_event_from_isr(const rtsfr_trace_event_t *event)
{
    UBaseType_t mask = taskENTER_CRITICAL_FROM_ISR();
    prv_push(event);
    taskEXIT_CRITICAL_FROM_ISR(mask);
}

bool rtsfr_trace_pop(rtsfr_trace_event_t *event)
{
    bool found = false;
    if (event == NULL)
    {
        return false;
    }
    taskENTER_CRITICAL();
    if (g_trace_used > 0U)
    {
        *event = g_trace_ring[g_trace_tail];
        g_trace_tail = (g_trace_tail + 1U) % RTSFR_TRACE_CAPACITY;
        g_trace_used--;
        found = true;
    }
    taskEXIT_CRITICAL();
    return found;
}

uint32_t rtsfr_trace_dropped(void)
{
    uint32_t result;
    taskENTER_CRITICAL();
    result = g_trace_dropped;
    taskEXIT_CRITICAL();
    return result;
}

const char *rtsfr_trace_name(rtsfr_m5_event_kind_t event)
{
    static const char *const names[] = {
        "BOOT", "TASK_CREATE", "TASK_DELETE", "INJECT",
        "FAULT_DETECTED", "RESOURCE_CHECK", "POLICY", "RECOVERY_STEP", "RECOVERY_OK",
        "RECOVERY_FAILED", "FIRST_PROGRESS", "SAFE_MODE", "VERIFY_PASS",
        "VERIFY_FAIL", "NOT_READY", "TEST_STARTED", "TEST_PASS",
        "TEST_FAIL", "TRACE_DROPPED", "WATCHDOG_STARTED", "RESET_CAUSE",
        "STATUS", "SNAPSHOT_BEGIN", "TASK_SNAPSHOT",
        "RESOURCE_SNAPSHOT", "SYSTEM_SNAPSHOT", "SNAPSHOT_END"};
    return ((unsigned int)event < (unsigned int)M5_EVENT_COUNT)
               ? names[event]
               : "UNKNOWN";
}

void rtsfr_m5_emit(rtsfr_m5_event_kind_t event,
                   uint8_t test_id,
                   uint32_t event_id,
                   uint8_t task_id,
                   uint16_t generation,
                   uint16_t reason,
                   uint32_t value)
{
    rtsfr_trace_event_t trace;
    (void)memset(&trace, 0, sizeof(trace));
    trace.tick = xTaskGetTickCount();
    trace.event = event;
    trace.test_id = test_id;
    trace.event_id = event_id;
    trace.task_id = task_id;
    trace.generation = generation;
    trace.reason = reason;
    trace.value = value;
#if RTSFR_M5_MEASURE_TRACE
    if (rtsfr_cycles_ready())
    {
        trace.cycles = rtsfr_cycles_now();
    }
#endif
    rtsfr_trace_event(&trace);
}

void rtsfr_m5_lifecycle(void *handle, unsigned int created)
{
    rtsfr_m5_emit(created ? M5_TASK_CREATE : M5_TASK_DELETE,
                  0U, 0U, 0U, 0U, 0U, (uint32_t)(uintptr_t)handle);
}
