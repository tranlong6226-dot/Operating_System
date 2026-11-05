/**
 * @file rtsfr_types.h
 * @brief Common types and definitions for RTSFR (Resource-aware Task Selective Fault Recovery)
 * @details Defined by HUST OS Project 2026 team. Shared between Modules 1 to 5.
 */

#ifndef RTSFR_TYPES_H
#define RTSFR_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* FreeRTOS types or Host Test forward declarations */
#if defined(RTSFR_HOST_TEST) || (!defined(USE_HAL_DRIVER) && !defined(STM32F103xB) && !defined(__arm__) && !defined(__ARMCC_VERSION))
    #ifndef INC_FREERTOS_H
    typedef uint32_t TickType_t;
    typedef void * TaskHandle_t;
    #endif
#else
    #include "FreeRTOS.h"
    #include "task.h"
#endif

/* Maximum number of tasks tracked by RTSFR */
#ifndef configRTSFR_MAX_TRACKED_TASKS
#define configRTSFR_MAX_TRACKED_TASKS   (4U)
#endif

/* Maximum size of fault event queue */
#ifndef configRTSFR_EVENT_QUEUE_SIZE
#define configRTSFR_EVENT_QUEUE_SIZE    (8U)
#endif

/**
 * @brief Unified return status codes across all RTSFR modules
 */
typedef enum {
    RTSFR_OK                 = 0,
    RTSFR_ERR_INVALID        = -1,
    RTSFR_ERR_STALE          = -2,
    RTSFR_ERR_BUSY           = -3,
    RTSFR_ERR_UNSAFE         = -4,
    RTSFR_ERR_NO_RESOURCE    = -5,
    RTSFR_ERR_TIMEOUT        = -6,
    RTSFR_ERR_NOT_FOUND      = -7,
    RTSFR_ERR_INTERNAL       = -8
} rtsfr_status_t;

/**
 * @brief Fault types supported by RTSFR
 */
typedef enum {
    RTSFR_FAULT_NONE = 0,
    RTSFR_FAULT_NO_PROGRESS,        /* F01: Task stops reporting progress within window */
    RTSFR_FAULT_BUSY_LOOP,          /* F02: Task busy looping without progress */
    RTSFR_FAULT_WAIT_TIMEOUT,       /* F04: Expected wait duration exceeded */
    RTSFR_FAULT_VOLUNTARY,          /* F03: Task voluntary fault report */
    RTSFR_FAULT_MUTEX_HELD_DEADLOCK,/* F06: Unsafe condition involving mutex */
    RTSFR_FAULT_KERNEL_PANIC        /* Unrecoverable kernel panic / assertion */
} rtsfr_fault_type_t;

/**
 * @brief Task monitoring state
 */
typedef enum {
    RTSFR_TASK_STATE_UNREGISTERED = 0,
    RTSFR_TASK_STATE_HEALTHY,        /* Task is active and reporting progress */
    RTSFR_TASK_STATE_EXPECTED_WAIT,  /* Task is performing a declared blocking wait */
    RTSFR_TASK_STATE_FAULTY,         /* Fault detected and event queued */
    RTSFR_TASK_STATE_QUIESCED        /* Task is quiesced awaiting recovery */
} rtsfr_task_state_t;

/**
 * @brief Configuration descriptor for registering a task into RTSFR
 */
typedef struct {
    uint8_t         task_id;         /* Static logical task ID (0..MAX-1) */
    const char     *task_name;       /* Task name string for debugging */
    TickType_t      timeout_ticks;   /* Maximum allowed ticks without progress */
    uint8_t         criticality;     /* 0: LOW, 1: MEDIUM, 2: HIGH (CRITICAL) */
} rtsfr_task_config_t;

/**
 * @brief Fault event emitted by Module 1 and consumed by Module 2
 */
typedef struct {
    uint32_t            event_id;            /* Monotonically increasing unique event ID */
    uint8_t             task_id;             /* Target task ID */
    uint16_t            generation;          /* Task instance generation when fault detected */
    rtsfr_fault_type_t  fault_type;          /* Classified fault type */
    TickType_t          detected_tick;       /* Timestamp of detection */
    uint32_t            last_progress_seq;   /* Last reported sequence number */
} rtsfr_fault_event_t;

#ifdef __cplusplus
}
#endif

#endif /* RTSFR_TYPES_H */
