# TÀI LIỆU ĐẶC TẢ YÊU CẦU PHẦN MỀM
*(Software Requirements Specification — SRS)*

**Tên dự án:** Thiết kế và triển khai cơ chế phát hiện và phục hồi có chọn lọc lỗi tác vụ, có xét trạng thái tài nguyên, ở mức kernel FreeRTOS trên STM32 (RTSFR)  
**Tên tiếng Anh:** Resource-aware Task Selective Fault Recovery Framework for FreeRTOS on STM32  
**Mã đề tài:** RTSFR-HUST-2026  
**Phạm vi tài liệu:** Đặc tả chi tiết yêu cầu chức năng, yêu cầu phi chức năng, giao tiếp liên module, ma trận xử lý lỗi và kịch bản kiểm thử của toàn bộ phân hệ phần mềm nhúng RTSFR.

---

## 1. Giới thiệu và Phân hệ Phần mềm (Software Overview)

Khung phần mềm RTSFR là một giải pháp mở rộng nhân hệ điều hành thời gian thực FreeRTOS, được thiết kế để giải quyết bài toán phục hồi tác vụ có chọn lọc trên vi điều khiển hạn chế tài nguyên STM32F103C8T6. Hệ thống phân chia thành các phân hệ phần mềm với trách nhiệm chuyên biệt:

- **Phân hệ Mở rộng Nhân FreeRTOS (RTSFR Kernel Extension):**
  - **Module 1 — Fault Detection & Kernel Integration (`rtsfr_detect.c`, `rtsfr_detect.h`):** Quản lý Registry tác vụ tĩnh; cung cấp API atomic ghi nhận nhịp tim; hỗ trợ hợp đồng chờ hợp lệ (Expected-Wait Contract); phát hiện quá hạn tiến độ an toàn tràn số tick; phát sinh sự kiện lỗi gắn nhãn thế hệ (`generation-aware`).
  - **Module 2 — Fault Classification & Recovery Policy (`rtsfr_policy.c`, `rtsfr_policy.h`):** Định nghĩa máy trạng thái phân loại lỗi; tính toán ngân sách thử lại (retry budget trong sliding window); kết hợp thông tin lỗi và tóm tắt tài nguyên để đưa ra quyết định xử lý tất định (`RESTART`, `SAFE_MODE`, `REQUEST_RESET`, `IGNORE`).
  - **Module 3 — Resource Ownership Tracker (`rtsfr_resource.c`, `rtsfr_resource.h`):** Móc nối vào các điểm take/give mutex thành công trong `queue.c`; theo dõi danh sách Mutex mà từng task đang nắm giữ; quản lý một Managed Buffer Pool tĩnh có hợp đồng dọn dẹp xác thực; kết xuất snapshot tài nguyên nhất quán.
  - **Module 4 — Selective Recovery Manager (`rtsfr_recovery.c`, `rtsfr_recovery.h`):** Đóng vai trò hạt nhân của tác vụ `RTSFR_Supervisor`; thực thi chu trình cách ly tác vụ lỗi (quiescing), dọn dẹp tài nguyên (cleanup), xóa và tạo mới task instance với thế hệ mới (`generation + 1`), và tiến hành kiểm tra xác thực sau phục hồi (post-recovery verification).
  - **Module 5 — Fault Injection, Verification & Watchdog (`rtsfr_verify.c`, `rtsfr_trace.c`):** Chứa các kịch bản tiêm lỗi chủ động; kiểm tra tính đúng đắn của các bất biến hệ điều hành (invariants); đệm và truyền dữ liệu log bất đồng bộ qua UART; quản lý cơ chế nạp lại Independent Watchdog (IWDG) theo nhịp đập sức khỏe kernel.
- **Phân hệ Can thiệp Mã nguồn Kernel Gốc (FreeRTOS Core Patches):**
  - Các can thiệp tối thiểu được cô lập trong cặp chỉ thị `/* RTSFR BEGIN: ... */` và `/* RTSFR END */` tại `tasks.c` (móc nối lifecycle task, đồng bộ thế hệ khi xóa task) và `queue.c` (ghi nhận quyền sở hữu sau khi take/give mutex thành công).
- **Phân hệ Tác vụ Mẫu Ứng dụng (Workload Application Tasks):**
  - Gồm 3 tác vụ mẫu thực hiện luân phiên công việc để chứng minh tính ổn định của hệ thống khi có lỗi cục bộ phát sinh:
    - `Task 1 (Sensor Task - ID 0):` Đọc dữ liệu định kỳ, cập nhật tiến độ đều đặn, thực hiện các pha ngủ hợp lệ qua `rtsfr_begin_expected_wait()`.
    - `Task 2 (Comm Task - ID 1):` Giao tiếp dữ liệu, sử dụng Managed Buffer từ Pool, là mục tiêu tiêm lỗi vòng lặp vô hạn hoặc dừng tiến độ.
    - `Task 3 (Control/Safety Task - ID 2):` Điều khiển trạng thái hệ thống, sử dụng Mutex chung với Comm Task để kiểm chứng việc từ chối restart khi đang giữ Mutex.
- **Phân hệ Kiểm thử Đơn vị trên Máy chủ (Host TDD Test Harness):**
  - Được xây dựng bằng C thuần trên nền tảng GCC MinGW, cung cấp các mock API của FreeRTOS (`vTaskDelay`, `xTaskGetTickCount`, `taskENTER_CRITICAL`,...) để chạy kiểm thử hồi quy 100% tự động trước khi nạp code xuống firmware.

---

## 2. Đặc tả Giao diện Điều khiển & Ngữ cảnh Supervisor (System Control & Interface)

### 2.1. Ngữ cảnh Thực thi của Supervisor Task (`RTSFR_Supervisor`)

Mọi hành động chẩn đoán phức tạp và phục hồi tác vụ bắt buộc phải thực thi trong ngữ cảnh của một tác vụ kernel độc lập, không được chạy trực tiếp trong ngắt ISR hay Critical Section:
- **Tên tác vụ:** `RTSFR_Supervisor`
- **Độ ưu tiên (Priority):** Cao hơn tất cả các task ứng dụng được giám sát (`configMAX_PRIORITIES - 2`), nhưng thấp hơn các task dịch vụ ngắt thời gian thực tối khẩn cấp.
- **Dung lượng Stack:** 256 từ (tương đương 1024 bytes trên kiến trúc 32-bit Cortex-M3).
- **Chu kỳ đánh thức:** Định kỳ thức dậy mỗi 20 ms – 50 ms qua `vTaskDelayUntil()` hoặc được đánh thức ngay lập tức qua Direct Task Notification khi có lỗi khẩn cấp được phát hiện.
- **Quy tắc an toàn:** Supervisor không bao giờ được phép tranh chấp hay chiếm giữ bất kỳ Mutex nào của tầng ứng dụng.

### 2.2. Điểm tích hợp Hook trong FreeRTOS Kernel

Các điểm can thiệp vào mã nguồn gốc của FreeRTOS được giới hạn nghiêm ngặt theo bảng sau:

| Tệp nguồn Kernel | Vị trí can thiệp | Mục đích kỹ thuật | Ràng buộc an toàn |
|---|---|---|---|
| `Middlewares/.../tasks.c` | Sau khi `prvAddNewTaskToReadyList` thành công | Ghi nhận handle vào Registry nếu task được đăng ký trước | Không cấp phát heap, thời gian thực thi O(1) |
| `Middlewares/.../tasks.c` | Trong `vTaskDelete` trước khi giải phóng TCB | Gọi `rtsfr_detect_on_task_delete` để vô hiệu hóa handle | Không gọi hàm có khả năng block scheduler |
| `Middlewares/.../queue.c` | Cuối hàm `xQueueSemaphoreTake` khi trả về `pdPASS` | Gọi hook Module 3 ghi nhận Task ID đang nắm giữ Mutex | Chỉ thực hiện khi kiểu queue là Mutex (`ucQueueType == queueQUEUE_TYPE_MUTEX`) |
| `Middlewares/.../queue.c` | Cuối hàm `xQueueGenericSend` khi nhả Mutex thành công | Gọi hook Module 3 xóa quyền sở hữu Mutex khỏi bảng | Cập nhật atomic, bảo vệ bằng critical section cực ngắn |

### 2.3. Hàng đợi Sự kiện Lỗi Tĩnh (Static Fault Event Queue)

Giao tiếp giữa Module 1 (Phát hiện) và Module 2 (Chính sách) được tổ chức dưới dạng một Ring Buffer tĩnh kiểu FIFO:
- Kích thước hàng đợi: `configRTSFR_EVENT_QUEUE_SIZE = 8` phần tử.
- Thao tác đẩy (`push`) và lấy (`pop`) được bảo vệ bằng Critical Section ngắn (< 2 µs).
- Khi hàng đợi đầy, chính sách xử lý là: Ghi đè hoặc loại bỏ sự kiện mới nhất kèm theo việc bật cờ cảnh báo tràn hàng đợi `RTSFR_ERR_BUSY`, đảm bảo hệ thống không bao giờ bị treo cứng.

---

## 3. Yêu cầu Chức năng Phân hệ Kiểm thử Máy chủ & Giả lập (Host Test & Tooling)

Phân hệ kiểm thử máy chủ đảm bảo kiểm chứng chất lượng mã nguồn theo phương pháp TDD (Test-Driven Development) trên môi trường MinGW GCC:

| Mã yêu cầu | Tên chức năng | Mô tả chi tiết yêu cầu | Trạng thái |
|---|---|---|---|
| **SRS-TST-01** | Giả lập FreeRTOS API | Xây dựng bộ mock đầy đủ cho các kiểu dữ liệu và hàm FreeRTOS: `TickType_t`, `TaskHandle_t`, `taskENTER_CRITICAL()`, `taskEXIT_CRITICAL()`, biến giả lập `g_mock_tick`. | Đã triển khai |
| **SRS-TST-02** | Kiểm thử Ring Buffer | Kiểm chứng tính đúng đắn của hàng đợi sự kiện lỗi: Thứ tự FIFO, trạng thái rỗng, trạng thái đầy, và khôi phục hoạt động sau khi đọc hết sự kiện. | Đã triển khai |
| **SRS-TST-03** | Khử báo nhầm Expected-Wait | Kiểm tra kịch bản task chuyển trạng thái `EXPECTED_WAIT`: Quá trình quét trong hạn định không sinh lỗi; quá hạn định phải lập tức bắn lỗi `F04`. | Đã triển khai |
| **SRS-TST-04** | Khử sự kiện rác qua Generation | Giả lập tình huống task được nâng lên thế hệ `gen + 1`: Toàn bộ sự kiện cũ và lệnh báo tiến độ từ thế hệ cũ `gen` bị từ chối và tiêu hủy tự động. | Đã triển khai |
| **SRS-TST-05** | An toàn tràn số Tick Wrap-around | Giả lập `g_mock_tick` ở mốc cận biên tràn số (`0xFFFFFFFF - 50 ticks`), chứng minh phép tính hiệu số không dấu phát hiện lỗi timeout chính xác khi tick cuộn qua 0. | Đã triển khai |
| **SRS-TST-06** | Tự động hóa Runner TDD | Thực thi toàn bộ test cases qua kịch bản tự động, xuất kết quả chi tiết từng case dạng `[PASS]` hoặc `[FAIL]`, đạt tỷ lệ thành công 100%. | Đã triển khai |

---

## 4. Yêu cầu Chức năng Firmware & Kernel RTSFR (Firmware/Kernel Requirements)

### 4.1. Module 1: Phát hiện Lỗi & Tích hợp Kernel (Việt Anh phụ trách)

| Mã yêu cầu | Tên chức năng | Mô tả chi tiết yêu cầu kỹ thuật | Trạng thái |
|---|---|---|---|
| **SRS-KER-01** | Khởi tạo & Đăng ký Task tĩnh | Cung cấp `rtsfr_detect_init()` và `rtsfr_register_task()`. Quản lý tối đa 4 task trên mảng tĩnh O(1). Khởi tạo task ở thế hệ `generation = 1` và gán mốc tiến độ ban đầu bằng tick hiện tại. | Đã triển khai |
| **SRS-KER-02** | Cập nhật Tiến độ Atomic | Hàm `rtsfr_report_progress(task_id)` cập nhật `last_progress_tick = now` và tăng bộ đếm `progress_seq++`. Nếu task đang ở trạng thái `FAULTY` hoặc `QUIESCED`, hàm từ chối cập nhật để ngăn che giấu lỗi. | Đã triển khai |
| **SRS-KER-03** | Hợp đồng Chờ Hợp lệ | Hàm `rtsfr_begin_expected_wait(task_id, timeout)` chuyển trạng thái task sang `EXPECTED_WAIT`, thiết lập deadline. Hàm `rtsfr_end_expected_wait(task_id)` đưa task trở về trạng thái `HEALTHY`. | Đã triển khai |
| **SRS-KER-04** | Vòng quét Giám sát Định kỳ | Hàm `rtsfr_detect_scan(current_tick)` được gọi từ Supervisor. Quét lần lượt các task: Nếu task `HEALTHY` không tiến độ quá `timeout_ticks` -> Bắn lỗi `F01`; Nếu task `EXPECTED_WAIT` quá deadline -> Bắn lỗi `F04`. | Đã triển khai |
| **SRS-KER-05** | Đồng bộ Thế hệ Mới | Hàm `rtsfr_detect_on_new_generation(task_id, new_handle, new_gen)` cập nhật handle mới, tăng thế hệ, xóa sạch cờ `fault_pending` và xóa các sự kiện thế hệ cũ trong hàng đợi. | Đã triển khai |

### 4.2. Module 2: Phân loại Lỗi & Chính sách Phục hồi (Vũ Long phụ trách)

| Mã yêu cầu | Tên chức năng | Mô tả chi tiết yêu cầu kỹ thuật | Trạng thái |
|---|---|---|---|
| **SRS-KER-06** | Phân loại Lỗi & Severity | Phân loại sự kiện lỗi nhận được theo mức độ nghiêm trọng: `LOW` (chậm tiến độ lần đầu), `MEDIUM` (quá hạn chờ dài), `CRITICAL` (báo lỗi tự nguyện hoặc vi phạm invariant). | Kế hoạch Tuần 2 |
| **SRS-KER-07** | Bảng Quyết định Phục hồi | Cung cấp hàm `rtsfr_policy_evaluate(fault_ev, res_summary)`. Trả về `RESTART` nếu tài nguyên an toàn và còn budget; Trả về `SAFE_MODE` nếu giữ Mutex hoặc hết budget; Trả về `REQUEST_RESET` nếu lỗi nhân. | Kế hoạch Tuần 2 |
| **SRS-KER-08** | Quản lý Ngân sách Thử lại | Duy trì cấu trúc trượt thời gian (Sliding Window): Tối đa cho phép 2 lần restart trong 60 giây cho mỗi task. Nếu vượt ngưỡng, chuyển vĩnh viễn sang trạng thái an toàn `SAFE_MODE`. | Kế hoạch Tuần 2 |

### 4.3. Module 3: Theo dõi Sở hữu Tài nguyên (Hồng Long phụ trách)

| Mã yêu cầu | Tên chức năng | Mô tả chi tiết yêu cầu kỹ thuật | Trạng thái |
|---|---|---|---|
| **SRS-KER-09** | Theo dõi Sở hữu Mutex | Lưu vết mảng sở hữu Mutex tĩnh (tối đa 4 Mutex). Hook trong `queue.c` cập nhật chính xác task nào đang giữ Mutex nào ngay khi thao tác Take/Give thành công. | Kế hoạch Tuần 2 |
| **SRS-KER-10** | Quản lý Managed Buffer Pool | Quản lý 2 – 4 vùng đệm độc quyền có cấu trúc trạng thái (`FREE`, `OWNED`, `IN_FLIGHT`, `QUARANTINED`). Cung cấp hàm dọn dẹp (cleanup/reset) đưa buffer về trạng thái `FREE` khi task sở hữu gặp sự cố. | Kế hoạch Tuần 3 |
| **SRS-KER-11** | Kết xuất Snapshot Tài nguyên | Hàm `rtsfr_resource_snapshot(task_id, gen, out_summary)` tạo ảnh chụp tài nguyên nhất quán. Xác nhận `can_reclaim_all = true` CHỈ KHI số Mutex nắm giữ bằng 0 và không có DMA in-flight. | Kế hoạch Tuần 3 |

### 4.4. Module 4: Quản lý Phục hồi Chọn lọc (Tú phụ trách)

| Mã yêu cầu | Tên chức năng | Mô tả chi tiết yêu cầu kỹ thuật | Trạng thái |
|---|---|---|---|
| **SRS-KER-12** | Cô lập Tác vụ Lỗi (Quiescing) | Khi nhận lệnh `RESTART` từ Module 2, Supervisor lập tức gọi `vTaskSuspend()` hoặc tạm thời rút task khỏi ready list để ngăn chặn task tiếp tục ghi hỏng dữ liệu. | Kế hoạch Tuần 3 |
| **SRS-KER-13** | Dọn dẹp & Khởi tạo Tác vụ Mới | Phối hợp cùng Module 3 giải phóng buffer; gọi `vTaskDelete()` xóa instance cũ; cho Idle task thời gian dọn TCB; gọi `xTaskCreate()` tái sinh task mới từ Task Descriptor gốc với `generation + 1`. | Kế hoạch Tuần 3 |
| **SRS-KER-14** | Xác thực Hậu Phục hồi | Đưa task mới vào trạng thái `VERIFYING`. Giám sát trong cửa sổ `verify_timeout_ticks`: Nếu task mới phát báo cáo tiến độ đầu tiên thành công -> Chuyển `HEALTHY`; Nếu không -> Báo lỗi thất bại. | Kế hoạch Tuần 3 |

### 4.5. Module 5: Giả lập Lỗi, Đo kiểm & Watchdog (Thủy phụ trách)

| Mã yêu cầu | Tên chức năng | Mô tả chi tiết yêu cầu kỹ thuật | Trạng thái |
|---|---|---|---|
| **SRS-KER-15** | Giả lập Lỗi có Kiểm soát | Đóng gói bộ tiêm lỗi kích hoạt qua cờ biên dịch `RTSFR_ENABLE_FAULT_INJECTION`: Giả lập vòng lặp vô hạn `while(1)`, giả lập kẹt chờ ngoại vi, giả lập giữ Mutex rồi chết. | Kế hoạch Tuần 2 |
| **SRS-KER-16** | Đệm Log Trace Bất đồng bộ | Xây dựng Ring Buffer tĩnh lưu vết sự kiện; truyền qua UART1 115200 bps bởi tác vụ nền có độ ưu tiên thấp, đảm bảo không làm méo mó thời gian thực thi của hệ điều hành. | Kế hoạch Tuần 2 |
| **SRS-KER-17** | Quản lý Watchdog IWDG | Cấu hình IWDG phần cứng với chu kỳ timeout 1.6 giây. Chỉ cho phép Supervisor nạp lại Watchdog khi tất cả bất biến đều thỏa mãn; nếu Supervisor bị treo, IWDG sẽ tự động reset MCU. | Kế hoạch Tuần 2 |

---

## 5. Yêu cầu Phi Chức năng Phần mềm (Non-Functional Requirements)

1. **Tính Tất định về Thời gian Quét (SRS-NFR-01):** Hàm quét `rtsfr_detect_scan()` trên mảng tĩnh 4 task phải có thời gian thực thi hoàn toàn tất định và không vượt quá **300 µs** trên kiến trúc Cortex-M3 72 MHz.
2. **Thời gian Khóa Ngắt Bounded (SRS-NFR-02):** Mọi đoạn găng bảo vệ dữ liệu (`taskENTER_CRITICAL()` / `taskEXIT_CRITICAL()`) trong các hàm báo tiến độ, đẩy/lấy hàng đợi phải có thời gian chiếm giữ ngắt dưới **3 µs** (~200 chu kỳ xung nhịp), đảm bảo không làm trôi ngắt ngoại vi.
3. **Cấm Cấp phát Động trong Runtime Hot-path (SRS-NFR-03):** Toàn bộ cấu trúc Registry, Event Queue và Trace Buffer phải được cấp phát tĩnh. Tuyệt đối không gọi `pvPortMalloc()` hay `malloc()` trong quá trình giám sát, quét lỗi, báo tiến độ hoặc ghi log.
4. **Giới hạn Chiếm dụng Bộ nhớ (SRS-NFR-04):** Toàn bộ phân hệ RTSFR mở rộng chỉ được phép chiếm tối đa **1.5 KB SRAM** (vùng nhớ tĩnh BSS) và dưới **12 KB Flash ROM**, đảm bảo hệ thống vận hành an toàn trên vi điều khiển 20 KB SRAM của STM32F103.
5. **Độ Tin cậy Tràn số Tick (SRS-NFR-05):** Hệ thống phải hoạt động chính xác liên tục mà không sinh lỗi giả khi biến đếm tick của FreeRTOS tràn từ `0xFFFFFFFF` về `0x00000000` (chu kỳ xấp xỉ 49.7 ngày).
6. **Triệt tiêu Race Condition & Sự kiện Rác (SRS-NFR-06):** Mọi sự kiện lỗi của thế hệ `gen` cũ còn tồn đọng trong hàng đợi phải bị vô hiệu hóa hoàn toàn ngay khi thế hệ `gen + 1` được kích hoạt, triệt tiêu 100% nguy cơ xảy ra phục hồi lặp (double recovery).

---

## 6. Yêu cầu Bổ sung & Quản lý Ngoại lệ (Supplementary & Exception Requirements)

1. **Lỗi Không thể Phục hồi (SRS-SUP-01):** Nếu phát hiện lỗi phá hỏng bộ nhớ kernel, lỗi vi phạm vùng nhớ nghiêm trọng hoặc ngoại lệ phần cứng HardFault, RTSFR không cố gắng restart task mà lập tức ngừng nạp IWDG để kích hoạt Hardware Reset toàn hệ thống.
2. **Cạn kiệt Ngân sách Thử lại (SRS-SUP-02):** Khi một task bị lỗi liên tiếp vượt quá 2 lần trong 60 giây, hệ thống chuyển task đó sang trạng thái `SAFE_MODE`, tắt các tín hiệu điều khiển liên quan đến task đó và duy trì hoạt động an toàn cho các tác vụ còn lại.
3. **Tràn Hàng đợi Sự kiện (SRS-SUP-03):** Trong tình huống cực đoan nhiều lỗi phát sinh đồng thời làm đầy Ring Buffer 8 phần tử, Module 1 kích hoạt cờ cảnh báo tràn `queue_overflow`, loại bỏ sự kiện thừa và phát tín hiệu cảnh báo khẩn cấp sang Supervisor.
4. **Thiếu Bộ nhớ Heap khi Tạo Task Mới (SRS-SUP-04):** Nếu hàm `xTaskCreate()` thất bại do không đủ bộ nhớ heap liên tục sau khi xóa task cũ, Recovery Manager phải đưa hệ thống về `SAFE_MODE` và phát mã lỗi chẩn đoán `RTSFR_ERR_NO_RESOURCE` qua cổng UART.

---

## 7. Luồng Dữ liệu và Xử lý Lỗi Thời gian thực (Data Pipeline & Execution Flow)

Chu trình phát hiện và xử lý lỗi được vận hành khép kín qua 8 giai đoạn tuần tự:

1. **Đăng ký Tác vụ (Task Registration):** Tác vụ ứng dụng được tạo và đăng ký vào RTSFR Registry với các tham số: `task_id`, `criticality`, `timeout_ticks`. Thế hệ khởi tạo được gán là `generation = 1`.
2. **Theo dõi Nhịp tim & Khai báo Chờ (Heartbeat & Expected Wait):** Tác vụ định kỳ gọi `rtsfr_report_progress()`. Khi cần chờ sự kiện ngoại vi hoặc delay, tác vụ chủ động gọi cặp `rtsfr_begin_expected_wait()` và `rtsfr_end_expected_wait()`.
3. **Quét Giám sát & Bắn Sự kiện Lỗi (Scanning & Event Generation):** Tác vụ Supervisor định kỳ gọi `rtsfr_detect_scan()`. Nếu phát hiện quá hạn tiến độ, Module 1 đóng gói cấu trúc `rtsfr_fault_event_t` và đẩy vào Hàng đợi Sự kiện Ring Buffer tĩnh.
4. **Đánh giá Chính sách & Chụp Snapshot Tài nguyên (Policy & Resource Snapshot):** Supervisor lấy sự kiện lỗi qua `rtsfr_detect_pop_event()`, yêu cầu Module 3 cung cấp ảnh chụp tài nguyên `rtsfr_resource_summary_t`, sau đó gửi thông tin sang Module 2 để nhận phán quyết `rtsfr_decision_t`.
5. **Cô lập Thực thi (Execution Quiescing):** Nếu quyết định là `RESTART`, Supervisor ra lệnh đình chỉ (suspend) hoặc rút task lỗi ra khỏi danh sách thực thi của Scheduler.
6. **Dọn dẹp Tài nguyên An toàn (Resource Cleanup):** Module 3 tiến hành hoàn trả các Managed Buffer của task lỗi về trạng thái `FREE`. Nếu task đang nắm giữ Mutex, quy trình restart lập tức bị hủy bỏ và chuyển sang `SAFE_MODE`.
7. **Tái tạo Tác vụ Thế hệ Mới (Recreation & Generation Increment):** Supervisor giải phóng task cũ (`vTaskDelete`), nhường quyền cho Idle Task hoàn tất dọn dẹp, sau đó gọi `xTaskCreate` tạo instance mới với thế hệ `generation + 1`. Module 1 được cập nhật qua `rtsfr_detect_on_new_generation()`.
8. **Xác thực Hậu Phục hồi & Ghi Trace (Post-Recovery Verification & Trace Log):** Supervisor theo dõi lần báo tiến độ đầu tiên của instance mới. Toàn bộ tiến trình phục hồi và độ trễ thời gian được Module 5 ghi vào Ring Buffer và truyền qua UART.

---

## 8. Ma trận Xử lý Lỗi & Mã Trạng thái (Error Handling & State Matrix)

| Mã lỗi | Tên lỗi & Nguyên nhân | Hành vi Module Giám sát (M1) | Phán quyết Chính sách (M2) | Hành động Phục hồi Hệ thống (M4/M5) |
|---|---|---|---|---|
| **F01** | **NO_PROGRESS**<br>Task ngừng báo tiến độ quá hạn định nhưng Scheduler vẫn chạy. | Phát hiện timeout; phát sự kiện lỗi F01 kèm `task_id` và `generation`. | `RESTART` nếu tài nguyên an toàn và còn retry budget. | Cô lập task, dọn dẹp bộ nhớ, tạo lại task thế hệ mới (`gen + 1`). |
| **F02** | **BUSY_LOOP**<br>Task rơi vào vòng lặp vô hạn nhưng không chiếm độc quyền CPU. | Phát hiện timeout tiến độ tương tự F01; phát sự kiện lỗi F02. | `RESTART` nếu không giữ tài nguyên nguy hiểm. | Đình chỉ thực thi, dọn dẹp tài nguyên và tái khởi động task. |
| **F03** | **VOLUNTARY**<br>Task tự phát hiện lỗi logic nội bộ và chủ động báo cáo. | Nhận báo cáo tự nguyện qua `rtsfr_report_fault()`; đẩy sự kiện vào queue. | `RESTART` hoặc `SAFE_MODE` theo mức độ nguy cấp đã cấu hình. | Thi hành quyết định chính sách tương ứng của Module 2. |
| **F04** | **WAIT_TIMEOUT**<br>Task chờ đợi ngoại vi (Expected-Wait) vượt quá hạn định đã đăng ký. | Phát hiện quá deadline của `expected_wait`; phát sự kiện lỗi F04. | `RESTART` hoặc đưa ngoại vi vào chế độ Reset. | Khởi tạo lại tác vụ và khởi động lại driver ngoại vi tương ứng. |
| **F05** | **MANAGED_BUFFER_HELD**<br>Task gặp lỗi khi đang sở hữu vùng đệm thuộc Pool quản lý. | Phát hiện lỗi tiến độ; Module 3 xác nhận task sở hữu Managed Buffer độc quyền. | `RESTART sau CLEANUP` (nếu không có DMA in-flight). | Module 3 reset buffer về `FREE`; Module 4 tái tạo task và cấp lại buffer. |
| **F06** | **MUTEX_HELD**<br>Task gặp lỗi trong khi đang chiếm giữ FreeRTOS Mutex. | Phát hiện lỗi tiến độ; Module 3 xác nhận số Mutex nắm giữ `>= 1`. | **`SAFE_MODE`** tuyệt đối; cấm tự ý nhả Mutex. | Cô lập task vĩnh viễn, phát cảnh báo lỗi LED/UART; nuôi IWDG nếu safe. |
| **F07** | **RETRY_EXHAUSTED**<br>Task bị lỗi liên tiếp vượt quá ngân sách thử lại (bão phục hồi). | Nhận sự kiện lỗi liên tiếp từ cùng một `task_id` trong thời gian ngắn. | **`SAFE_MODE`** hoặc `REQUEST_RESET`. | Chuyển task sang trạng thái cách ly hoàn toàn; ngăn vòng lặp restart lặp vô tận. |
| **F08** | **KERNEL_STALL**<br>Kernel bị tê liệt, ngắt bị tắt vĩnh viễn hoặc kẹt trong critical section. | Module 1 và Supervisor hoàn toàn không thể thực thi. | Không thể ra quyết định phần mềm. | **IWDG phần cứng hết hạn** sau 1.6 giây; kích hoạt Reset toàn bộ MCU. |
| **F09** | **CORRUPTED_KERNEL**<br>Tràn Stack (Stack Overflow), HardFault hoặc ghi hỏng cấu trúc TCB. | Bị bắt bởi FreeRTOS Stack Hook hoặc HardFault Handler. | `REQUEST_RESET` khẩn cấp. | Ghi mã lỗi khẩn cấp ra thanh ghi backup nếu kịp; để Watchdog reset MCU. |
| **F10** | **LEGITIMATE_WAIT**<br>Task đang ngủ `vTaskDelay` hoặc chờ ngoại vi trong thời hạn cho phép. | Kiểm tra thấy `now < expected_wait_deadline`. Bỏ qua. | **`IGNORE`** (Không coi là lỗi). | Không có hành động can thiệp; duy trì hoạt động bình thường. |

---

## 9. Kiểm thử và Nghiệm thu Phần mềm (Testing & Acceptance Matrix)

| Mã k/thử | Yêu cầu liên quan | Phương pháp kiểm thử | Tiêu chí Đạt (Pass Criteria) |
|---|---|---|---|
| **TC-01** | SRS-KER-01, SRS-KER-02 | Chạy tác vụ báo nhịp tim định kỳ, kiểm tra giá trị `last_progress_tick` và `progress_seq`. | Giá trị tick được cập nhật liên tục; `progress_seq` tăng đều đặn; trả về `RTSFR_OK`. |
| **TC-02** | SRS-KER-03, SRS-NFR-05 | Cho task gọi `rtsfr_begin_expected_wait` với thời hạn 300 ticks, kiểm tra tại mốc 250 ticks và 350 ticks. | Tại 250 ticks không phát sinh lỗi; tại 350 ticks lập tức phát sinh sự kiện lỗi F04 chính xác. |
| **TC-03** | SRS-KER-04, SRS-KER-05 | Chặn báo tiến độ trên Task 1, đợi Supervisor phát hiện lỗi và khởi tạo lại task thế hệ mới. | Task mới được tạo với `generation = 2`; sự kiện của `generation = 1` bị hủy hoàn toàn; hệ thống tiếp tục chạy. |
| **TC-04** | SRS-KER-09, SRS-KER-07 | Task 2 thực hiện `xSemaphoreTake(mutex)` thành công rồi rơi vào vòng lặp vô hạn `while(1)`. | Module 3 nhận diện task đang giữ Mutex; Module 2 ra quyết định `SAFE_MODE`; tuyệt đối không giải phóng Mutex bừa bãi. |
| **TC-05** | SRS-KER-10, SRS-KER-13 | Task 1 mượn 1 Managed Buffer, ghi dữ liệu dở dang rồi kích hoạt lỗi tự nguyện F03. | Module 3 thu hồi và reset buffer về `FREE`; Task 1 được tái tạo và có thể mượn lại buffer bình thường. |
| **TC-06** | SRS-KER-08, SRS-SUP-02 | Kích hoạt lỗi liên tiếp 3 lần trên Task 2 trong vòng 20 giây. | Hai lần đầu hệ thống phục hồi thành công; lần thứ 3 chuyển sang `SAFE_MODE` và dừng hẳn Task 2. |
| **TC-07** | SRS-KER-17, SRS-SUP-01 | Tắt ngắt toàn cục bằng lệnh `__disable_irq()` trong vòng lặp vô hạn. | Supervisor không thể chạy; IWDG không được refresh và kích hoạt Hardware Reset sau đúng ~1.6 giây. |
| **TC-08** | SRS-NFR-02, SRS-NFR-03 | Đo xung nhịp thực thi hàm `rtsfr_report_progress` và kiểm tra file MAP bộ nhớ sau khi build Keil. | Thời gian thực thi dưới 3 µs; không có bất kỳ lệnh gọi `malloc`/`pvPortMalloc` nào trong báo cáo linker. |
| **TC-09** | SRS-TST-01 đến SRS-TST-06 | Chạy bộ test suite máy chủ `test_m1_detect.exe` biên dịch bằng MinGW GCC. | 10/10 test cases đạt trạng thái `[PASS]` 100%, không phát sinh cảnh báo bộ nhớ. |
| **TC-10** | SRS-KER-16 | Kết nối cổng UART1 lên phần mềm Serial Terminal trên PC khi hệ thống đang vận hành các kịch bản lỗi. | Các dòng log sự kiện hiển thị đúng định dạng, đầy đủ thông tin `task_id`, `generation`, `fault_type` và `action`. |
