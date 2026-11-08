#ifndef RTSFR_TYPES_H
#define RTSFR_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#if defined(RTSFR_HOST_TEST) || (!defined(USE_HAL_DRIVER) && !defined(STM32F103xB) && !defined(__arm__) && !defined(__ARMCC_VERSION) && !defined(__ARMCOMPILER_VERSION))
#ifndef INC_FREERTOS_H
typedef uint32_t TickType_t;
typedef uint32_t UBaseType_t;
typedef uint16_t configSTACK_DEPTH_TYPE;
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
#endif
#else
#include "FreeRTOS.h"
#include "task.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * 1. CẤU HÌNH HỆ THỐNG DÙNG CHUNG CHO M1 - M5
 * Đồng bộ giữa nhánh main (Tú), module1-vietanh (Việt Anh) và hlong (Hồng Long)
 * ========================================================================== */
#ifndef configRTSFR_ENABLE
#define configRTSFR_ENABLE                  1
#endif

#ifndef configRTSFR_MAX_TASKS
#define configRTSFR_MAX_TASKS               3U
#endif

#ifndef configRTSFR_MAX_TRACKED_TASKS
#define configRTSFR_MAX_TRACKED_TASKS       4U
#endif

#ifndef configRTSFR_EVENT_QUEUE_SIZE
#define configRTSFR_EVENT_QUEUE_SIZE        8U
#endif

#ifndef configRTSFR_MAX_TRACKED_MUTEXES
#define configRTSFR_MAX_TRACKED_MUTEXES     4U
#endif

#ifndef configRTSFR_MAX_MANAGED_BUFFERS
#define configRTSFR_MAX_MANAGED_BUFFERS     8U
#endif

#ifndef configRTSFR_MAX_RESTARTS
#define configRTSFR_MAX_RESTARTS            2U
#endif

#ifndef configRTSFR_RETRY_WINDOW_MS
#define configRTSFR_RETRY_WINDOW_MS         60000U
#endif

#ifndef RTSFR_TASK_ID_NONE
#define RTSFR_TASK_ID_NONE                  (0xFFU)
#endif

/* ==========================================================================
 * 2. MÃ TRẠNG THÁI TRẢ VỀ CHUNG (Tương thích cả M1, M2, M3, M4, M5)
 * Bao gồm cả RTSFR_ERR_NOT_READY (M4) và RTSFR_ERR_NOT_FOUND (M1)
 * ========================================================================== */
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
    RTSFR_ERR_NOT_FOUND,
    RTSFR_ERR_INTERNAL
} rtsfr_status_t;

/* ==========================================================================
 * 3. HÀNH ĐỘNG QUYẾT ĐỊNH & KẾT QUẢ PHỤC HỒI (M2 <-> M4)
 * ========================================================================== */
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

#ifndef RTSFR_RESOURCE_SUMMARY_T_DEFINED
#define RTSFR_RESOURCE_SUMMARY_T_DEFINED
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
#endif /* RTSFR_RESOURCE_SUMMARY_T_DEFINED */

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

/* ==========================================================================
 * 4. PHÂN LOẠI LỖI, TRẠNG THÁI TASK, MỨC QUAN TRỌNG & SỰ KIỆN LỖI (M1 <-> M2)
 * Tương thích đồng thời với rtsfr_detect.h (Việt Anh) và rtsfr_policy.h (Vũ Long)
 * ========================================================================== */

/* Mức độ quan trọng của Task (Criticality: 0 = LOW, 1 = MEDIUM, 2 = HIGH) */
typedef enum
{
    RTSFR_CRIT_LOW    = 0,
    RTSFR_CRIT_MEDIUM = 1,
    RTSFR_CRIT_HIGH   = 2
} rtsfr_criticality_t;

/*
 * Trạng thái vòng đời của Task.
 * Hỗ trợ đầy đủ cả tên hằng RTSFR_TASK_STATE_... (M1 Việt Anh)
 * và RTSFR_TASK_... (M2 Vũ Long).
 */
typedef enum
{
    RTSFR_TASK_STATE_UNREGISTERED = 0,
    RTSFR_TASK_UNREGISTERED       = RTSFR_TASK_STATE_UNREGISTERED,

    RTSFR_TASK_REGISTERED,

    RTSFR_TASK_STATE_HEALTHY,
    RTSFR_TASK_HEALTHY            = RTSFR_TASK_STATE_HEALTHY,

    RTSFR_TASK_STATE_EXPECTED_WAIT,

    RTSFR_TASK_STATE_FAULTY,
    RTSFR_TASK_SUSPECT            = RTSFR_TASK_STATE_FAULTY,

    RTSFR_TASK_STATE_QUIESCED,
    RTSFR_TASK_QUIESCING          = RTSFR_TASK_STATE_QUIESCED,

    RTSFR_TASK_CLEANUP,
    RTSFR_TASK_RECREATE,
    RTSFR_TASK_VERIFYING,
    RTSFR_TASK_SAFE_MODE
} rtsfr_task_state_t;

/*
 * Các loại lỗi hỗ trợ (F01 -> F10).
 * Hỗ trợ đầy đủ cả các mã lỗi từ M1 (Việt Anh: RTSFR_FAULT_WAIT_TIMEOUT,
 * RTSFR_FAULT_VOLUNTARY, RTSFR_FAULT_MUTEX_HELD_DEADLOCK, RTSFR_FAULT_KERNEL_PANIC)
 * và các mã lỗi chi tiết trong đặc tả M2.
 */
typedef enum
{
    RTSFR_FAULT_NONE = 0,
    RTSFR_FAULT_NO_PROGRESS,          /* F01: Không báo tiến độ đúng hạn */
    RTSFR_FAULT_BUSY_LOOP,            /* F02: Vòng lặp bận không tiến triển */
    RTSFR_FAULT_WAIT_TIMEOUT,         /* F04 expired: Quá hạn chờ expected wait */
    RTSFR_FAULT_VOLUNTARY,            /* F03 (M1): Task tự báo lỗi nội bộ */
    RTSFR_FAULT_SELF_REPORTED = RTSFR_FAULT_VOLUNTARY,
    RTSFR_FAULT_MUTEX_HELD_DEADLOCK,  /* F06 (M1): Lỗi liên quan giữ khóa Mutex */
    RTSFR_FAULT_KERNEL_PANIC,         /* F08/F09 (M1): Lỗi nghiêm trọng mức Kernel */
    RTSFR_FAULT_EXPECTED_WAIT,        /* F04/F10: Đang chờ hợp lệ (không phải lỗi) */
    RTSFR_FAULT_REPEATED_FAILURE,     /* F07: Lỗi lặp lại nhiều lần */
    RTSFR_FAULT_KERNEL_STALL,         /* F08: Kernel/Tick bị kẹt */
    RTSFR_FAULT_STACK_OVERFLOW,       /* F09a: Tràn ngăn xếp (Untrusted state) */
    RTSFR_FAULT_HARD_FAULT,           /* F09b: Lỗi phần cứng/con trỏ */
    RTSFR_FAULT_CORRUPTION            /* F09c: Hỏng cấu trúc dữ liệu kernel */
} rtsfr_fault_type_t;

typedef rtsfr_fault_type_t rtsfr_fault_t;

/* Cấu hình đăng ký Task của Module 1 (rtsfr_detect.h) */
typedef struct
{
    uint8_t     task_id;        /* Static logical task ID */
    const char *task_name;      /* Task name string for debugging */
    TickType_t  timeout_ticks;  /* Maximum allowed ticks without progress */
    uint8_t     criticality;    /* 0: LOW, 1: MEDIUM, 2: HIGH (CRITICAL) */
} rtsfr_task_config_t;

/* Mức độ nghiêm trọng của lỗi (Severity - M2 chuẩn hóa) */
typedef enum
{
    RTSFR_SEV_NONE = 0,
    RTSFR_SEV_TRANSIENT,            /* Lỗi tạm thời, không giữ tài nguyên nguy hiểm */
    RTSFR_SEV_RECOVERABLE_RES,      /* Giữ managed buffer nhưng thu hồi an toàn được */
    RTSFR_SEV_HAZARDOUS_RES,        /* Giữ Mutex / IO đang bay / Tài nguyên unknown */
    RTSFR_SEV_PERSISTENT,           /* Lỗi lặp lại vượt ngân sách retry */
    RTSFR_SEV_FATAL                 /* Lỗi nghiêm trọng mức Kernel/MCU */
} rtsfr_severity_t;

/*
 * Mã lý do ra quyết định của Module 2 (Policy Reason Codes).
 * Dùng tiền tố RTSFR_POL_REASON_ để không trùng với rtsfr_recovery_reason_t (M4).
 */
typedef enum
{
    RTSFR_POL_REASON_NONE = 0x0000,

    /* Nhóm lý do trả về IGNORE / OBSERVE */
    RTSFR_POL_REASON_INVALID_INPUT          = 0x0101,
    RTSFR_POL_REASON_STALE_GENERATION       = 0x0102,
    RTSFR_POL_REASON_DUPLICATE_EVENT        = 0x0103,
    RTSFR_POL_REASON_LEGITIMATE_WAIT        = 0x0104,
    RTSFR_POL_REASON_FALSE_ALARM_RECOVERED  = 0x0105,
    RTSFR_POL_REASON_ALREADY_IN_SAFE_MODE   = 0x0106,

    /* Nhóm lý do trả về DEFER */
    RTSFR_POL_REASON_RECOVERY_IN_PROGRESS   = 0x0201,

    /* Nhóm lý do trả về RESTART */
    RTSFR_POL_REASON_SAFE_NO_RESOURCE       = 0x0301,
    RTSFR_POL_REASON_SAFE_BUFFER_RECLAIM    = 0x0302,

    /* Nhóm lý do trả về SAFE_MODE */
    RTSFR_POL_REASON_UNSAFE_MUTEX_HELD      = 0x0401,
    RTSFR_POL_REASON_UNSAFE_UNKNOWN_RES     = 0x0402,
    RTSFR_POL_REASON_UNSAFE_INFLIGHT_IO     = 0x0403,
    RTSFR_POL_REASON_UNSAFE_CANNOT_RECLAIM  = 0x0404,
    RTSFR_POL_REASON_RETRY_EXHAUSTED        = 0x0405,
    RTSFR_POL_REASON_HIGH_CRIT_POLICY       = 0x0406,
    RTSFR_POL_REASON_RECOVERY_FAILED        = 0x0407,

    /* Nhóm lý do trả về REQUEST_RESET */
    RTSFR_POL_REASON_UNTRUSTED_STATE        = 0x0501,
    RTSFR_POL_REASON_KERNEL_STALL           = 0x0502,
    RTSFR_POL_REASON_HIGH_CRIT_EXHAUSTED    = 0x0503
} rtsfr_policy_reason_t;

/*
 * Sự kiện lỗi đầu vào từ Module 1 gửi sang Module 2 (M1 -> M2).
 * Sử dụng anonymous union để hỗ trợ đồng thời tên trường của Việt Anh
 * (detected_tick, last_progress_seq) và tên trường trong đặc tả (observed_tick, evidence).
 */
typedef struct
{
    uint32_t           event_id;
    uint8_t            task_id;
    uint16_t           generation;
    rtsfr_fault_type_t fault_type;
    union
    {
        TickType_t     detected_tick;
        TickType_t     observed_tick;
    };
    union
    {
        uint32_t       last_progress_seq;
        uint32_t       evidence;
    };
} rtsfr_fault_event_t;

#ifdef __cplusplus
}
#endif

#endif /* RTSFR_TYPES_H */
