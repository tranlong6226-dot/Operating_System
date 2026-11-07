# Module 2: Fault Classification & Recovery Policy

## 1. Nguyên tắc thiết kế Chính sách (Policy Design Principles)

Module 2 (`Fault Classification & Recovery Policy`) hoạt động như một **hàm thuần túy tất định (deterministic pure function)** kết hợp với bảng lịch sử thử lại (`retry history`) kích thước cố định:
* **Không tác dụng phụ (No Side Effects):** Không gọi API tạo/xóa task của FreeRTOS, không gọi hàm phần cứng HAL/UART, không cấp phát bộ nhớ động.
* **Nghĩa vụ chứng minh an toàn (Safety Proof Obligation):** Chỉ trả về quyết định `RTSFR_ACT_RESTART` khi chứng minh được mọi tài nguyên task đang giữ đều an toàn để thu hồi. Nếu có bất kỳ nghi ngờ nào (`mutex_held_count > 0`, `unknown_resource_count > 0`, `has_inflight_io == true`, hoặc `can_reclaim_all == false`), hệ thống bắt buộc từ chối `RESTART` và chuyển sang `RTSFR_ACT_SAFE_MODE` hoặc `RTSFR_ACT_REQUEST_RESET`.

## 2. Sơ đồ 6 Cổng kiểm tra an toàn tuần tự (Sequential Safety Gates)

Khi hàm `rtsfr_policy_evaluate(fault, resource)` được gọi, dữ liệu sẽ đi qua **6 cổng lọc theo thứ tự ưu tiên tuyệt đối** từ trên xuống dưới:

```text
[Nhận Fault Event (M1) + Resource Summary (M3)]
       │
       ▼
┌─────────────────────────────────────────────────────────────┐
│ CỔNG 1: Kiểm tra tính hợp lệ & Chống sự kiện cũ (Stale/Dup) │
│ - Con trỏ NULL / task_id ngoài khoảng (1..MAX_TASKS)?       │──(Đúng)──► IGNORE (INVALID_INPUT)
│ - fault->generation != active_generation của task?          │──(Đúng)──► IGNORE (STALE_GENERATION)
│ - fault->event_id == last_processed_event_id?               │──(Đúng)──► IGNORE (DUPLICATE_EVENT)
└──────────────────────────────┬──────────────────────────────┘
                               │ (Qua Cổng 1)
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ CỔNG 2: Kiểm tra lỗi nghiêm trọng mức Kernel / Phần cứng    │
│ - Lỗi thuộc F09 (Stack Overflow / HardFault / Corruption)?  │──(Đúng)──► REQUEST_RESET (UNTRUSTED_STATE)
│ - Lỗi thuộc F08 (Kernel Stall)?                             │──(Đúng)──► REQUEST_RESET (KERNEL_STALL)
└──────────────────────────────┬──────────────────────────────┘
                               │ (Qua Cổng 2)
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ CỔNG 3: Lọc chờ hợp lệ & Trạng thái hiện tại của Task       │
│ - fault_type == RTSFR_FAULT_EXPECTED_WAIT (F04/F10)?        │──(Đúng)──► IGNORE (LEGITIMATE_WAIT)
│ - Task hiện đã ở trạng thái SAFE_MODE?                      │──(Đúng)──► IGNORE (ALREADY_IN_SAFE_MODE)
│ - Task đang trong quá trình QUIESCING/CLEANUP/RECREATE?     │──(Đúng)──► DEFER  (RECOVERY_IN_PROGRESS)
└──────────────────────────────┬──────────────────────────────┘
                               │ (Qua Cổng 3)
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ CỔNG 4: Kiểm tra Ngân sách thử lại (Retry Budget Window)    │
│ - Nếu (now - window_start) >= RETRY_WINDOW: reset count = 0 │
│ - Nếu retry_count >= configRTSFR_MAX_RESTARTS (VD: >= 2):   │
│     + Nếu criticality == HIGH                               │──(Đúng)──► REQUEST_RESET (HIGH_CRIT_EXHAUSTED)
│     + Nếu criticality == LOW / MEDIUM                       │──(Đúng)──► SAFE_MODE (RETRY_EXHAUSTED)
└──────────────────────────────┬──────────────────────────────┘
                               │ (Qua Cổng 4: Còn lượt Retry)
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ CỔNG 5: Kiểm tra An toàn Tài nguyên (Resource Safety Gate)  │
│ - resource->generation != fault->generation?                │──(Đúng)──► SAFE_MODE (UNSAFE_UNKNOWN_RES)
│ - resource->mutex_held_count > 0? (F06)                     │──(Đúng)──► SAFE_MODE (UNSAFE_MUTEX_HELD)
│ - resource->unknown_resource_count > 0?                     │──(Đúng)──► SAFE_MODE (UNSAFE_UNKNOWN_RES)
│ - resource->has_inflight_io == true?                        │──(Đúng)──► SAFE_MODE (UNSAFE_INFLIGHT_IO)
│ - resource->can_reclaim_all == false?                       │──(Đúng)──► SAFE_MODE (UNSAFE_CANNOT_RECLAIM)
└──────────────────────────────┬──────────────────────────────┘
                               │ (Qua Cổng 5: Tài nguyên An toàn)
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ CỔNG 6: Cấp phép Phục hồi có chọn lọc (Selective Restart)   │
│ - Nếu managed_buffers_owned > 0 (và can_reclaim_all==true): │──(Đúng)──► RESTART (SAFE_BUFFER_RECLAIM)
│ - Nếu không giữ tài nguyên nào (managed_buffers_owned == 0):│──(Đúng)──► RESTART (SAFE_NO_RESOURCE)
└─────────────────────────────────────────────────────────────┘
```

## 3. Bảng quyết định tất định (Deterministic Decision Matrix)

Bảng dưới đây quy định chính xác đầu ra của hàm `rtsfr_policy_evaluate()` cho tất cả 17 kịch bản kiểm thử (bao phủ toàn bộ ma trận lỗi `F01`–`F10` và bộ test `TC01`–`TC14`):

| STT | Nhóm tình huống | Điều kiện đầu vào (`FaultEvent` + `ResourceSummary` + `TaskState`) | Severity phân loại | Hành động (`Action`) | Mã lý do (`Reason Code`) | Test Case liên quan |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **01** | Tham số không hợp lệ | `fault == NULL` hoặc `resource == NULL` hoặc `task_id` ngoài `1..MAX_TASKS` | `SEV_NONE` | `ACT_IGNORE` | `REASON_INVALID_INPUT` | Unit Test M2 |
| **02** | Sự kiện của thế hệ cũ | `fault->generation != task_policy_state[id].active_generation` | `SEV_NONE` | `ACT_IGNORE` | `REASON_STALE_GENERATION` | `TC10` (Invariant `I2`) |
| **03** | Sự kiện bị gửi trùng | `fault->event_id == task_policy_state[id].last_event_id` | `SEV_NONE` | `ACT_IGNORE` | `REASON_DUPLICATE_EVENT` | `TC10`, `M1-06` |
| **04** | Chờ hợp lệ (`F04`, `F10`) | `fault_type == RTSFR_FAULT_EXPECTED_WAIT` hoặc `RTSFR_FAULT_NONE` | `SEV_NONE` | `ACT_IGNORE` | `REASON_LEGITIMATE_WAIT` | `TC01`, `TC04` |
| **05** | Task đã ở Safe Mode | `task_state == RTSFR_TASK_SAFE_MODE` | `SEV_HAZARDOUS_RES` | `ACT_IGNORE` | `REASON_ALREADY_IN_SAFE_MODE` | Unit Test M2 |
| **06** | Đang phục hồi dở | `task_state` đang là `QUIESCING`, `CLEANUP`, `RECREATE` hoặc `VERIFYING` | `SEV_TRANSIENT` | `ACT_DEFER` | `REASON_RECOVERY_IN_PROGRESS` | `M2-06` |
| **07** | Tràn Stack / HardFault (`F09`) | `fault_type` thuộc `STACK_OVERFLOW`, `HARD_FAULT`, `CORRUPTION` | `SEV_FATAL` | `ACT_REQUEST_RESET` | `REASON_UNTRUSTED_STATE` | `F09` |
| **08** | Kẹt nhân RTOS (`F08`) | `fault_type == RTSFR_FAULT_KERNEL_STALL` | `SEV_FATAL` | `ACT_REQUEST_RESET` | `REASON_KERNEL_STALL` | `TC12` (`F08`) |
| **09** | Cạn ngân sách Retry (Task thường) | `retry_count >= MAX_RESTARTS` trong `RETRY_WINDOW` & `criticality != HIGH` | `SEV_PERSISTENT` | `ACT_SAFE_MODE` | `REASON_RETRY_EXHAUSTED` | `TC07` (`F07`, `I8`) |
| **10** | Cạn ngân sách Retry (Task quan trọng) | `retry_count >= MAX_RESTARTS` trong `RETRY_WINDOW` & `criticality == HIGH` | `SEV_FATAL` | `ACT_REQUEST_RESET` | `REASON_HIGH_CRIT_EXHAUSTED` | `TC07` (`F07`) |
| **11** | Lệch thế hệ bản chụp tài nguyên | `resource->generation != fault->generation` | `SEV_HAZARDOUS_RES` | `ACT_SAFE_MODE` | `REASON_UNSAFE_UNKNOWN_RES` | Invariant `I4` |
| **12** | Task lỗi khi đang giữ Mutex (`F06`) | `resource->mutex_held_count > 0` | `SEV_HAZARDOUS_RES` | `ACT_SAFE_MODE` | `REASON_UNSAFE_MUTEX_HELD` | `TC06` (`F06`, `I4`) |
| **13** | Có tài nguyên lạ không rõ | `resource->unknown_resource_count > 0` | `SEV_HAZARDOUS_RES` | `ACT_SAFE_MODE` | `REASON_UNSAFE_UNKNOWN_RES` | `M2-05`, `M3-07` |
| **14** | Đang có I/O hoặc DMA bay (`In-flight`) | `resource->has_inflight_io == true` | `SEV_HAZARDOUS_RES` | `ACT_SAFE_MODE` | `REASON_UNSAFE_INFLIGHT_IO` | `M2-05` (`F05` unsafe) |
| **15** | Không thể thu hồi hết tài nguyên | `resource->can_reclaim_all == false` | `SEV_HAZARDOUS_RES` | `ACT_SAFE_MODE` | `REASON_UNSAFE_CANNOT_RECLAIM` | `M2-05` |
| **16** | Lỗi giữ Buffer độc quyền an toàn (`F05`) | `F01/F02/F03`, còn lượt retry, `mutex==0`, `unknown==0`, `io==false`, `can_reclaim_all==true`, `managed_buffers_owned > 0` | `SEV_RECOVERABLE_RES` | `ACT_RESTART` | `REASON_SAFE_BUFFER_RECLAIM` | `TC05` (`F05`) |
| **17** | Lỗi treo đơn thuần không giữ tài nguyên (`F01`/`F02`/`F03`) | `F01/F02/F03`, còn lượt retry, `mutex==0`, `unknown==0`, `io==false`, `can_reclaim_all==true`, `managed_buffers_owned == 0` | `SEV_TRANSIENT` | `ACT_RESTART` | `REASON_SAFE_NO_RESOURCE` | `TC02`, `TC03` (`F01`, `F02`) |

## 4. Quy tắc tính cửa sổ thời gian Retry Budget (Chống lỗi tràn số `Tick Wrap-around`)

* Mỗi task có 2 biến theo dõi lịch sử thử lại: `retry_count` (số lần đã restart trong cửa sổ hiện tại) và `window_start_tick` (thời điểm bắt đầu tính cửa sổ).
* Khi có lỗi mới tại thời điểm `now = fault->observed_tick`:
  * Nếu `retry_count == 0`, gán `window_start_tick = now`.
  * Nếu `retry_count > 0`, kiểm tra khoảng thời gian trôi qua bằng phép trừ không dấu:
    `elapsed_ticks = now - window_start_tick`
  * Nếu `elapsed_ticks >= pdMS_TO_TICKS(configRTSFR_RETRY_WINDOW_MS)` (tức là đã qua 60 giây mà task chạy ổn định), ngân sách thử lại được làm mới: đặt lại `retry_count = 0` và `window_start_tick = now`.
  * Nếu `elapsed_ticks < window_ticks` và `retry_count >= configRTSFR_MAX_RESTARTS` (đã restart 2 lần trong vòng chưa đầy 60 giây), từ chối `RESTART` và chuyển sang `SAFE_MODE` (hoặc `REQUEST_RESET` nếu task có `criticality == HIGH`).
