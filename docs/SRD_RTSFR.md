# TÀI LIỆU YÊU CẦU HỆ THỐNG
*(System Requirements Document — SRD)*

**Tên dự án:** Thiết kế và triển khai cơ chế phát hiện và phục hồi có chọn lọc lỗi tác vụ, có xét trạng thái tài nguyên, ở mức kernel FreeRTOS trên STM32 (RTSFR)  
**Tên tiếng Anh:** Resource-aware Task Selective Fault Recovery Framework for FreeRTOS on STM32  
**Mã đề tài:** RTSFR-HUST-2026  
**Phạm vi tài liệu:** Mô tả kiến trúc tổng thể, yêu cầu hệ thống đích, các ràng buộc kỹ thuật, giao tiếp liên phân hệ và tiêu chí nghiệm thu của khung phục hồi lỗi firmware RTSFR.

---

## 1. Mục đích và Phạm vi Hệ thống

Khung phần mềm nhúng RTSFR được thiết kế để tích hợp trực tiếp vào nhân FreeRTOS, chạy trên vi điều khiển STM32F103C8T6. Hệ thống thực hiện giám sát tiến độ thực chất của từng tác vụ (task), phát hiện lỗi bất thường (treo vô hạn, quá hạn chờ, dừng cập nhật nhịp tim), đánh giá tính an toàn của tài nguyên do task nắm giữ (mutex, bộ đệm quản lý), từ đó tự động tái khởi động có chọn lọc tác vụ lỗi mà không làm gián đoạn các tác vụ khác và không làm treo vi điều khiển.

- **Đối tượng người dùng (Target Users):** Sinh viên, kỹ sư hệ thống nhúng, nhà phát triển phần mềm RTOS cho các ứng dụng đòi hỏi tính sẵn sàng cao (High-Availability Embedded Systems), robot tự hành, hệ thống điều khiển công nghiệp và thiết bị IoT y tế.
- **Bài toán cần giải quyết (Problem Statement):**
  - Trong các hệ thống nhúng truyền thống sử dụng RTOS, khi một task bị tê liệt (do vòng lặp vô hạn, deadlock phần mềm, kẹt đường truyền ngoại vi SPI/I2C), giải pháp cứu cánh duy nhất thường là cơ chế Independent Watchdog (IWDG) reset toàn bộ vi điều khiển. Việc khởi động lại toàn hệ thống làm mất trắng dữ liệu tạm thời, ngắt quãng các tác vụ quan trọng đang vận hành bình thường (như tác vụ điều khiển động cơ, lấy mẫu cảm biến an toàn) và gây thời gian chết (downtime) lớn.
  - Ngược lại, nếu cho phép ứng dụng tự xóa rồi tạo lại task (`vTaskDelete()` và `xTaskCreate()`) một cách máy móc, hệ thống sẽ đối mặt với các hiểm họa phá vỡ trạng thái nhất quán của RTOS kernel: mutex bị khóa vĩnh viễn (dangling/orphaned mutex), rò rỉ bộ nhớ heap, và dữ liệu trong các buffer chia sẻ bị chỉnh sửa dở dang (state corruption).
  - Dự án giải quyết bài toán cốt lõi: **Làm thế nào để phát hiện task bị dừng tiến độ và chỉ phục hồi những task có trạng thái tài nguyên đã được chứng minh là an toàn, đồng thời giữ vững hoạt động cho các task lành mạnh, với chi phí RAM/CPU cực kỳ tối ưu trên nền tảng vi điều khiển giới hạn 20 KB SRAM.**
- **Mục đích:** Xây dựng một framework mở rộng nhân FreeRTOS hoàn chỉnh gồm 5 module chức năng: Giám sát tiến độ tại kernel, Phân loại lỗi và ra quyết định, Theo dõi sở hữu tài nguyên, Quản lý phục hồi chọn lọc theo thế hệ (generation-aware), và Bộ kiểm chứng/tiêm lỗi/watchdog dự phòng. Toàn bộ kiến trúc được triển khai trên nền tảng STM32F103C8T6 không MPU, có thể tái lập lỗi và chứng minh tính đúng đắn qua nhật ký trace và đo lường thời gian thực.
- **Phạm vi hệ thống (System Boundary):**
  - **Môi trường phần cứng thực thi:** Vi điều khiển STM32F103C8T6 (Blue Pill, ARM Cortex-M3, 72 MHz, 20 KB SRAM, 64 KB Flash). Sử dụng cổng nạp/debug ST-LINK V2, cổng UART1 truyền log trace bất đồng bộ lên máy tính và bộ đếm phần cứng Independent Watchdog (IWDG) làm chốt chặn cuối cùng. Hệ thống là giải pháp firmware/kernel thuần túy, không yêu cầu thiết kế mạch bảo vệ điện áp hay cảm biến analog mở rộng.
  - **Phân hệ Kernel RTSFR mở rộng:** Can thiệp có kiểm soát vào FreeRTOS Core (`tasks.c`, `queue.c`) kết hợp 5 module chuyên trách độc lập:
    1. *Module 1 (Fault Detection & Kernel Integration):* Registry giám sát tĩnh, cập nhật tiến độ atomic, hợp đồng chờ hợp lệ (Expected-Wait), phát hiện quá hạn an toàn tràn tick.
    2. *Module 2 (Fault Classification & Recovery Policy):* Máy trạng thái phân loại lỗi, ngân sách thử lại (retry budget) và bảng quyết định an toàn.
    3. *Module 3 (Resource Ownership Tracker):* Móc nối theo dõi mutex và quản lý vùng đệm độc quyền (Managed Buffer Pool).
    4. *Module 4 (Selective Recovery Manager):* Cô lập thực thi (quiescing), dọn dẹp tài nguyên, cấp phát lại task thế hệ mới và xác thực hậu phục hồi.
    5. *Module 5 (Fault Injection, Verification & Watchdog):* Giả lập lỗi có chủ đích, kiểm chứng bất biến hệ thống, đệm log trace bất đồng bộ và nuôi watchdog phần cứng.
  - **Phân hệ Tác vụ Mẫu (Workload Application):** Gồm 3 tác vụ đại diện cho các miền công việc nhúng: Tác vụ cảm biến (Sensor Task), Tác vụ truyền thông (Comm Task), Tác vụ điều khiển & giám sát (Control/Logger Task).
  - **Phân hệ Kiểm thử Máy chủ (Host Test Harness):** Môi trường kiểm thử đơn vị độc lập chạy trên máy chủ (Host MinGW GCC), giả lập hành vi API FreeRTOS để kiểm chứng các thuật toán trước khi nạp xuống vi điều khiển.

---

## 2. Bảng Thông số Kỹ thuật Hệ thống (System Specifications)

| Mã yêu cầu | Hạng mục | Thông số thiết kế (STM32F103C8T6 - Hiện tại) | Khả năng mở rộng (STM32F4/Cortex-M4 - Tương lai) | Yêu cầu tối thiểu từ Đề tài |
|---|---|---|---|---|
| **SRD-KER-01** | Nhân hệ điều hành thời gian thực | FreeRTOS Kernel V10.3.1 (Patch trực tiếp tại `tasks.c`, `queue.c`) | FreeRTOS Kernel V11.x / Cortex-M4 MPU Port | FreeRTOS chính thức, cấu hình chuẩn |
| **SRD-KER-02** | Số lượng tác vụ giám sát | Cố định 4 tác vụ ứng dụng (`configRTSFR_MAX_TRACKED_TASKS = 4`) | Mở rộng cấu hình linh hoạt (8 – 16 tác vụ) | Tối thiểu 3 tác vụ demo + 1 supervisor |
| **SRD-KER-03** | Chu kỳ quét của Supervisor | 20 ms – 50 ms (Chạy trong `RTSFR_Supervisor` Task) | 1 ms – 10 ms (Ngắt SysTick hoặc Timer chuyên dụng) | Định kỳ quét không làm treo scheduler |
| **SRD-MEM-01** | Bộ nhớ tĩnh RAM Framework (BSS) | Dưới 1.5 KB SRAM (Registry + Event Queue + Trace Buffer) | Dưới 4 KB SRAM | Phù hợp với tổng dung lượng 20 KB SRAM |
| **SRD-MEM-02** | Cơ chế cấp phát động (Heap) | Hoàn toàn KHÔNG cấp phát động (`zero heap`) trong runtime hot-path | Cấp phát tĩnh hoặc Static Pools | Cấm gọi `malloc`/`pvPortMalloc` khi giám sát/quét |
| **SRD-TIME-01** | Độ trễ phát hiện lỗi (Detection Latency) | Không quá 1 chu kỳ quét Supervisor + sai số jitter (<= 60 ms) | <= 5 ms | Phát hiện nhanh chóng trong thời hạn timeout |
| **SRD-TIME-02** | Thời gian khóa ngắt (Critical Section) | Tối đa dưới 5 µs (~360 chu kỳ CPU tại 72 MHz) cho mỗi thao tác | Dưới 2 µs | Không làm trễ các ngắt ngoại vi thời gian thực |
| **SRD-TIME-03** | Thời gian phục hồi chọn lọc (Recovery Time) | Dưới 15 ms từ lúc chẩn đoán an toàn đến khi task mới chạy | Dưới 5 ms | Nhanh hơn đáng kể so với thời gian reset toàn MCU (> 200 ms) |
| **SRD-FLT-01** | Các loại lỗi hỗ trợ xử lý | F01 (No-Progress), F02 (Busy-Loop), F03 (Tự nguyện), F04 (Wait-Timeout), F05 (Giữ Buffer), F06 (Giữ Mutex) | Mở rộng lỗi Stack Overflow và Ngoại vi DMA stall | Tối thiểu F01, F02, F04, F06 |
| **SRD-RES-01** | Theo dõi tài nguyên Mutex | Tối đa 4 Mutex được giám sát trực tiếp trong kernel hook | 8 – 16 Mutex | Nhận biết task lỗi có giữ Mutex hay không |
| **SRD-RES-02** | Vùng đệm quản lý (Managed Buffers) | 2 – 4 vùng đệm tĩnh thuộc pool có hợp đồng thu hồi an toàn | Dynamic Buffer Pools | Tối thiểu 1 loại buffer có thể tái thu hồi |
| **SRD-REC-01** | Cơ chế chống sự kiện rác (Anti-Stale) | Đánh dấu thế hệ `generation` tăng đơn điệu (16-bit counter) | 32-bit unique execution ID | Triệt tiêu hoàn toàn race condition sau phục hồi |
| **SRD-REC-02** | Ngân sách khởi động lại (Retry Budget) | Tối đa 2 lần phục hồi trong cửa sổ 60 giây (vượt quá -> SAFE_MODE) | Cấu hình tham số hóa cho từng mức criticality | Ngăn chặn vòng lặp khởi động lại vô tận |
| **SRD-TRC-01** | Giao diện ghi vết (Trace Output) | UART1 115200 bps, truyền chuỗi định dạng qua Ring Buffer bất đồng bộ | Segger RTT / Tracealyzer / High-speed USB CDC | Không sử dụng hàm in `printf` dạng blocking trong hook |
| **SRD-WDG-01** | Chốt chặn phần cứng (Hardware Fallback) | Independent Watchdog (IWDG) chu kỳ timeout 1.5 – 2.0 giây | IWDG kết hợp WWDG | Reset toàn hệ thống nếu RTSFR bị tê liệt |

---

## 3. Các Ràng buộc Thiết kế Cấp Hệ thống (System Constraints)

- **Ràng buộc Bộ nhớ STM32F103C8T6 (SRD-CON-01):** Vi điều khiển chỉ có 20 KB SRAM và 64 KB Flash. Bảng quản lý `rtsfr_registry_t`, hàng đợi sự kiện lỗi `rtsfr_event_queue_t` và bộ đệm log trace bắt buộc phải cấp phát tĩnh tại vùng BSS. Nghiêm cấm cấp phát động trong runtime path. Việc tái sinh task sử dụng `xTaskCreate` kết hợp `heap_4.c` với kích thước stack được kiểm soát chặt chẽ.
- **Ràng buộc Thế hệ Tác vụ & Chống Xung đột Trạng thái (SRD-CON-02 - Generation-Aware):** Mọi bản ghi tiến độ, sự kiện lỗi và yêu cầu phục hồi phải gắn liền với cặp định danh `(task_id, generation)`. Khi một task được xóa và tạo lại, thế hệ của nó tăng lên `generation + 1`. Bất kỳ sự kiện lỗi hoặc tiến độ báo về từ phiên bản cũ còn sót lại trong hàng đợi đều phải bị hủy bỏ ngay lập tức, ngăn ngừa lỗi phục hồi kép (double recovery).
- **Ràng buộc Hợp đồng Chờ Hợp lệ (SRD-CON-03 - Expected-Wait Contract):** Một task rơi vào trạng thái `Blocked` khi đang ngủ `vTaskDelay()` hoặc chờ đợi thiết bị ngoại vi (I2C/SPI) phản hồi không được coi là lỗi. Ứng dụng phải khai báo thời hạn chờ dự kiến qua `rtsfr_begin_expected_wait()`. Giám sát viên chỉ phát cờ báo lỗi khi thời gian chờ vượt quá giới hạn đã đăng ký.
- **Ràng buộc Tràn số Bộ đếm Thời gian (SRD-CON-04 - Tick Wrap-around Safety):** Biến đếm `xTaskGetTickCount()` của FreeRTOS là kiểu số nguyên không dấu 32-bit, sẽ bị tràn số sau khoảng 49.7 ngày hoạt động liên tục (với tick rate 1000 Hz). Mọi phép so sánh thời gian trong hệ thống bắt buộc phải sử dụng hiệu số không dấu: `(TickType_t)(xCurrentTick - xLastProgressTick) >= xTimeoutTicks` để đảm bảo tính đúng đắn toán học tuyệt đối.
- **Ràng buộc Cách ly Thực thi vs Cách ly Bộ nhớ (SRD-CON-05):** Do STM32F103 không có bộ nhớ bảo vệ phần cứng (MPU), RTSFR chỉ cam kết **Cô lập thực thi (Execution Isolation)** bằng cách đình chỉ hoặc xóa task lỗi khỏi danh sách lập lịch của scheduler. Hệ thống **không cam kết Cách ly bộ nhớ (Memory Isolation)**; nếu một tác vụ lỗi làm hỏng cấu trúc dữ liệu nội tại của Kernel hoặc gây HardFault, hệ thống phải kích hoạt reset phần cứng qua IWDG thay vì cố gắng phục hồi task.
- **Ràng buộc Không Chặn I/O trong Ngữ cảnh Kernel (SRD-CON-06):** Tuyệt đối không gọi `printf()`, `HAL_UART_Transmit` dạng blocking hay các hàm giữ semaphore trong ISR, hook hoặc vòng quét giám sát. Mọi bản ghi log phải được đẩy vào Ring Buffer bất đồng bộ do tác vụ có độ ưu tiên thấp đẩy ra UART.

---

## 4. Sơ đồ Khối Hệ thống (System Block Diagram)

Hệ thống được tổ chức thành 4 phân tầng phân định rõ ràng về trách nhiệm và giao diện:

```
+---------------------------------------------------------------------------------------+
|                              TẦNG TÁC VỤ ỨNG DỤNG (DEMO WORKLOAD)                     |
|   +-----------------------+   +------------------------+   +----------------------+   |
|   |  Sensor Task (ID 0)   |   |  Comm Task (ID 1)      |   | Control Task (ID 2)  |   |
|   |  - Báo tiến độ        |   |  - Expected wait       |   | - Báo lỗi tự nguyện  |   |
|   +-----------+-----------+   +-----------+------------+   +----------+-----------+   |
+---------------|---------------------------|---------------------------|---------------+
                |                           |                           |                
                v (rtsfr_report_progress)   v (rtsfr_begin_expected)    v (voluntary)    
+---------------------------------------------------------------------------------------+
|                              KHUNG PHỤC HỒI LỖI KERNEL (RTSFR)                        |
|                                                                                       |
|   +------------------------------------+      +-----------------------------------+   |
|   | Module 1: Fault Detection          |      | Module 2: Fault Policy            |   |
|   | - Task Registry tĩnh O(1)          | ---> | - State Machine quyết định        |   |
|   | - Event Queue (FIFO Ring Buffer)   | (ev) | - Ngân sách thử lại (Retry Budget)|   |
|   +------------------------------------+      +-----------------+-----------------+   |
|                     ^                                           | (decision)          |
|                     |                                           v                     |
|   +-----------------+------------------+      +-----------------------------------+   |
|   | Module 3: Resource Tracker         |      | Module 4: Selective Recovery      |   |
|   | - Bảng sở hữu Mutex                | ---> | - Ngừng lập lịch (Quiescing)      |   |
|   | - Managed Buffer Pool              | (res)| - Dọn dẹp & Tái tạo task (Gen+1)  |   |
|   +------------------------------------+      +-----------------+-----------------+   |
|                                                                 |                     |
|   +-------------------------------------------------------------+-----------------+   |
|   | Module 5: Verification, Trace & Watchdog Support                              |   |
|   | - Async Trace Buffer (UART)         - Quản lý Heartbeat IWDG                  |   |
|   | - Bộ tiêm lỗi giả lập (Fault Inj)   - Bộ kiểm tra Invariant thời gian thực    |   |
|   +-------------------------------------------------------------------------------+   |
+---------------------------------------|-----------------------------------------------+
                                        | (Can thiệp Hook có kiểm soát)                 
                                        v                                               
+---------------------------------------------------------------------------------------+
|                              NHÂN HỆ ĐIỀU HÀNH (FREERTOS CORE)                         |
|   - tasks.c: Hook đăng ký task, xóa task, context switch                              |
|   - queue.c: Hook theo dõi quyền sở hữu sau khi take/give mutex thành công            |
|   - Scheduler & TCB Management                                                        |
+---------------------------------------|-----------------------------------------------+
                                        |                                               
                                        v                                               
+---------------------------------------------------------------------------------------+
|                              PHẦN CỨNG VI ĐIỀU KHIỂN (STM32F103)                      |
|   - ARM Cortex-M3 (72 MHz)            - SysTick (FreeRTOS Tick 1ms)                   |
|   - TIM4 (HAL Timebase 1ms)           - Independent Watchdog (IWDG Fallback)         |
|   - USART1 (Asynchronous Trace Log)   - GPIO LEDs (Trực quan hóa trạng thái)         |
+---------------------------------------------------------------------------------------+
```

### Mô tả luồng tương tác và phân tách đường thực thi:
1. **Fast-path (Đường nhanh):** Bao gồm các hàm API gọi từ ngữ cảnh tác vụ hoặc hook trong kernel (`rtsfr_report_progress`, hook take/give mutex trong `queue.c`). Các hàm này chỉ thao tác ghi dữ liệu ngắn gọn vào biến nội tại, sử dụng Critical Section cực ngắn (< 3 µs), tuyệt đối không chứa vòng lặp tìm kiếm phức tạp và không cấp phát bộ nhớ.
2. **Control-path (Đường điều khiển):** Được thực thi độc quyền trong ngữ cảnh của tác vụ độc lập `RTSFR_Supervisor` (độ ưu tiên cao hơn các task ứng dụng nhưng thấp hơn các task ngắt hệ thống quan trọng). Supervisor định kỳ thức dậy, gọi Module 1 quét toàn bộ Registry, nếu có lỗi sẽ chuyển sự kiện sang Module 2 và phối hợp cùng Module 3, Module 4 để triển khai quy trình cách ly, dọn dẹp và khởi tạo lại task một cách an toàn.

---

## 5. Đặc tả Giao thức Liên Phân hệ & Cấu trúc Dữ liệu

### 5.1. Cấu trúc Gói tin Sự kiện Lỗi (Fault Event: Module 1 -> Module 2)

Được đóng gói trong cấu trúc tĩnh `rtsfr_fault_event_t` và trung chuyển qua Ring Buffer:

| Trường dữ liệu | Kiểu dữ liệu | Kích thước | Mô tả ý nghĩa |
|---|---|---|---|
| `event_id` | `uint32_t` | 4 bytes | Mã định danh sự kiện tăng đơn điệu, dùng để đồng bộ log |
| `task_id` | `uint8_t` | 1 byte | ID định danh tác vụ bị phát hiện lỗi (0..3) |
| `generation` | `uint16_t` | 2 bytes | Thế hệ của task instance tại thời điểm phát sinh lỗi |
| `fault_type` | `rtsfr_fault_type_t` | 1 byte | Phân loại lỗi: `F01` (NO_PROGRESS), `F02` (BUSY_LOOP), `F04` (WAIT_TIMEOUT),... |
| `detected_tick` | `TickType_t` | 4 bytes | Mốc thời gian hệ thống (SysTick) khi phát hiện lỗi |
| `last_progress_seq`| `uint32_t` | 4 bytes | Số thứ tự tiến độ cuối cùng được ghi nhận trước khi lỗi |

### 5.2. Cấu trúc Tóm tắt Tài nguyên (Resource Summary: Module 3 -> Module 2/4)

Cấu trúc `rtsfr_resource_summary_t` cung cấp căn cứ an toàn để quyết định có được phép restart hay không:

| Trường dữ liệu | Kiểu dữ liệu | Kích thước | Mô tả ý nghĩa |
|---|---|---|---|
| `task_id` | `uint8_t` | 1 byte | ID định danh tác vụ cần kiểm tra |
| `generation` | `uint16_t` | 2 bytes | Thế hệ tác vụ tương ứng |
| `mutex_held_count` | `uint8_t` | 1 byte | Số lượng Mutex mà task lỗi đang chiếm giữ (>= 1 là Unsafe) |
| `managed_buffers_owned` | `uint8_t` | 1 byte | Số lượng vùng đệm thuộc Pool an toàn mà task đang giữ |
| `unknown_resource_count` | `uint8_t` | 1 byte | Số tài nguyên không xác định được trạng thái sở hữu |
| `has_inflight_io` | `bool` | 1 byte | Có kênh DMA/ngoại vi nào đang đọc/ghi vào bộ đệm của task không |
| `can_reclaim_all` | `bool` | 1 byte | Cờ khẳng định: `true` nếu mọi tài nguyên đều thu hồi được an toàn |

### 5.3. Cấu trúc Phán quyết Phục hồi (Policy Decision: Module 2 -> Module 4)

Cấu trúc `rtsfr_decision_t` quy định hành động chính thức mà Recovery Manager phải thi hành:

| Trường dữ liệu | Kiểu dữ liệu | Kích thước | Mô tả ý nghĩa |
|---|---|---|---|
| `event_id` | `uint32_t` | 4 bytes | Khớp với ID sự kiện lỗi ban đầu |
| `task_id` | `uint8_t` | 1 byte | ID tác vụ mục tiêu |
| `generation` | `uint16_t` | 2 bytes | Thế hệ tác vụ mục tiêu |
| `action` | `rtsfr_action_t` | 1 byte | Quyết định: `RESTART`, `SAFE_MODE`, `REQUEST_RESET`, `IGNORE` |
| `reason_code` | `uint16_t` | 2 bytes | Mã nguyên nhân kỹ thuật phục vụ ghi vết chẩn đoán |

### 5.4. Đặc tả Dòng Dữ liệu Trace Log qua Serial UART (Module 5 -> PC Terminal)

Nhật ký sự kiện được truyền bất đồng bộ qua UART1 dưới dạng chuỗi văn bản định dạng chuẩn (CSV/JSONL-like):
`[RTSFR_EVT] timestamp=<tick>, event_id=<id>, task_id=<id>, gen=<gen>, fault=<type>, action=<act>, reason=<rc>`

---

## 6. Môi trường Thực thi Phần cứng Đích & Phân bổ Tài nguyên MCU

Hệ thống được cấu hình và vận hành trên phần cứng tiêu chuẩn:
- **Vi điều khiển:** STM32F103C8T6 (64 KB Flash, 20 KB SRAM).
- **Hệ thống xung nhịp (Clock Tree):** Thạch anh ngoài HSE 8 MHz, nhân tần số qua PLL đạt xung nhịp tối đa **72 MHz** cấp cho CPU và các bus APB1/APB2.
- **Phân bổ Bộ đếm Thời gian (Timers):**
  - `SysTick Timer`: Dành riêng cho FreeRTOS Kernel tạo nhịp Tick với chu kỳ 1.0 ms (`configTICK_RATE_HZ = 1000`).
  - `TIM4 General-Purpose Timer`: Được cấu hình làm Timebase độc lập cho STM32 HAL (1.0 ms interrupt), giải phóng hoàn toàn `TIM1` cho các ứng dụng điều khiển thời gian thực và tránh xung đột với ngắt của hệ điều hành.
- **Phần cứng Giám sát Dự phòng (Watchdog):**
  - `IWDG (Independent Watchdog)`: Chạy bằng xung nội LSI (~40 kHz). Được nạp giá trị Prescaler 64 và Reload tương ứng chu kỳ ngắt quãng **1.6 giây**. Chỉ duy nhất Supervisor Task được phép nạp lại (refresh) IWDG sau khi kiểm tra toàn bộ các bất biến sức khỏe hệ thống đều đạt chuẩn.
- **Giao diện Giao tiếp (UART):**
  - `USART1` (TX chân PA9, RX chân PA10), tốc độ baud 115200 bps, 8 bit dữ liệu, 1 bit stop, không parity. Được kết nối qua mạch chuyển đổi USB-UART lên máy tính.
- **Bản đồ Phân bổ Bộ nhớ SRAM 20 KB:**
  - `Vùng BSS/Data (Hệ thống + RTSFR Static Tables):` ~2.5 KB (trong đó toàn bộ bảng RTSFR chiếm < 1.5 KB).
  - `FreeRTOS Heap (heap_4.c):` 10.0 KB (Dùng để cấp phát TCB và Stack cho các task ứng dụng và Supervisor).
  - `Vùng Ngắt & Main Stack (MSP):` 2.0 KB.
  - `Bộ nhớ dự phòng chống tràn:` ~5.5 KB.

---

## 7. Yêu cầu Giao diện Giám sát, Điều khiển & Output Trace

- **Giao diện Dòng lệnh/Nhật ký (CLI & Trace Log):**
  - Module 5 triển khai một Ring Buffer tĩnh kích thước 512 bytes. Khi các module khác phát sinh sự kiện, chuỗi log được ghi vào Ring Buffer trong vài micro-giây.
  - Tác vụ `Trace_Task` có độ ưu tiên thấp nhất (bằng Idle task) sẽ định kỳ lấy dữ liệu từ Ring Buffer và truyền qua hàm non-blocking hoặc ngắt của UART1.
  - Định dạng gói tin trace bao gồm: `RUN_ID`, `TICK`, `TASK_ID`, `GENERATION`, `EVENT_TYPE`, `DETAILS`.
- **Trực quan hóa Trạng thái bằng Phần cứng (LED Status):**
  - `LED Xanh (PC13 - Onboard):` Đảo trạng thái định kỳ 500 ms biểu thị Scheduler và Supervisor đang sống bình thường.
  - `LED Báo Trạng thái Phục hồi (GPIO Ngoài):` Sáng khi có tác vụ đang được phục hồi; nhấp nháy nhanh khi hệ thống phải chuyển sang `SAFE_MODE`.

---

## 8. Tiêu chí Kiểm thử và Nghiệm thu Hệ thống (Testing & Acceptance Criteria)

| Mã TC | Hạng mục kiểm thử | Phương pháp & Điều kiện kiểm thử | Tiêu chí Đạt (Pass Criteria) |
|---|---|---|---|
| **TC-SRD-01** | Hoạt động bình thường (Healthy Path) | 3 tác vụ demo chạy liên tục 60 phút, định kỳ báo tiến độ hợp lệ | Không có bất kỳ cảnh báo lỗi giả nào; IWDG được refresh đều đặn; không gián đoạn |
| **TC-SRD-02** | Phát hiện task ngừng tiến độ (F01) | Kích hoạt kịch bản dừng báo tiến độ trên Task 2 trong khi Scheduler vẫn hoạt động | Module 1 phát hiện lỗi F01 trong vòng 1 chu kỳ quét + timeout; bắn đúng `task_id` và `generation` |
| **TC-SRD-03** | Khử báo nhầm khi chờ hợp lệ (F04) | Task 1 khai báo `rtsfr_begin_expected_wait(500 ticks)` rồi gọi `vTaskDelay(400 ticks)` | Không sinh sự kiện lỗi trong suốt 400 ticks; chuyển về trạng thái `HEALTHY` an toàn |
| **TC-SRD-04** | Phục hồi tác vụ không giữ tài nguyên | Task 2 bị lỗi F01 khi không giữ Mutex hay Managed Buffer | Task 2 bị xóa và tạo lại thành công; thế hệ mới tăng lên `generation + 1`; Task 1 và Task 3 không bị ảnh hưởng |
| **TC-SRD-05** | Từ chối phục hồi khi giữ Mutex (F06) | Task 3 chiếm giữ Mutex rồi rơi vào vòng lặp vô hạn `while(1)` | Module 3 phát hiện giữ Mutex; Module 2 ra quyết định `SAFE_MODE`; tuyệt đối không tự ý release Mutex |
| **TC-SRD-06** | Thu hồi và dọn dẹp Managed Buffer (F05) | Task 1 giữ một Managed Buffer độc quyền rồi gặp lỗi tự nguyện | Module 3 dọn dẹp buffer về trạng thái `FREE`; Task 1 được tái sinh an toàn và nhận lại buffer mới |
| **TC-SRD-07** | Ngăn chặn bão phục hồi (Retry Budget) | Task 2 bị lỗi liên tiếp 3 lần trong khoảng thời gian dưới 60 giây | Lần lỗi thứ 3 bị từ chối `RESTART`; hệ thống chuyển sang `SAFE_MODE` cách ly task lỗi vĩnh viễn |
| **TC-SRD-08** | Chốt chặn Watchdog phần cứng (F08) | Vô hiệu hóa ngắt toàn cục (`__disable_irq()`) làm scheduler tê liệt hoàn toàn | Không có task nào chạy; IWDG hết hạn sau 1.6 giây và reset phần cứng STM32 thành công |
