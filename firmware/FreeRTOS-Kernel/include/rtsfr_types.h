#ifndef RTSFR_TYPES_H
#define RTSFR_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * 1. HỖ TRỢ KIỂU DỮ LIỆU TICK CHO CẢ FREERTOS TRÊN MẠCH VÀ HOST UNIT TEST
 * ========================================================================== */
#ifndef INC_FREERTOS_H
typedef uint32_t TickType_t;
#endif

/* ==========================================================================
 * 2. CẤU HÌNH MẶC ĐỊNH CỦA HỆ THỐNG RTSFR (Theo mục 7.2 Đặc tả hệ thống)
 * ========================================================================== */
#ifndef configRTSFR_ENABLE
#define configRTSFR_ENABLE                  1
#endif

#ifndef configRTSFR_MAX_TASKS
#define configRTSFR_MAX_TASKS               3U
#endif

#ifndef configRTSFR_MAX_TRACKED_MUTEXES
#define configRTSFR_MAX_TRACKED_MUTEXES     4U
#endif

#ifndef configRTSFR_MAX_MANAGED_BUFFERS
#define configRTSFR_MAX_MANAGED_BUFFERS     8U
#endif

#ifndef configRTSFR_MONITOR_PERIOD_MS
#define configRTSFR_MONITOR_PERIOD_MS       20U
#endif

#ifndef configRTSFR_DEFAULT_TIMEOUT_MS
#define configRTSFR_DEFAULT_TIMEOUT_MS      500U
#endif

#ifndef configRTSFR_VERIFY_TIMEOUT_MS
#define configRTSFR_VERIFY_TIMEOUT_MS       500U
#endif

#ifndef configRTSFR_MAX_RESTARTS
#define configRTSFR_MAX_RESTARTS            2U
#endif

#ifndef configRTSFR_RETRY_WINDOW_MS
#define configRTSFR_RETRY_WINDOW_MS         60000U
#endif

#ifndef configRTSFR_TRACE_EVENTS
#define configRTSFR_TRACE_EVENTS            128U
#endif

/* ==========================================================================
 * 3. MÃ TRẠNG THÁI TRẢ VỀ CHUNG (Theo mục 8.2 Phân công nhóm)
 * ========================================================================== */
typedef enum {
    RTSFR_OK = 0,               /* Thực thi thành công */
    RTSFR_ERR_INVALID,          /* Tham số đầu vào NULL hoặc ngoài phạm vi */
    RTSFR_ERR_STALE,            /* Dữ liệu/Event thuộc về generation cũ */
    RTSFR_ERR_BUSY,             /* Hệ thống/Module đang bận xử lý */
    RTSFR_ERR_UNSAFE,           /* Trạng thái tài nguyên không an toàn để thu hồi */
    RTSFR_ERR_NO_RESOURCE,      /* Hết bộ nhớ hoặc vượt giới hạn bảng tĩnh */
    RTSFR_ERR_TIMEOUT,          /* Quá thời gian chờ xác nhận (verify timeout) */
    RTSFR_ERR_INTERNAL,         /* Lỗi bất biến nội bộ kernel */
    RTSFR_NOT_IMPLEMENTED       /* Dùng cho hàm stub khi chưa ghép module */
} rtsfr_status_t;

/* ==========================================================================
 * 4. MỨC ĐỘ QUAN TRỌNG CỦA TASK (Criticality - Theo mục 5.1 Đặc tả)
 * ========================================================================== */
typedef enum {
    RTSFR_CRIT_LOW = 0,         /* Task phụ (VD: Logger/Comm), ưu tiên restart cục bộ */
    RTSFR_CRIT_MEDIUM,          /* Task trung bình (VD: Sensor) */
    RTSFR_CRIT_HIGH             /* Task sống còn (VD: Control), lỗi nặng phải Safe Mode/Reset */
} rtsfr_criticality_t;

/* ==========================================================================
 * 5. TRẠNG THÁI VÒNG ĐỜI TASK TRONG RTSFR (Theo mục 4.1 Đặc tả)
 * ========================================================================== */
typedef enum {
    RTSFR_TASK_UNREGISTERED = 0,
    RTSFR_TASK_REGISTERED,      /* Đã đăng ký, chờ nhịp progress đầu tiên */
    RTSFR_TASK_HEALTHY,         /* Đang chạy bình thường */
    RTSFR_TASK_SUSPECT,         /* Nghi ngờ lỗi (quá hạn progress / báo lỗi) */
    RTSFR_TASK_QUIESCING,       /* Đang cô lập khỏi bộ lập lịch (ngừng chạy) */
    RTSFR_TASK_CLEANUP,         /* Đang thu hồi tài nguyên an toàn */
    RTSFR_TASK_RECREATE,        /* Đang khởi tạo lại thế hệ (generation) mới */
    RTSFR_TASK_VERIFYING,       /* Đang chờ nhịp progress đầu tiên của thế hệ mới */
    RTSFR_TASK_SAFE_MODE        /* Đã đưa vào chế độ an toàn, ngừng tự phục hồi */
} rtsfr_task_state_t;

/* ==========================================================================
 * 6. PHÂN LOẠI MÃ LỖI (Fault Types F01..F10 - Theo mục 2.1 Đặc tả)
 * ========================================================================== */
typedef enum {
    RTSFR_FAULT_NONE = 0,
    RTSFR_FAULT_NO_PROGRESS,        /* F01: Không báo tiến độ trong cửa sổ timeout */
    RTSFR_FAULT_BUSY_LOOP,          /* F02: Vòng lặp bận nhưng tick/supervisor còn chạy */
    RTSFR_FAULT_SELF_REPORTED,      /* F03: Task tự phát hiện lỗi nội bộ và báo cáo */
    RTSFR_FAULT_EXPECTED_WAIT,      /* F04/F10: Đang chờ hợp lệ (không phải lỗi thật) */
    RTSFR_FAULT_REPEATED_FAILURE,   /* F07: Lỗi lặp lại vượt ngưỡng hoặc verify thất bại */
    RTSFR_FAULT_KERNEL_STALL,       /* F08: Kernel/Tick kẹt (dành cho IWDG fallback) */
    RTSFR_FAULT_STACK_OVERFLOW,     /* F09a: Tràn ngăn xếp (Untrusted state -> Reset) */
    RTSFR_FAULT_HARD_FAULT,         /* F09b: Lỗi phần cứng/con trỏ (Untrusted -> Reset) */
    RTSFR_FAULT_CORRUPTION          /* F09c: Sai lệch bất biến kernel/TCB -> Reset */
} rtsfr_fault_t;

/* ==========================================================================
 * 7. MỨC ĐỘ NGHIÊM TRỌNG CỦA LỖI (Severity - Do M2 chuẩn hóa)
 * ========================================================================== */
typedef enum {
    RTSFR_SEV_NONE = 0,         /* Không có lỗi (chờ hợp lệ hoặc báo động giả) */
    RTSFR_SEV_TRANSIENT,        /* Lỗi cục bộ tạm thời, không giữ tài nguyên nguy hiểm */
    RTSFR_SEV_RECOVERABLE_RES,  /* Lỗi có giữ tài nguyên nhưng thu hồi tự động được */
    RTSFR_SEV_HAZARDOUS_RES,    /* Lỗi khi đang giữ Mutex/IO/Tài nguyên không rõ */
    RTSFR_SEV_PERSISTENT,       /* Lỗi tái diễn liên tục, đã cạn ngân sách retry */
    RTSFR_SEV_FATAL             /* Lỗi làm mất tính tin cậy của Kernel/RAM */
} rtsfr_severity_t;

/* ==========================================================================
 * 8. HÀNH ĐỘNG QUYẾT ĐỊNH CỦA POLICY (Actions - Theo mục 5.4 Đặc tả)
 * ========================================================================== */
typedef enum {
    RTSFR_ACT_IGNORE = 0,       /* Bỏ qua (Event cũ, trùng lặp, hoặc chờ hợp lệ) */
    RTSFR_ACT_OBSERVE,          /* Tiếp tục theo dõi thêm (chưa kết luận lỗi) */
    RTSFR_ACT_RESTART,          /* Cho phép phục hồi cục bộ (Cleanup nếu có -> Recreate) */
    RTSFR_ACT_DEFER,            /* Tạm hoãn phục hồi (hệ thống đang bận xử lý) */
    RTSFR_ACT_SAFE_MODE,        /* Chặn restart, đưa hệ thống về trạng thái an toàn */
    RTSFR_ACT_REQUEST_RESET     /* Yêu cầu reset toàn bộ MCU (lỗi không thể tin cậy) */
} rtsfr_action_t;

/* ==========================================================================
 * 9. MÃ LÝ DO GIẢI THÍCH QUYẾT ĐỊNH (Reason Codes - Mọi nhánh M2 phải có)
 * ========================================================================== */
typedef enum {
    RTSFR_REASON_NONE = 0x0000,

    /* Nhóm lý do trả về IGNORE / OBSERVE */
    RTSFR_REASON_INVALID_INPUT          = 0x0101, /* Con trỏ NULL hoặc sai task_id */
    RTSFR_REASON_STALE_GENERATION       = 0x0102, /* Event thuộc về generation cũ */
    RTSFR_REASON_DUPLICATE_EVENT        = 0x0103, /* Event ID đã được xử lý rồi */
    RTSFR_REASON_LEGITIMATE_WAIT        = 0x0104, /* Task đang trong expected-wait (F04/F10) */
    RTSFR_REASON_FALSE_ALARM_RECOVERED  = 0x0105, /* Task đã kịp báo progress trở lại */
    RTSFR_REASON_ALREADY_IN_SAFE_MODE   = 0x0106, /* Task/Hệ thống đã ở SAFE_MODE */

    /* Nhóm lý do trả về DEFER */
    RTSFR_REASON_RECOVERY_IN_PROGRESS   = 0x0201, /* Đang trong quá trình phục hồi */

    /* Nhóm lý do trả về RESTART (An toàn) */
    RTSFR_REASON_SAFE_NO_RESOURCE       = 0x0301, /* F01/F02/F03: Không giữ tài nguyên nào */
    RTSFR_REASON_SAFE_BUFFER_RECLAIM    = 0x0302, /* F05: Giữ managed buffer thu hồi được */

    /* Nhóm lý do trả về SAFE_MODE (Chặn Restart vì không an toàn) */
    RTSFR_REASON_UNSAFE_MUTEX_HELD      = 0x0401, /* F06: Đang giữ Mutex, cấm auto-unlock */
    RTSFR_REASON_UNSAFE_UNKNOWN_RES     = 0x0402, /* Có tài nguyên không xác định (unknown > 0) */
    RTSFR_REASON_UNSAFE_INFLIGHT_IO     = 0x0403, /* Đang có truyền thông/DMA/IO dở dang */
    RTSFR_REASON_UNSAFE_CANNOT_RECLAIM  = 0x0404, /* M3 báo can_reclaim_all == false */
    RTSFR_REASON_RETRY_EXHAUSTED        = 0x0405, /* F07: Vượt quá số lần restart trong cửa sổ */
    RTSFR_REASON_HIGH_CRIT_POLICY       = 0x0406, /* Task HIGH criticality không cho phép mạo hiểm */
    RTSFR_REASON_RECOVERY_FAILED        = 0x0407, /* M4 báo tạo lại task hoặc verify thất bại */

    /* Nhóm lý do trả về REQUEST_RESET (Lỗi nghiêm trọng mức Kernel/Phần cứng) */
    RTSFR_REASON_UNTRUSTED_STATE        = 0x0501, /* F09: Stack overflow / HardFault / Corruption */
    RTSFR_REASON_KERNEL_STALL           = 0x0502, /* F08: Kernel ngừng phản hồi */
    RTSFR_REASON_HIGH_CRIT_EXHAUSTED    = 0x0503  /* Task HIGH criticality hết lượt cứu -> Reset */
} rtsfr_reason_code_t;

/* ==========================================================================
 * 10. KẾT QUẢ PHỤC HỒI TỪ MODULE 4 BÁO VỀ (Theo mục 4.3 & 6.3 Phân công)
 * ========================================================================== */
typedef enum {
    RTSFR_OUTCOME_NONE = 0,
    RTSFR_OUTCOME_SUCCESS,              /* Thế hệ mới đã chạy và báo first progress (RECOVERY_OK) */
    RTSFR_OUTCOME_ABORTED_FALSE_ALARM,  /* Hủy phục hồi vì phát hiện báo động giả kịp thời */
    RTSFR_OUTCOME_BLOCKED_UNSAFE_RES,   /* Revalidate sau khi dừng task thấy tài nguyên unsafe */
    RTSFR_OUTCOME_CLEANUP_FAILED,       /* Dọn dẹp managed buffer thất bại */
    RTSFR_OUTCOME_CREATE_FAILED,        /* Thiếu heap (OOM) khi tạo lại task */
    RTSFR_OUTCOME_VERIFY_TIMEOUT,       /* Thế hệ mới tạo xong nhưng không báo first progress */
    RTSFR_OUTCOME_ENTERED_SAFE_MODE     /* Đã chuyển sang SAFE_MODE thành công */
} rtsfr_recovery_outcome_t;

/* ==========================================================================
 * 11. BA CẤU TRÚC GIAO TIẾP CHÍNH GIỮA CÁC MODULE (Theo mục 5.2 - 5.4 Đặc tả)
 * ========================================================================== */

/* Giao thức M1 (Việt Anh) -> M2 (Vũ Long): Sự kiện báo lỗi */
typedef struct {
    uint8_t         task_id;        /* ID ổn định của task (1..configRTSFR_MAX_TASKS) */
    uint16_t        generation;     /* Thế hệ của instance task lúc phát hiện lỗi */
    uint32_t        event_id;       /* Mã sự kiện duy nhất trong phiên chạy */
    TickType_t      observed_tick;  /* Thời điểm (tick) ghi nhận lỗi */
    rtsfr_fault_t   fault_type;     /* Loại lỗi (F01..F10) */
    uint32_t        evidence;       /* Bằng chứng (VD: số tick trễ hoặc mã lỗi phụ) */
} rtsfr_fault_event_t;

/* Giao thức M3 (Hồng Long) -> M2 (Vũ Long) & M4 (Tú): Bản chụp tài nguyên */
typedef struct {
    uint8_t         task_id;                /* ID ổn định của task */
    uint16_t        generation;             /* Thế hệ của task lúc chụp tài nguyên */
    uint8_t         mutex_held_count;       /* Số lượng FreeRTOS Mutex đang giữ */
    uint8_t         managed_buffers_owned;  /* Số lượng Managed Buffer độc quyền đang giữ */
    uint8_t         unknown_resource_count; /* Số tài nguyên không rõ trạng thái */
    bool            has_inflight_io;        /* Có ngoại vi/DMA/IO đang truyền dở không */
    bool            can_reclaim_all;        /* M3 xác nhận mọi tài nguyên đều thu hồi an toàn */
} rtsfr_resource_summary_t;

/* Giao thức M2 (Vũ Long) -> M4 (Tú) & M5 (Thủy): Quyết định xử lý */
typedef struct {
    uint32_t            event_id;       /* Khớp với event_id của fault_event */
    uint8_t             task_id;        /* ID của task cần xử lý */
    uint16_t            generation;     /* Thế hệ của task mà quyết định này áp dụng */
    rtsfr_action_t      action;         /* Hành động: IGNORE / RESTART / SAFE_MODE / RESET... */
    rtsfr_severity_t    severity;       /* Mức độ nghiêm trọng sau khi phân loại */
    uint16_t            reason_code;    /* Mã giải thích lý do (rtsfr_reason_code_t) */
} rtsfr_decision_t;

#ifdef __cplusplus
}
#endif

#endif /* RTSFR_TYPES_H */
