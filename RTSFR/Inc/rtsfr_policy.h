#ifndef RTSFR_POLICY_H
#define RTSFR_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#include "rtsfr_recovery.h"
#include "rtsfr_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * 1. CẤU HÌNH THỜI GIAN CỬA SỔ RETRY TÍNH THEO TICKS
 * ========================================================================== */
#ifndef configRTSFR_RETRY_WINDOW_TICKS
#ifdef pdMS_TO_TICKS
#define configRTSFR_RETRY_WINDOW_TICKS pdMS_TO_TICKS(configRTSFR_RETRY_WINDOW_MS)
#else
/* Mặc định 1 tick = 1 ms khi chạy Host Unit Test không có FreeRTOS */
#define configRTSFR_RETRY_WINDOW_TICKS ((TickType_t)configRTSFR_RETRY_WINDOW_MS)
#endif
#endif

/* ==========================================================================
 * 2. CẤU TRÚC LƯU TRẠNG THÁI & LỊCH SỬ RETRY CỦA TỪNG TASK TRONG MODULE 2
 * ========================================================================== */
typedef struct
{
    bool                registered;         /* Task đã được đăng ký với M2 chưa */
    uint8_t             task_id;            /* ID ổn định (1..configRTSFR_MAX_TASKS) */
    uint16_t            active_generation;  /* Thế hệ đang chạy hợp lệ hiện tại */
    rtsfr_criticality_t criticality;        /* Mức độ quan trọng: LOW / MEDIUM / HIGH */
    rtsfr_task_state_t  state;              /* Trạng thái vòng đời theo góc nhìn Policy */
    uint8_t             retry_count;        /* Số lần đã cấp quyền RESTART trong cửa sổ */
    uint8_t             max_restarts;       /* Giới hạn restart tối đa trong cửa sổ */
    TickType_t          retry_window_ticks; /* Độ rộng cửa sổ thời gian tính bằng ticks */
    TickType_t          window_start_tick;  /* Thời điểm bắt đầu cửa sổ retry hiện tại */
    uint32_t            last_event_id;      /* Event ID gần nhất đã xử lý (chống trùng) */
    bool                has_last_event;     /* Cờ đánh dấu đã từng xử lý ít nhất 1 event */
    rtsfr_severity_t    last_severity;      /* Mức độ nghiêm trọng của lỗi gần nhất */
    uint16_t            last_reason_code;   /* Mã lý do của quyết định gần nhất */
} rtsfr_policy_task_state_t;

/* ==========================================================================
 * 3. CÁC HÀM KHỞI TẠO & CẤU HÌNH TASK CHO MODULE 2
 * ========================================================================== */

/*
 * Khởi tạo lại toàn bộ bảng trạng thái Policy và lịch sử Retry.
 * Context: Task/Init context, không gọi trong ISR.
 */
void rtsfr_policy_init(void);

/*
 * Đăng ký thông tin khởi đầu của một task được giám sát với Module 2.
 * - task_id: 1 .. configRTSFR_MAX_TASKS
 * - initial_generation: Thế hệ ban đầu (thường là 1)
 * - criticality: RTSFR_CRIT_LOW / MEDIUM / HIGH
 * Trả về: RTSFR_OK hoặc RTSFR_ERR_INVALID.
 */
rtsfr_status_t rtsfr_policy_register_task(uint8_t task_id,
                                          uint16_t initial_generation,
                                          rtsfr_criticality_t criticality);

/*
 * Tùy chỉnh giới hạn số lần restart và cửa sổ thời gian cho riêng một task.
 * Nếu không gọi hàm này, task dùng mặc định configRTSFR_MAX_RESTARTS (2 lần)
 * và configRTSFR_RETRY_WINDOW_TICKS (60000 ms).
 */
rtsfr_status_t rtsfr_policy_set_retry_policy(uint8_t task_id,
                                             uint8_t max_restarts,
                                             TickType_t retry_window_ticks);

/*
 * Cập nhật trạng thái hoặc generation mới từ M1/M4 (ví dụ khi hủy báo động giả
 * hoặc khi muốn đồng bộ trạng thái thủ công).
 */
rtsfr_status_t rtsfr_policy_set_task_state(uint8_t task_id,
                                           uint16_t generation,
                                           rtsfr_task_state_t new_state);

/* ==========================================================================
 * 4. HÀM CỐT LÕI: ĐÁNH GIÁ LỖI & RA QUYẾT ĐỊNH (M2-03, M2-04, M2-05, M2-06)
 * ========================================================================== */

/*
 * Đánh giá sự kiện lỗi (từ M1) kết hợp bản chụp tài nguyên (từ M3) để đưa ra
 * quyết định phục hồi tất định (deterministic decision).
 * - Không gọi API xóa/tạo task của FreeRTOS.
 * - Không gọi hàm phần cứng HAL/UART hay cấp phát động.
 */
rtsfr_decision_t rtsfr_policy_evaluate(const rtsfr_fault_event_t *fault,
                                       const rtsfr_resource_summary_t *resource);

/*
 * Kiểm tra xem quyết định từ M2 có cần chuyển tiếp sang hàm
 * rtsfr_recovery_request() của Module 4 (Tú) hay không.
 * Trả về true khi action thuộc: RESTART, SAFE_MODE, REQUEST_RESET.
 * Trả về false khi action thuộc: IGNORE, OBSERVE, DEFER.
 */
bool rtsfr_policy_should_invoke_recovery(const rtsfr_decision_t *decision);

/*
 * Phân loại mức độ nghiêm trọng (Severity) từ loại lỗi và bản chụp tài nguyên.
 */
rtsfr_severity_t rtsfr_policy_classify_severity(
    const rtsfr_fault_event_t *fault,
    const rtsfr_resource_summary_t *resource,
    bool retry_exhausted);

/* ==========================================================================
 * 5. CẬP NHẬT KẾT QUẢ PHỤC HỒI TỪ MODULE 4 & TRUY VẤN TRẠNG THÁI
 * ========================================================================== */

/*
 * Ghi nhận kết quả phục hồi theo đặc tả mục 4.3.
 */
void rtsfr_policy_record_result(uint8_t task_id,
                                rtsfr_recovery_outcome_t result,
                                TickType_t tick);

/*
 * Hook tương thích trực tiếp với rtsfr_recovery_result_fn trong rtsfr_recovery.h
 * để Module 4 (Tú) có thể gắn thẳng vào rtsfr_recovery_hooks_t.record_result.
 */
void rtsfr_policy_on_recovery_result(const rtsfr_recovery_result_t *result);

/*
 * Lấy bản sao trạng thái Policy hiện tại của một task để phục vụ kiểm thử / M5 trace.
 */
rtsfr_status_t rtsfr_policy_get_task_state(uint8_t task_id,
                                           rtsfr_policy_task_state_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RTSFR_POLICY_H */
