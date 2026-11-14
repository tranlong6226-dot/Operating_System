#ifndef RTSFR_VERIFY_H
#define RTSFR_VERIFY_H

#include "rtsfr_m5_config.h"
#include "rtsfr_resource.h"
#include "rtsfr_types.h"

typedef enum
{
    M5_RESOURCE_FREE = 0,
    M5_RESOURCE_OWNED,
    M5_RESOURCE_IN_FLIGHT,
    M5_RESOURCE_QUARANTINED
} rtsfr_verify_resource_state_t;

typedef struct
{
    uint8_t task_id;
    uint16_t generation;
    bool active;
    bool restarting;
    bool resources_safe;
    bool first_progress;
    bool recovery_ok;
    uint16_t retries;
    uint16_t retry_limit;
} rtsfr_verify_task_t;

typedef struct
{
    uint16_t resource_id;
    uint8_t owner_id;
    uint16_t owner_generation;
    rtsfr_verify_resource_state_t state;
} rtsfr_verify_resource_t;

typedef struct
{
    bool registry_complete;
    bool resources_complete;
    uint8_t task_count;
    uint8_t resource_count;
    rtsfr_verify_task_t tasks[RTSFR_VERIFY_MAX_TASKS];
    rtsfr_verify_resource_t resources[RTSFR_VERIFY_MAX_RESOURCES];
} rtsfr_verify_snapshot_t;

typedef enum
{
    M5_INVARIANT_OK = 0,
    M5_INVARIANT_MISSING_DATA,
    M5_INVARIANT_BOUNDS,
    M5_INVARIANT_TASK_ID,
    M5_INVARIANT_GENERATION,
    M5_INVARIANT_DUPLICATE_TASK,
    M5_INVARIANT_RESOURCE_ID,
    M5_INVARIANT_OWNER,
    M5_INVARIANT_UNSAFE_RESTART,
    M5_INVARIANT_EARLY_SUCCESS,
    M5_INVARIANT_RETRY_LIMIT,
    M5_INVARIANT_RESOURCE_STATE
} rtsfr_invariant_reason_t;

typedef struct
{
    rtsfr_invariant_reason_t reason;
    uint16_t index;
} rtsfr_verify_report_t;

typedef rtsfr_status_t (*rtsfr_verify_provider_fn)(rtsfr_verify_snapshot_t *out);

rtsfr_status_t rtsfr_verify_check(const rtsfr_verify_snapshot_t *snapshot,
                                  rtsfr_verify_report_t *report);
void rtsfr_verify_bind(rtsfr_verify_provider_fn provider);
bool rtsfr_verify_invariants(void);
rtsfr_verify_report_t rtsfr_verify_last_report(void);

#endif /* RTSFR_VERIFY_H */
