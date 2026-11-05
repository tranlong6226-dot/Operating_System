#ifndef RTSFR_RESOURCE_H
#define RTSFR_RESOURCE_H

#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "rtsfr_types.h"

#ifndef configRTSFR_MAX_TRACKED_MUTEXES
#define configRTSFR_MAX_TRACKED_MUTEXES 4
#endif
#ifndef configRTSFR_MAX_MANAGED_BUFFERS
#define configRTSFR_MAX_MANAGED_BUFFERS 8
#endif
#ifndef configRTSFR_ENABLE
#define configRTSFR_ENABLE 0
#endif


#ifndef RTSFR_TASK_ID_NONE
#define RTSFR_TASK_ID_NONE  (0xFFu)
#endif

#define RTSFR_CMSIS_MUTEX_TO_HANDLE(id)  ((const void *)((uintptr_t)(id) & ~(uintptr_t)1u))

typedef enum { BUF_FREE, BUF_OWNED, BUF_IN_FLIGHT, BUF_QUARANTINED } rtsfr_buffer_state_t;
typedef struct {
    uint8_t  task_id; uint16_t generation; uint8_t mutex_held_count;
    uint8_t  managed_buffers_owned; uint8_t unknown_resource_count;
    bool     has_inflight_io; bool can_reclaim_all;
} rtsfr_resource_summary_t;

rtsfr_status_t rtsfr_resource_snapshot(uint8_t task_id, uint16_t generation, rtsfr_resource_summary_t *out);
rtsfr_status_t rtsfr_resource_cleanup_safe(uint8_t task_id, uint16_t generation);
void rtsfr_resource_on_mutex_acquire(uint8_t task_id, uint16_t resource_id);
void rtsfr_resource_on_mutex_release(uint8_t task_id, uint16_t resource_id);
void rtsfr_resource_on_new_generation(uint8_t task_id, uint16_t generation);
rtsfr_status_t rtsfr_buffer_acquire(uint8_t task_id, uint8_t *out_index);
rtsfr_status_t rtsfr_buffer_release(uint8_t task_id, uint8_t index);
rtsfr_status_t rtsfr_resource_register_mutex(const void *mutex_handle, uint16_t *out_resource_id);
rtsfr_status_t rtsfr_resource_unregister_mutex(const void *mutex_handle);
void rtsfr_resource_hook_mutex_taken(const void *mutex_handle, const void *holder_task_handle);
void rtsfr_resource_hook_mutex_given(const void *mutex_handle, const void *holder_task_handle);
uint8_t rtsfr_detect_task_id_from_handle(const void *task_handle);
uint16_t rtsfr_resource_get_anomaly_count(void);
#endif
