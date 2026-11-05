# Lộ trình và Kế hoạch thực hiện Module 1 — Việt Anh

> **Module 1:** Fault Detection & Kernel Integration  
> **Phụ trách:** Việt Anh  
> **Thời gian:** 8 tuần (Học kỳ 2026 - Môn Hệ điều hành HUST)  
> **Mục tiêu:** Giám sát tiến độ thực chất của task, phát hiện kịp thời task bị kẹt/treo, sinh fault event chuẩn xác kèm generation, phân biệt chờ hợp lệ với lỗi, tích hợp sâu vào vòng đời FreeRTOS Kernel.

---

## 1. Bảng công việc chi tiết (Backlog Module 1)

| Task ID | Tên công việc | Chi tiết kỹ thuật | Đầu ra cụ thể | Tuần | Trạng thái |
|---|---|---|---|---|---|
| **M1-01** | Khảo sát kernel & vòng đời task | Phân tích `tasks.c`, TCB, trạng thái task, tick counter, các macro trace hook sẵn có của FreeRTOS | [`docs/m1_kernel_analysis.md`](file:///d:/Study/HUST/2026/HDH/freertos-fault-recovery/docs/m1_kernel_analysis.md) | W1 | Đang tiến hành |
| **M1-02** | Thiết kế Task Registry | Định nghĩa struct `rtsfr_task_record_t`, `rtsfr_registry_t` cố định statically, hàm đăng ký, tra cứu theo `task_id` | `rtsfr_types.h`, `rtsfr_detect.h` | W1 | Đang tiến hành |
| **M1-03** | Triển khai `rtsfr_report_progress` | Ghi nhận timestamp tick và sequence number an toàn concurrency (critical section ngắn hoặc atomic) | `rtsfr_detect.c` + Host Unit Test | W2 | Chờ W1 |
| **M1-04** | Hợp đồng Chờ hợp lệ (Expected-Wait) | `rtsfr_begin_expected_wait` & `rtsfr_end_expected_wait`, gắn cờ chờ hợp lệ kèm deadline riêng | Code `rtsfr_detect.c` + Host Test | W2 | Chờ W1 |
| **M1-05** | Bộ quét giám sát (Monitor Scan) | Hàm `rtsfr_detect_scan()` duyệt registry, phát hiện task quá hạn báo tiến độ -> sinh sự kiện `F01` (No Progress) / `F02` (Busy Loop) | Code `rtsfr_detect.c` | W2 | Chờ W1 |
| **M1-06** | Hàng đợi sự kiện & Chống Stale Event | Cơ chế Ring Buffer cho Fault Event, chống sinh sự kiện trùng (duplicate), loại bỏ event thuộc thế hệ cũ | Code `rtsfr_detect.c` + Test | W3 | Chưa |
| **M1-07** | Tích hợp Lifecycle hooks vào `tasks.c` | Đặt hook vào `xTaskCreate` / `vTaskDelete` để cập nhật Registry tự động, tránh dangling `TaskHandle_t` | Patch `tasks.c` có `#if configUSE_RTSFR` | W3 | Chưa |
| **M1-08** | Kiểm thử Target TC01–TC04 | Phối hợp với Thủy (M5) chạy kiểm thử trên STM32: Bình thường (TC01), Không báo tiến độ (TC02), Vòng lặp bận (TC03), Chờ hợp lệ (TC04) | Báo cáo kiểm thử + log UART | W4 | Chưa |
| **M1-09** | Đo đạc và Tối ưu hóa Overhead | Đo thời gian chạy của hàm scan (CPU cycles / microseconds) khi số task tăng từ 1 -> 4 task | Bảng số liệu trong báo cáo kỹ thuật | W6-W7 | Chưa |

---

## 2. Giao diện hợp đồng với các Module khác

1. **Gửi cho Module 2 (Vũ Long - Policy):**
   - Sự kiện lỗi: `rtsfr_fault_event_t` chứa:
     - `task_id`: Định danh logic của task (0..MAX-1).
     - `generation`: Thế hệ hiện tại của task instance.
     - `fault_type`: `RTSFR_FAULT_NO_PROGRESS`, `RTSFR_FAULT_BUSY_LOOP`, v.v.
     - `detected_tick`: Thời điểm phát hiện lỗi.

2. **Nhận từ Module 4 (Tú - Recovery Manager):**
   - Khi task được tái sinh thành công: `rtsfr_detect_on_new_generation(task_id, new_handle, new_gen)`.
   - Giúp Registry cập nhật sang thế hệ mới, reset lại bộ đếm thời gian và xóa bỏ cờ lỗi cũ.

3. **Gửi cho Module 5 (Thủy - Testing & Trace):**
   - Trace hooks cho các sự kiện: `PROGRESS_REPORTED`, `EXPECTED_WAIT_START`, `EXPECTED_WAIT_END`, `FAULT_DETECTED`.

---

## 3. Tiêu chí nghiệm thu (Definition of Done cho Module 1)

- [ ] Task hoạt động bình thường liên tục 5 phút không bị báo nhầm lỗi (Zero False Positive).
- [ ] Giả lập task bị kẹt: Hệ thống phát hiện chính xác sau đúng khoảng thời gian cấu hình `timeout_ticks ± scan_period`.
- [ ] Task delay hoặc chờ Semaphore/Queue hợp lệ (có đăng ký expected wait) không bao giờ bị báo lỗi.
- [ ] Khi thế hệ task tăng (`generation++`), toàn bộ sự kiện của thế hệ cũ không bao giờ được gửi tiếp cho Module 2.
- [ ] Không có dynamic memory allocation trong hot-path; RAM sử dụng của Module 1 tĩnh, dưới 500 bytes.
- [ ] Có đầy đủ Unit test chạy trên Host (MinGW) và Firmware chạy thực nghiệm trên mạch Blue Pill STM32F103.
