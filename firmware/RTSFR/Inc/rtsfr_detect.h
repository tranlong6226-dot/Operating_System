#ifndef RTSFR_DETECT_H
#define RTSFR_DETECT_H

#include "rtsfr_types.h"

typedef struct
{
    uint8_t task_id;
    uint16_t generation;
    TaskHandle_t task_handle;
    TickType_t timeout_ticks;
    TickType_t last_progress_tick;
    TickType_t expected_wait_deadline;
    uint32_t progress_seq;
    rtsfr_task_state_t state;
    bool fault_pending;
} rtsfr_task_record_t;

rtsfr_status_t rtsfr_detect_init(void);
rtsfr_status_t rtsfr_register_task(const rtsfr_task_config_t *cfg,
                                   TaskHandle_t task_handle);
rtsfr_status_t rtsfr_report_progress(uint8_t task_id);
rtsfr_status_t rtsfr_begin_expected_wait(uint8_t task_id, TickType_t timeout);
rtsfr_status_t rtsfr_end_expected_wait(uint8_t task_id);
rtsfr_status_t rtsfr_detect_report_fault(uint8_t task_id,
                                         uint16_t generation,
                                         uint32_t event_id,
                                         rtsfr_fault_t fault_type);
rtsfr_status_t rtsfr_detect_scan(TickType_t current_tick);
bool rtsfr_detect_pop_event(rtsfr_fault_event_t *event);
void rtsfr_detect_on_new_generation(uint8_t task_id,
                                    TaskHandle_t new_handle,
                                    uint16_t new_generation);
void rtsfr_detect_on_task_delete(TaskHandle_t task_handle);
rtsfr_status_t rtsfr_detect_get_record(uint8_t task_id,
                                       rtsfr_task_record_t *out_record);
uint8_t rtsfr_detect_task_id_from_handle(const void *task_handle);
uint32_t rtsfr_detect_event_overflow_count(void);
#if RTSFR_ENABLE_FAULT_INJECTION
rtsfr_status_t rtsfr_detect_test_saturate_event_queue(void);
#endif

#endif /* RTSFR_DETECT_H */
