#ifndef RTSFR_TRACE_H
#define RTSFR_TRACE_H

#include "rtsfr_m5_config.h"
#include "rtsfr_types.h"

typedef enum
{
    M5_BOOT = 0,
    M5_TASK_CREATE,
    M5_TASK_DELETE,
    M5_INJECT,
    M5_FAULT_DETECTED,
    M5_RESOURCE_CHECK,
    M5_POLICY,
    M5_RECOVERY_STEP,
    M5_RECOVERY_OK,
    M5_RECOVERY_FAILED,
    M5_FIRST_PROGRESS,
    M5_SAFE_MODE,
    M5_VERIFY_PASS,
    M5_VERIFY_FAIL,
    M5_NOT_READY,
    M5_TEST_STARTED,
    M5_TEST_PASS,
    M5_TEST_FAIL,
    M5_TRACE_DROPPED,
    M5_WATCHDOG_STARTED,
    M5_RESET_CAUSE,
    M5_STATUS,
    M5_SNAPSHOT_BEGIN,
    M5_TASK_SNAPSHOT,
    M5_RESOURCE_SNAPSHOT,
    M5_SYSTEM_SNAPSHOT,
    M5_SNAPSHOT_END,
    M5_EVENT_COUNT
} rtsfr_m5_event_kind_t;

typedef struct
{
    uint32_t tick;
    uint32_t event_id;
    uint32_t value;
    uint32_t cycles;
    uint16_t generation;
    uint16_t reason;
    uint8_t task_id;
    uint8_t test_id;
    rtsfr_m5_event_kind_t event;
} rtsfr_trace_event_t;

void rtsfr_trace_init(void);
void rtsfr_trace_event(const rtsfr_trace_event_t *event);
void rtsfr_trace_event_from_isr(const rtsfr_trace_event_t *event);
bool rtsfr_trace_pop(rtsfr_trace_event_t *event);
uint32_t rtsfr_trace_dropped(void);
const char *rtsfr_trace_name(rtsfr_m5_event_kind_t event);
void rtsfr_m5_emit(rtsfr_m5_event_kind_t event,
                   uint8_t test_id,
                   uint32_t event_id,
                   uint8_t task_id,
                   uint16_t generation,
                   uint16_t reason,
                   uint32_t value);
void rtsfr_m5_lifecycle(void *handle, unsigned int created);

#endif /* RTSFR_TRACE_H */
