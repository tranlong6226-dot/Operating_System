#ifndef RTSFR_TYPES_H
#define RTSFR_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

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
    RTSFR_ERR_INTERNAL
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

#ifdef __cplusplus
}
#endif

#endif /* RTSFR_TYPES_H */
