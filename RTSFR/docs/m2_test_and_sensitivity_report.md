# BÁO CÁO KIỂM THỬ UNIT TEST (M2-07) & PHÂN TÍCH ĐỘ NHẠY THAM SỐ (M2-08)

> **Module:** Module 2 — Fault Classification & Recovery Policy  
> **Thành viên phụ trách:** Đinh Vũ Long  
> **Mã nguồn kiểm thử:** `test_m2_policy.c`, `rtsfr_policy.c`, `rtsfr_policy.h`

---

## 1. Kết quả kiểm thử Host Unit Tests (M2-07)

Bộ kiểm thử độc lập phần cứng (`test_m2_policy.c`) gồm **18 kịch bản kiểm thử tự động** (vượt mức tối thiểu 15 kịch bản theo yêu cầu `M2-07`), bao phủ toàn bộ 6 cổng an toàn, các điều kiện biên và phối hợp vòng kín với `rtsfr_recovery.c` (Module 4):

| Test ID | Nội dung kiểm thử | Kỳ vọng (`Expected`) | Kết quả (`Actual`) | Trạng thái |
| :--- | :--- | :--- | :--- | :--- |
| **UT01** | Kiểm tra con trỏ `NULL`, `event_id == 0`, `task_id` ngoài biên (`0`, `MAX+1`) và task chưa đăng ký | `RTSFR_ACT_IGNORE` (`RTSFR_POL_REASON_INVALID_INPUT`) | Khớp 100% | **PASS** |
| **UT02** | Sự kiện thuộc thế hệ cũ (`fault->generation != active_generation` — `TC10` / Invariant `I2`) | `RTSFR_ACT_IGNORE` (`RTSFR_POL_REASON_STALE_GENERATION`) | Khớp 100% | **PASS** |
| **UT03** | Sự kiện bị gửi trùng `event_id` (Chống khởi động lại 2 lần cùng 1 lỗi) | `RTSFR_ACT_IGNORE` (`RTSFR_POL_REASON_DUPLICATE_EVENT`) | Khớp 100% | **PASS** |
| **UT04** | Task đang chờ hợp lệ (`RTSFR_FAULT_EXPECTED_WAIT` — `F04`/`F10`/`TC04`) | `RTSFR_ACT_IGNORE` (`RTSFR_POL_REASON_LEGITIMATE_WAIT`), `retry_count == 0` | Khớp 100% | **PASS** |
| **UT05** | Lỗi nghiêm trọng không tin cậy (`F09`: `STACK_OVERFLOW`, `HARD_FAULT`, `CORRUPTION`) | `RTSFR_ACT_REQUEST_RESET` (`RTSFR_POL_REASON_UNTRUSTED_STATE`) | Khớp 100% | **PASS** |
| **UT06** | Lỗi kẹt Kernel / Scheduler Stall (`F08` / `TC12`) | `RTSFR_ACT_REQUEST_RESET` (`RTSFR_POL_REASON_KERNEL_STALL`) | Khớp 100% | **PASS** |
| **UT07** | Tạm hoãn khi Task đang trong tiến trình phục hồi (`QUIESCING`/`CLEANUP`/`RECREATE`/`VERIFYING`) | `RTSFR_ACT_DEFER` (`RTSFR_POL_REASON_RECOVERY_IN_PROGRESS`) | Khớp 100% | **PASS** |
| **UT08** | Bỏ qua khi Task đã ở trạng thái `RTSFR_TASK_SAFE_MODE` | `RTSFR_ACT_IGNORE` (`RTSFR_POL_REASON_ALREADY_IN_SAFE_MODE`) | Khớp 100% | **PASS** |
| **UT09** | Phục hồi có chọn lọc khi Task lỗi tạm thời không giữ tài nguyên (`F01`/`F02`/`TC02`/`TC03`) | `RTSFR_ACT_RESTART` (`RTSFR_POL_REASON_SAFE_NO_RESOURCE`) | Khớp 100% | **PASS** |
| **UT10** | Phục hồi khi Task giữ Managed Buffer độc quyền thu hồi an toàn (`F05` / `TC05`) | `RTSFR_ACT_RESTART` (`RTSFR_POL_REASON_SAFE_BUFFER_RECLAIM`) | Khớp 100% | **PASS** |
| **UT11** | Chặn Restart khi Task đang giữ FreeRTOS Mutex (`F06` / `TC06` / Invariant `I4`) | `RTSFR_ACT_SAFE_MODE` (`RTSFR_POL_REASON_UNSAFE_MUTEX_HELD`) | Khớp 100% | **PASS** |
| **UT12** | Chặn Restart khi `unknown_resource_count > 0` hoặc lệch `generation` của bản chụp tài nguyên | `RTSFR_ACT_SAFE_MODE` (`RTSFR_POL_REASON_UNSAFE_UNKNOWN_RES`) | Khớp 100% | **PASS** |
| **UT13** | Chặn Restart khi đang có truyền thông ngoại vi/DMA dở dang (`has_inflight_io == true`) | `RTSFR_ACT_SAFE_MODE` (`RTSFR_POL_REASON_UNSAFE_INFLIGHT_IO`) | Khớp 100% | **PASS** |
| **UT14** | Chặn Restart khi Module 3 báo `can_reclaim_all == false` | `RTSFR_ACT_SAFE_MODE` (`RTSFR_POL_REASON_UNSAFE_CANNOT_RECLAIM`) | Khớp 100% | **PASS** |
| **UT15** | Cạn ngân sách Retry trong cửa sổ 60s đối với Task `LOW`/`MEDIUM` (`F07` / `TC07` / Invariant `I8`) | Cho phép 2 lần `RESTART`, lỗi thứ 3 chuyển `RTSFR_ACT_SAFE_MODE` | Khớp 100% | **PASS** |
| **UT16** | Cạn ngân sách Retry trong cửa sổ 60s đối với Task `HIGH` criticality (`ControlTask`) | Lỗi thứ 3 chuyển `RTSFR_ACT_REQUEST_RESET` (`HIGH_CRIT_EXHAUSTED`) | Khớp 100% | **PASS** |
| **UT17** | Làm mới cửa sổ Retry sau 60s & An toàn tràn bộ đếm 32-bit `TickType_t` (`0xFFFF0000` -> wrap-around) | Reset `retry_count = 0` và cho phép `RESTART` bình thường | Khớp 100% | **PASS** |
| **UT18** | Tích hợp callback `rtsfr_policy_on_recovery_result()` với `rtsfr_recovery.c` của Module 4 | Hoàn trả lượt retry khi rollback báo động giả; tăng `generation` khi `RECOVERY_OK`; khóa `SAFE_MODE` khi `SELF_RECOVERY_FORBIDDEN` | Khớp 100% | **PASS** |

---

## 2. Phân tích độ nhạy tham số Timeout & Retry Policy (M2-08)

Thực nghiệm mô phỏng chuỗi sự kiện gồm **1 lần chờ hợp lệ (`Expected Wait`) + 5 lỗi tạm thời liên tiếp cách nhau mỗi 15.000 ms (15 giây)** trên 9 tổ hợp tham số `(max_restarts, retry_window_ms)`:

| `max_restarts` | `retry_window_ms` (ms) | Số báo động giả (`False Alarm`) | Số lần `RESTART` được cấp phép | Thời điểm chuyển `SAFE_MODE` | Đánh giá hành vi hệ thống |
| :---: | :---: | :---: | :---: | :---: | :--- |
| **1** | `10000` (10s) | 0 | 5 | Không bị chặn | Cửa sổ 10s ngắn hơn khoảng cách lỗi 15s nên bộ đếm luôn tự làm mới; không phát hiện được lỗi lặp chu kỳ 15s. |
| **1** | `30000` (30s) | 0 | 1 | **Ở lỗi thứ 2** (t = 16s) | Quá nhạy: chỉ cho phép 1 lần thử lại, dễ đẩy hệ thống vào `SAFE_MODE` sớm khi có nhiễu thoáng qua. |
| **1** | `60000` (60s) | 0 | 1 | **Ở lỗi thứ 2** (t = 16s) | Quá nhạy tương tự cấu hình (1, 30s). |
| **2** | `10000` (10s) | 0 | 5 | Không bị chặn | Cửa sổ 10s quá ngắn đối với lỗi lặp lại mỗi 15s. |
| **2** | `30000` (30s) | 0 | 5 | Không bị chặn | Tại lỗi thứ 3 (t = 31s), thời gian tính từ lỗi 1 (t = 1s) là `30000 ms == retry_window_ms` nên cửa sổ vừa kịp reset. |
| **2 (Mặc định)** | **`60000` (60s)** | **0** | **2** | **Ở lỗi thứ 3** (t = 31s) | **Cân bằng tối ưu (Baseline V2):** Chịu được 2 lỗi thoáng qua, chặn đứng vòng lặp lỗi ở lần thứ 3 trong 60s (`TC07`). |
| **3** | `10000` (10s) | 0 | 5 | Không bị chặn | Cửa sổ quá ngắn, cho phép restart liên tục. |
| **3** | `30000` (30s) | 0 | 5 | Không bị chặn | Cửa sổ 30s tự reset trước khi chạm ngưỡng 3 lần lỗi. |
| **3** | `60000` (60s) | 0 | 3 | **Ở lỗi thứ 4** (t = 46s) | Phù hợp cho các task ít quan trọng (`RTSFR_CRIT_LOW`) trong môi trường nhiễu cao nhưng tốn thêm thời gian CPU recovery. |
