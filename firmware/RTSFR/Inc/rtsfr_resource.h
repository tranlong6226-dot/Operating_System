#ifndef RTSFR_RESOURCE_H
#define RTSFR_RESOURCE_H

#include "rtsfr_types.h"

#ifndef configRTSFR_MAX_TRACKED_MUTEXES
#define configRTSFR_MAX_TRACKED_MUTEXES 4U
#endif
#ifndef configRTSFR_MAX_MANAGED_BUFFERS
#define configRTSFR_MAX_MANAGED_BUFFERS 8U
#endif

typedef enum
{
    RTSFR_RESOURCE_FREE = 0,
    RTSFR_RESOURCE_OWNED,
    RTSFR_RESOURCE_IN_FLIGHT,
    RTSFR_RESOURCE_QUARANTINED
} rtsfr_resource_state_t;

typedef struct
{
    uint16_t resource_id;
    uint8_t owner_task_id;
    uint16_t owner_generation;
    rtsfr_resource_state_t state;
} rtsfr_resource_record_t;

void rtsfr_resource_init(void);
rtsfr_status_t rtsfr_resource_snapshot(uint8_t task_id,
                                       uint16_t generation,
                                       rtsfr_resource_summary_t *out);
rtsfr_status_t rtsfr_resource_cleanup_safe(uint8_t task_id,
                                           uint16_t generation);
void rtsfr_resource_on_mutex_acquire(uint8_t task_id, uint16_t resource_id);
void rtsfr_resource_on_mutex_release(uint8_t task_id, uint16_t resource_id);
void rtsfr_resource_on_new_generation(uint8_t task_id, uint16_t generation);
rtsfr_status_t rtsfr_buffer_acquire(uint8_t task_id, uint8_t *out_index);
rtsfr_status_t rtsfr_buffer_release(uint8_t task_id, uint8_t index);
rtsfr_status_t rtsfr_resource_register_mutex(const void *mutex_handle,
                                              uint16_t *out_resource_id);
rtsfr_status_t rtsfr_resource_unregister_mutex(const void *mutex_handle);
void rtsfr_resource_hook_mutex_taken(const void *mutex_handle,
                                     const void *holder_task_handle);
void rtsfr_resource_hook_mutex_given(const void *mutex_handle,
                                     const void *holder_task_handle);
rtsfr_status_t rtsfr_resource_export(rtsfr_resource_record_t *out,
                                     uint8_t capacity,
                                     uint8_t *out_count);
uint16_t rtsfr_resource_get_anomaly_count(void);

#endif /* RTSFR_RESOURCE_H */
