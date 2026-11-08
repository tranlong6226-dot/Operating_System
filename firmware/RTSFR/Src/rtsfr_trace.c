#include "rtsfr_trace.h"
#include <string.h>
#if RTSFR_M5_MEASURE_TRACE
#include "cycle_counter.h"
#endif
static rtsfr_trace_event_t ring[RTSFR_TRACE_CAPACITY];
static uint32_t head, tail, used, dropped;
static void push_locked(const rtsfr_trace_event_t *event)
{
    if (event == NULL) return;
    if (used == RTSFR_TRACE_CAPACITY) {
        if (dropped != UINT32_MAX) ++dropped;
        return; /* Preserve earliest evidence, explicitly count lost events. */
    }
    ring[head] = *event;
    head = (head + 1U) % RTSFR_TRACE_CAPACITY;
    ++used;
}
void rtsfr_trace_init(void)
{
    taskENTER_CRITICAL();
    head = tail = used = dropped = 0U;
    taskEXIT_CRITICAL();
}
void rtsfr_trace_event(const rtsfr_trace_event_t *event)
{
#if RTSFR_M5_MEASURE_TRACE
    uint32_t start = rtsfr_cycles_now();
    uint32_t slot;
    taskENTER_CRITICAL();
    slot = head;
    if ((event != NULL) && (used < RTSFR_TRACE_CAPACITY)) {
        push_locked(event);
        ring[slot].cycles = rtsfr_cycles_ready() ? rtsfr_cycles_elapsed(start, rtsfr_cycles_now()) : 0U;
    } else push_locked(event);
    taskEXIT_CRITICAL();
#else
    taskENTER_CRITICAL(); push_locked(event); taskEXIT_CRITICAL();
#endif
}
void rtsfr_trace_event_from_isr(const rtsfr_trace_event_t *event)
{
    UBaseType_t mask = taskENTER_CRITICAL_FROM_ISR();
    push_locked(event);
    taskEXIT_CRITICAL_FROM_ISR(mask);
}
bool rtsfr_trace_pop(rtsfr_trace_event_t *event)
{
    bool found = false;
    if (event == NULL) return false;
    taskENTER_CRITICAL();
    if (used != 0U) {
        *event = ring[tail]; tail = (tail + 1U) % RTSFR_TRACE_CAPACITY;
        --used; found = true;
    }
    taskEXIT_CRITICAL();
    return found;
}
uint32_t rtsfr_trace_dropped(void)
{
    uint32_t result;
    taskENTER_CRITICAL(); result = dropped; taskEXIT_CRITICAL();
    return result;
}
const char *rtsfr_trace_name(rtsfr_m5_event_kind_t event)
{
    static const char *const names[] = {
        "BOOT", "TASK_CREATE", "TASK_DELETE", "INJECT", "FAULT_DETECTED",
        "POLICY", "RECOVERY_STEP", "RECOVERY_OK", "RECOVERY_FAILED",
        "FIRST_PROGRESS", "SAFE_MODE", "VERIFY_PASS", "VERIFY_FAIL",
        "NOT_READY", "TEST_STARTED", "TEST_PASS", "TEST_FAIL",
        "TRACE_DROPPED", "WATCHDOG_STARTED", "RESET_CAUSE", "STATUS"
    };
    return ((unsigned int)event < (unsigned int)M5_EVENT_COUNT) ? names[event] : "UNKNOWN";
}
void rtsfr_m5_emit(rtsfr_m5_event_kind_t event, uint8_t test_id,
                   uint32_t event_id, uint8_t task_id, uint16_t generation,
                   uint16_t reason, uint32_t value)
{
    rtsfr_trace_event_t e;
    (void)memset(&e, 0, sizeof(e));
    e.tick = xTaskGetTickCount(); e.event = event; e.test_id = test_id;
    e.event_id = event_id; e.task_id = task_id; e.generation = generation;
    e.reason = reason; e.value = value;
    rtsfr_trace_event(&e);
}
void rtsfr_m5_lifecycle(void *handle, unsigned int created)
{
    rtsfr_m5_emit(created ? M5_TASK_CREATE : M5_TASK_DELETE, 0U, 0U,
                   0U, 0U, 0U, (uint32_t)(uintptr_t)handle);
}
