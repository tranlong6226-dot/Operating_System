#ifndef RTSFR_RECOVERY_H
#define RTSFR_RECOVERY_H

#include "rtsfr_safe_mode.h"
#include "rtsfr_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef configRTSFR_MAX_TASKS
#define configRTSFR_MAX_TASKS 3U
#endif
#ifndef configRTSFR_EVENT_HISTORY_SIZE
#define configRTSFR_EVENT_HISTORY_SIZE 8U
#endif

typedef enum
{
    RTSFR_REC_STATE_IDLE = 0,
    RTSFR_REC_STATE_QUIESCING,
    RTSFR_REC_STATE_REVALIDATING,
    RTSFR_REC_STATE_CLEANUP,
    RTSFR_REC_STATE_DELETING,
    RTSFR_REC_STATE_WAIT_KERNEL_CLEANUP,
    RTSFR_REC_STATE_RECREATING,
    RTSFR_REC_STATE_VERIFYING,
    RTSFR_REC_STATE_COMPLETE,
    RTSFR_REC_STATE_SAFE_MODE
} rtsfr_recovery_state_t;

typedef enum
{
    RTSFR_REASON_NONE = 0,
    RTSFR_REASON_INVALID_DECISION,
    RTSFR_REASON_FALSE_ALARM_ROLLBACK,
    RTSFR_REASON_STALE_GENERATION,
    RTSFR_REASON_DUPLICATE_EVENT,
    RTSFR_REASON_RECOVERY_BUSY,
    RTSFR_REASON_SELF_RECOVERY_FORBIDDEN,
    RTSFR_REASON_SNAPSHOT_UNAVAILABLE,
    RTSFR_REASON_SNAPSHOT_MISMATCH,
    RTSFR_REASON_MUTEX_HELD,
    RTSFR_REASON_UNKNOWN_RESOURCE,
    RTSFR_REASON_IO_IN_FLIGHT,
    RTSFR_REASON_RESOURCE_NOT_RECLAIMABLE,
    RTSFR_REASON_CLEANUP_UNAVAILABLE,
    RTSFR_REASON_CLEANUP_FAILED,
    RTSFR_REASON_CLEANUP_INCOMPLETE,
    RTSFR_REASON_PREPARE_DELETE_FAILED,
    RTSFR_REASON_KERNEL_CLEANUP_TIMEOUT,
    RTSFR_REASON_CONTEXT_RESET_FAILED,
    RTSFR_REASON_CREATE_FAILED,
    RTSFR_REASON_GENERATION_PUBLISH_FAILED,
    RTSFR_REASON_VERIFY_TIMEOUT,
    RTSFR_REASON_FIRST_PROGRESS,
    RTSFR_REASON_GENERATION_EXHAUSTED,
    RTSFR_REASON_INTERNAL
} rtsfr_recovery_reason_t;

typedef enum
{
    RTSFR_TRACE_REQUEST = 0,
    RTSFR_TRACE_CANCELLED,
    RTSFR_TRACE_QUIESCED,
    RTSFR_TRACE_RESOURCE_SAFE,
    RTSFR_TRACE_RESOURCE_UNSAFE,
    RTSFR_TRACE_CLEANUP_OK,
    RTSFR_TRACE_TASK_DELETED,
    RTSFR_TRACE_KERNEL_CLEANUP_OK,
    RTSFR_TRACE_TASK_CREATED,
    RTSFR_TRACE_FIRST_PROGRESS,
    RTSFR_TRACE_RECOVERY_RESULT,
    RTSFR_TRACE_SAFE_MODE
} rtsfr_recovery_trace_event_t;

typedef rtsfr_status_t (*rtsfr_task_context_reset_fn)(uint8_t task_id,
                                                       uint16_t new_generation,
                                                       void *argument);

typedef struct
{
    uint8_t task_id;
    uint16_t initial_generation;
    TaskFunction_t entry;
    void *argument;
    configSTACK_DEPTH_TYPE stack_depth;
    UBaseType_t priority;
    const char *name;
    TickType_t verify_timeout_ticks;
    rtsfr_task_context_reset_fn reset_context;
} rtsfr_recovery_task_config_t;

typedef struct
{
    uint32_t event_id;
    uint8_t task_id;
    uint16_t old_generation;
    uint16_t new_generation;
    rtsfr_recovery_outcome_t outcome;
    uint16_t reason_code;
    TickType_t started_tick;
    TickType_t finished_tick;
} rtsfr_recovery_result_t;

typedef struct
{
    uint32_t event_id;
    uint8_t task_id;
    uint16_t generation;
    rtsfr_recovery_state_t state;
    rtsfr_recovery_trace_event_t event;
    uint16_t reason_code;
    TickType_t tick;
} rtsfr_recovery_trace_t;

typedef rtsfr_status_t (*rtsfr_resource_snapshot_fn)(uint8_t task_id,
                                                      uint16_t generation,
                                                      rtsfr_resource_summary_t *out);
typedef rtsfr_status_t (*rtsfr_resource_cleanup_fn)(uint8_t task_id,
                                                     uint16_t generation);
typedef rtsfr_status_t (*rtsfr_prepare_delete_fn)(uint8_t task_id,
                                                   uint16_t generation,
                                                   TaskHandle_t handle);
typedef rtsfr_status_t (*rtsfr_publish_generation_fn)(uint8_t task_id,
                                                       uint16_t generation,
                                                       TaskHandle_t new_handle);
typedef rtsfr_status_t (*rtsfr_task_create_fn)(TaskFunction_t entry,
                                               const char *name,
                                               configSTACK_DEPTH_TYPE stack_depth,
                                               void *argument,
                                               UBaseType_t priority,
                                               TaskHandle_t *out_handle);
typedef void (*rtsfr_recovery_result_fn)(const rtsfr_recovery_result_t *result);
typedef void (*rtsfr_recovery_trace_fn)(const rtsfr_recovery_trace_t *trace);
typedef void (*rtsfr_reset_request_fn)(uint16_t reason_code);

typedef struct
{
    rtsfr_resource_snapshot_fn resource_snapshot;
    rtsfr_resource_cleanup_fn resource_cleanup;
    rtsfr_prepare_delete_fn prepare_delete;
    rtsfr_publish_generation_fn publish_generation;
    rtsfr_task_create_fn create_task;
    rtsfr_recovery_result_fn record_result;
    rtsfr_recovery_trace_fn trace;
    rtsfr_reset_request_fn request_reset;
    rtsfr_safe_output_fn apply_safe_output;
} rtsfr_recovery_hooks_t;

void rtsfr_recovery_init(const rtsfr_recovery_hooks_t *hooks);

rtsfr_status_t rtsfr_recovery_register_task(
    const rtsfr_recovery_task_config_t *config,
    TaskHandle_t active_handle);

rtsfr_status_t rtsfr_recovery_request(const rtsfr_decision_t *decision);
rtsfr_status_t rtsfr_recovery_process(void);

/*
 * Cancels a false-positive recovery only while the old instance has not
 * entered cleanup/deletion.  If it was already quiesced, M4 resumes exactly
 * the handle/generation captured by the accepted decision.
 */
rtsfr_status_t rtsfr_recovery_cancel_pending(uint32_t event_id,
                                              uint8_t task_id,
                                              uint16_t generation);

void rtsfr_recovery_on_first_progress(uint8_t task_id,
                                      uint16_t generation);

rtsfr_recovery_state_t rtsfr_recovery_get_state(void);
rtsfr_status_t rtsfr_recovery_get_result(uint32_t event_id,
                                         rtsfr_recovery_result_t *out);
rtsfr_status_t rtsfr_recovery_get_active_instance(uint8_t task_id,
                                                  uint16_t *generation,
                                                  TaskHandle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* RTSFR_RECOVERY_H */
