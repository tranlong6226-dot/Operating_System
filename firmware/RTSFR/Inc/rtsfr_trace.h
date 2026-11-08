#ifndef RTSFR_TRACE_H
#define RTSFR_TRACE_H
#include "rtsfr_types.h"
#include "rtsfr_m5_config.h"
typedef enum {
    M5_BOOT, M5_TASK_CREATE, M5_TASK_DELETE, M5_INJECT, M5_FAULT_DETECTED,
    M5_POLICY, M5_RECOVERY_STEP, M5_RECOVERY_OK, M5_RECOVERY_FAILED,
    M5_FIRST_PROGRESS, M5_SAFE_MODE, M5_VERIFY_PASS, M5_VERIFY_FAIL,
    M5_NOT_READY, M5_TEST_STARTED, M5_TEST_PASS, M5_TEST_FAIL,
    M5_TRACE_DROPPED, M5_WATCHDOG_STARTED, M5_RESET_CAUSE, M5_STATUS,
    M5_EVENT_COUNT
} rtsfr_m5_event_kind_t;
typedef struct {
    uint32_t tick, event_id, value, cycles;
    uint16_t generation, reason;
    uint8_t task_id, test_id;
    rtsfr_m5_event_kind_t event;
} rtsfr_trace_event_t;
/* Task context, including nested kernel critical sections. No allocation/UART. */
void rtsfr_trace_event(const rtsfr_trace_event_t *event);
/* ISR only at priorities permitted to use FreeRTOS FromISR APIs; never NMI/fault. */
void rtsfr_trace_event_from_isr(const rtsfr_trace_event_t *event);
/* Single task consumer. False when empty/null. Initialization before producers. */
void rtsfr_trace_init(void);
bool rtsfr_trace_pop(rtsfr_trace_event_t *event);
uint32_t rtsfr_trace_dropped(void);
const char *rtsfr_trace_name(rtsfr_m5_event_kind_t event);
/* Task context helper; timestamp is captured at emission. */
void rtsfr_m5_emit(rtsfr_m5_event_kind_t event, uint8_t test_id,
                   uint32_t event_id, uint8_t task_id, uint16_t generation,
                   uint16_t reason, uint32_t value);
/* Called by FreeRTOS lifecycle trace hooks; handle is logged as opaque value. */
void rtsfr_m5_lifecycle(void *handle, unsigned int created);
#endif
