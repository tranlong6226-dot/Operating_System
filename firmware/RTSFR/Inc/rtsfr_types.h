#ifndef RTSFR_TYPES_H
#define RTSFR_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef configRTSFR_MAX_TASKS
#define configRTSFR_MAX_TASKS 3U
#endif
#ifndef configRTSFR_MAX_TRACKED_TASKS
#define configRTSFR_MAX_TRACKED_TASKS configRTSFR_MAX_TASKS
#endif
#ifndef configRTSFR_EVENT_QUEUE_SIZE
#define configRTSFR_EVENT_QUEUE_SIZE 8U
#endif
#ifndef configRTSFR_MAX_RESTARTS
#define configRTSFR_MAX_RESTARTS 2U
#endif
#ifndef configRTSFR_RETRY_WINDOW_MS
#define configRTSFR_RETRY_WINDOW_MS 60000U
#endif
#ifndef configRTSFR_KERNEL_CLEANUP_TIMEOUT_MS
#define configRTSFR_KERNEL_CLEANUP_TIMEOUT_MS 500U
#endif

#define RTSFR_TASK_ID_NONE 0xFFU

typedef enum
{
    RTSFR_OK = 0,
    RTSFR_ERR_INVALID,
    RTSFR_ERR_STALE,
    RTSFR_ERR_BUSY,
    RTSFR_ERR_UNSAFE,
    RTSFR_ERR_NO_RESOURCE,
    RTSFR_ERR_TIMEOUT,
    RTSFR_ERR_NOT_READY,
    RTSFR_ERR_INTERNAL,
    RTSFR_ERR_NOT_FOUND
} rtsfr_status_t;

typedef enum
{
    RTSFR_ACT_IGNORE = 0,
    RTSFR_ACT_OBSERVE,
    RTSFR_ACT_RESTART,
    RTSFR_ACT_DEFER,
    RTSFR_ACT_SAFE_MODE,
    RTSFR_ACT_REQUEST_RESET
} rtsfr_action_t;

typedef enum
{
    RTSFR_CRIT_LOW = 0,
    RTSFR_CRIT_MEDIUM,
    RTSFR_CRIT_HIGH
} rtsfr_criticality_t;

typedef enum
{
    RTSFR_TASK_UNREGISTERED = 0,
    RTSFR_TASK_REGISTERED,
    RTSFR_TASK_HEALTHY,
    RTSFR_TASK_EXPECTED_WAIT,
    RTSFR_TASK_SUSPECT,
    RTSFR_TASK_FAULTY,
    RTSFR_TASK_QUIESCING,
    RTSFR_TASK_CLEANUP,
    RTSFR_TASK_RECREATE,
    RTSFR_TASK_VERIFYING,
    RTSFR_TASK_SAFE_MODE
} rtsfr_task_state_t;

typedef enum
{
    RTSFR_FAULT_NONE = 0,
    RTSFR_FAULT_NO_PROGRESS,
    RTSFR_FAULT_BUSY_LOOP,
    RTSFR_FAULT_SELF_REPORTED,
    RTSFR_FAULT_EXPECTED_WAIT,
    RTSFR_FAULT_WAIT_TIMEOUT,
    RTSFR_FAULT_REPEATED_FAILURE,
    RTSFR_FAULT_KERNEL_STALL,
    RTSFR_FAULT_STACK_OVERFLOW,
    RTSFR_FAULT_HARD_FAULT,
    RTSFR_FAULT_CORRUPTION
} rtsfr_fault_t;

typedef enum
{
    RTSFR_SEV_NONE = 0,
    RTSFR_SEV_TRANSIENT,
    RTSFR_SEV_RECOVERABLE_RES,
    RTSFR_SEV_HAZARDOUS_RES,
    RTSFR_SEV_PERSISTENT,
    RTSFR_SEV_FATAL
} rtsfr_severity_t;

typedef struct
{
    uint8_t task_id;
    const char *task_name;
    TickType_t timeout_ticks;
    rtsfr_criticality_t criticality;
} rtsfr_task_config_t;

typedef struct
{
    uint8_t task_id;
    uint16_t generation;
    uint32_t event_id;
    TickType_t observed_tick;
    rtsfr_fault_t fault_type;
    uint32_t evidence;
} rtsfr_fault_event_t;

typedef struct
{
    uint32_t event_id;
    uint8_t task_id;
    uint16_t generation;
    rtsfr_action_t action;
    uint16_t reason_code;
} rtsfr_decision_t;

typedef struct
{
    uint8_t task_id;
    uint16_t generation;
    uint8_t mutex_held_count;
    uint8_t managed_buffers_owned;
    uint8_t unknown_resource_count;
    bool has_inflight_io;
    bool can_reclaim_all;
} rtsfr_resource_summary_t;

typedef enum
{
    RTSFR_RECOVERY_NONE = 0,
    RTSFR_RECOVERY_IN_PROGRESS,
    RTSFR_RECOVERY_OK,
    RTSFR_RECOVERY_REJECTED,
    RTSFR_RECOVERY_STALE,
    RTSFR_RECOVERY_UNSAFE_RESOURCE,
    RTSFR_RECOVERY_CLEANUP_FAILED,
    RTSFR_RECOVERY_CREATE_FAILED,
    RTSFR_RECOVERY_VERIFY_TIMEOUT,
    RTSFR_RECOVERY_INTERNAL_ERROR
} rtsfr_recovery_outcome_t;

typedef enum
{
    RTSFR_POL_REASON_NONE = 0x0000,
    RTSFR_POL_REASON_INVALID_INPUT = 0x0101,
    RTSFR_POL_REASON_STALE_GENERATION = 0x0102,
    RTSFR_POL_REASON_DUPLICATE_EVENT = 0x0103,
    RTSFR_POL_REASON_LEGITIMATE_WAIT = 0x0104,
    RTSFR_POL_REASON_FALSE_ALARM_RECOVERED = 0x0105,
    RTSFR_POL_REASON_ALREADY_IN_SAFE_MODE = 0x0106,
    RTSFR_POL_REASON_RECOVERY_IN_PROGRESS = 0x0201,
    RTSFR_POL_REASON_SAFE_NO_RESOURCE = 0x0301,
    RTSFR_POL_REASON_SAFE_BUFFER_RECLAIM = 0x0302,
    RTSFR_POL_REASON_UNSAFE_MUTEX_HELD = 0x0401,
    RTSFR_POL_REASON_UNSAFE_UNKNOWN_RES = 0x0402,
    RTSFR_POL_REASON_UNSAFE_INFLIGHT_IO = 0x0403,
    RTSFR_POL_REASON_UNSAFE_CANNOT_RECLAIM = 0x0404,
    RTSFR_POL_REASON_RETRY_EXHAUSTED = 0x0405,
    RTSFR_POL_REASON_HIGH_CRIT_POLICY = 0x0406,
    RTSFR_POL_REASON_RECOVERY_FAILED = 0x0407,
    RTSFR_POL_REASON_EVENT_QUEUE_OVERFLOW = 0x0408,
    RTSFR_POL_REASON_UNTRUSTED_STATE = 0x0501,
    RTSFR_POL_REASON_KERNEL_STALL = 0x0502,
    RTSFR_POL_REASON_HIGH_CRIT_EXHAUSTED = 0x0503
} rtsfr_policy_reason_t;

#ifdef __cplusplus
}
#endif

#endif /* RTSFR_TYPES_H */
