/**
 * @file rtsfr_detect.h
 * @brief Public interface of Module 1: Fault Detection & Kernel Integration
 * @details Owned by Việt Anh.
 */

#ifndef RTSFR_DETECT_H
#define RTSFR_DETECT_H

#include "rtsfr_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Task record stored in the internal registry
 */
typedef struct {
    uint8_t             task_id;
    bool                fault_pending;
    uint16_t            generation;
    TaskHandle_t        task_handle;
    TickType_t          timeout_ticks;
    TickType_t          last_progress_tick;
    TickType_t          expected_wait_deadline;
    uint32_t            progress_seq;
    rtsfr_task_state_t  state;
} rtsfr_task_record_t;

/**
 * @brief Initialize the fault detection subsystem and internal registry
 * @return RTSFR_OK on success
 */
rtsfr_status_t rtsfr_detect_init(void);

/**
 * @brief Register a task to be monitored by RTSFR
 * @param cfg Pointer to static task configuration
 * @param task_handle Handle returned by xTaskCreate
 * @return RTSFR_OK on success, error code otherwise
 */
rtsfr_status_t rtsfr_register_task(const rtsfr_task_config_t *cfg,
                                   TaskHandle_t task_handle);

/**
 * @brief Report forward progress from application task context
 * @param task_id Logical task ID
 * @return RTSFR_OK if recorded, RTSFR_ERR_NOT_FOUND if unregistered
 * @note Must be called periodically by the monitored task
 */
rtsfr_status_t rtsfr_report_progress(uint8_t task_id);

/**
 * @brief Declare the start of a legitimate blocking wait (e.g., vTaskDelay, queue recv)
 * @param task_id Logical task ID
 * @param timeout Maximum expected duration of the wait in ticks
 * @return RTSFR_OK on success
 */
rtsfr_status_t rtsfr_begin_expected_wait(uint8_t task_id, TickType_t timeout);

/**
 * @brief Declare the end of a legitimate blocking wait
 * @param task_id Logical task ID
 * @return RTSFR_OK on success
 */
rtsfr_status_t rtsfr_end_expected_wait(uint8_t task_id);

/**
 * @brief Periodic scan function called from Supervisor context
 * @param current_tick Current system tick
 * @return RTSFR_OK on success
 */
rtsfr_status_t rtsfr_detect_scan(TickType_t current_tick);

/**
 * @brief Retrieve and remove the oldest pending fault event
 * @param[out] event Destination buffer for event data
 * @return true if an event was popped, false if queue is empty
 */
bool rtsfr_detect_pop_event(rtsfr_fault_event_t *event);

/**
 * @brief Lifecycle callback invoked by Module 4 when a new task generation starts
 * @param task_id Logical task ID
 * @param new_handle Handle of the newly created task instance
 * @param new_generation Increment generation counter
 */
void rtsfr_detect_on_new_generation(uint8_t task_id, TaskHandle_t new_handle,
                                    uint16_t new_generation);

/**
 * @brief Lifecycle callback invoked when a task is deleted in FreeRTOS
 * @param task_handle Handle of task being deleted
 */
void rtsfr_detect_on_task_delete(TaskHandle_t task_handle);

/**
 * @brief Get snapshot of a task record (for debug/test and Module 5 trace)
 * @param task_id Logical task ID
 * @param[out] out_record Destination buffer
 * @return RTSFR_OK if found, RTSFR_ERR_NOT_FOUND otherwise
 */
rtsfr_status_t rtsfr_detect_get_record(uint8_t task_id, rtsfr_task_record_t *out_record);

#ifdef __cplusplus
}
#endif

#endif /* RTSFR_DETECT_H */
