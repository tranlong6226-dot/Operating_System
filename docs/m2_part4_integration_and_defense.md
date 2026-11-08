# HƯỚNG DẪN TÍCH HỢP M1–M2–M3–M4 TRÊN KEIL C, BIÊN BẢN TEST TC06/TC07 & KỊCH BẢN BẢO VỆ BTL

> **Module:** Module 2 — Fault Classification & Recovery Policy  
> **Thành viên phụ trách:** Đinh Vũ Long  
> **Các file liên quan:** `RTSFR/Inc/rtsfr_types.h`, `RTSFR/Inc/rtsfr_policy.h`, `RTSFR/Src/rtsfr_policy.c`, `RTSFR/tests/target_test_m2_tc06_tc07.c`

---

## 1. Báo cáo rà soát chéo Header giữa M1 (Việt Anh), M3 (Hồng Long), M4 (Tú) và M2 (Vũ Long)

Khi đối chiếu `rtsfr_detect.h` / `rtsfr_types.h` (nhánh `module1-vietanh`), `rtsfr_resource.h` (nhánh `hlong`) và `rtsfr_recovery.h` / `rtsfr_types.h` (nhánh `main` của Tú), có **5 điểm khác biệt về tên kiểu/trường dữ liệu** đã được giải quyết triệt để trong bản `rtsfr_types.h` và `rtsfr_policy.c/.h` của Phần 4:

| STT | Điểm khác biệt giữa các thành viên | Cách giải quyết trong `rtsfr_types.h` & `rtsfr_policy.c` |
| :---: | :--- | :--- |
| **1** | **Tên trường trong `rtsfr_fault_event_t`:** Việt Anh (M1) dùng `detected_tick` và `last_progress_seq`, còn tài liệu đặc tả mục 5.2 dùng `observed_tick` và `evidence`. | Dùng `union` ẩn danh (anonymous union) cho cả 2 cặp trường trong `rtsfr_fault_event_t`. Nhờ đó code của Việt Anh gọi `ev.detected_tick` và code test gọi `ev.observed_tick` đều truy cập chung một ô nhớ 32-bit, không tốn thêm RAM và không ai phải sửa code. |
| **2** | **Tên kiểu và hằng số lỗi (`rtsfr_fault_type_t` vs `rtsfr_fault_t`):** Việt Anh đặt tên kiểu là `rtsfr_fault_type_t` và có thêm các hằng `RTSFR_FAULT_WAIT_TIMEOUT`, `RTSFR_FAULT_VOLUNTARY`, `RTSFR_FAULT_MUTEX_HELD_DEADLOCK`, `RTSFR_FAULT_KERNEL_PANIC`. | Khai báo đầy đủ toàn bộ hằng số của cả Việt Anh và đặc tả, dùng `typedef rtsfr_fault_type_t rtsfr_fault_t;`, đồng thời cập nhật `rtsfr_policy.c` xử lý `RTSFR_FAULT_KERNEL_PANIC` ở Cổng 2 (`REQUEST_RESET`) và `RTSFR_FAULT_MUTEX_HELD_DEADLOCK` ở Cổng 5 (`SAFE_MODE`). |
| **3** | **Trạng thái task (`RTSFR_TASK_STATE_...` vs `RTSFR_TASK_...`) và mã lỗi (`RTSFR_ERR_NOT_FOUND` vs `RTSFR_ERR_NOT_READY`):** M1 dùng `RTSFR_ERR_NOT_FOUND` và tiền tố `RTSFR_TASK_STATE_...`, trong khi M4 dùng `RTSFR_ERR_NOT_READY`. | Gộp cả hai vào chung `rtsfr_status_t` và `rtsfr_task_state_t` bằng enum alias để cả M1, M2, M3, M4 biên dịch chung không lỗi. |
| **4** | **Định nghĩa lặp `rtsfr_resource_summary_t`:** Trong `rtsfr_resource.h` của Hồng Long có định nghĩa lại `typedef struct { ... } rtsfr_resource_summary_t;` giống trong `rtsfr_types.h` của Tú. | Bọc `#ifndef RTSFR_RESOURCE_SUMMARY_T_DEFINED` quanh `rtsfr_resource_summary_t` trong `rtsfr_types.h` (và khi merge nhánh `hlong`, chỉ cần bọc macro này quanh struct trong `rtsfr_resource.h` là hết lỗi trùng typedef). |
| **5** | **Quy ước đánh số `task_id`:** Trong `rtsfr_recovery.c` của Tú bắt buộc `task_id` từ `1..configRTSFR_MAX_TASKS` (`task_id == 0` bị từ chối), trong khi comment ở M1 ghi `0..MAX-1`. | Khi khởi tạo 3 task demo (`SensorTask`, `CommTask`, `ControlTask`), cả nhóm thống nhất truyền `task_id = 1, 2, 3` để `rtsfr_recovery.c` của Tú không trả về `RTSFR_ERR_INVALID`. Ngoài ra M2 cung cấp sẵn hàm `rtsfr_policy_register_from_config(&m1_cfg, 1U)` nhận trực tiếp struct `rtsfr_task_config_t` của Việt Anh. |

---

## 2. Hướng dẫn tích hợp Module 2 vào Keil µVision5 (`MDK-ARM`) trên STM32F103C8T6

Khi ghép code toàn nhóm trên phần mềm **Keil µVision5**:

1. **Thêm file mã nguồn `.c` vào Project Tree của Keil C:**
   * Mở project trong thư mục `MDK-ARM/` bằng Keil µVision5.
   * Ở cột **Project** bên trái, click chuột phải vào nhóm thư mục **`RTSFR`** (hoặc `Application/User/RTSFR`) $
ightarrow$ chọn **Add Existing Files to Group...**.
   * Chọn file **`RTSFR/Src/rtsfr_policy.c`** $
ightarrow$ bấm **Add** $
ightarrow$ **Close**.
   * *(Lưu ý: Không thêm `test_m2_policy.c` vào Keil build chính vì file đó có hàm `main()` riêng dành cho Host PC. Nếu muốn gọi kiểm thử `TC06`/`TC07` trực tiếp trên mạch STM32, chỉ cần thêm `target_test_m2_tc06_tc07.c` và không bật cờ `RTSFR_STANDALONE_TARGET_TEST_MAIN`).*

2. **Kiểm tra đường dẫn Include Path trong Keil C:**
   * Bấm biểu tượng **Options for Target (hình cây đũa thần)** $
ightarrow$ tab **C/C++**.
   * Tại ô **Include Paths**, đảm bảo đã có đường dẫn trỏ tới thư mục **`../RTSFR/Inc`**.
   * Bật chế độ **C99 Mode** (hoặc AC6 C11).

3. **Đoạn code nối M1 – M2 – M3 – M4 trong `RTSFR_Supervisor`:**
   * Khi khởi tạo hệ thống (trước khi scheduler chạy hoặc đầu hàm Supervisor):
     ```c
     rtsfr_policy_init();
     rtsfr_policy_register_task(1U, 1U, RTSFR_CRIT_MEDIUM); /* SensorTask  */
     rtsfr_policy_register_task(2U, 1U, RTSFR_CRIT_LOW);    /* CommTask    */
     rtsfr_policy_register_task(3U, 1U, RTSFR_CRIT_HIGH);   /* ControlTask */

     /* Gắn callback nhận kết quả từ Module 4 của Tú về Module 2 của Vũ Long */
     rtsfr_recovery_hooks_t hooks = {
         .resource_snapshot  = rtsfr_resource_snapshot,
         .resource_cleanup   = rtsfr_resource_cleanup_safe,
         .record_result      = rtsfr_policy_on_recovery_result,
         /* ... các hook khác của M1/M5 ... */
     };
     rtsfr_recovery_init(&hooks);
     ```
   * Trong vòng lặp định kỳ của `RTSFR_Supervisor` (sau khi gọi `rtsfr_detect_scan(xTaskGetTickCount())`):
     ```c
     rtsfr_decision_t dec;
     while (rtsfr_m2_supervisor_dispatch_once(&dec) != RTSFR_ERR_NO_RESOURCE)
     {
         /* Nếu có tiến trình recovery đang kích hoạt, gọi máy trạng thái của M4 */
         (void)rtsfr_recovery_process();
     }
     (void)rtsfr_recovery_process();
     ```

---

## 3. Biên bản nghiệm thu Test Case TC06 & TC07 (Theo mẫu mục 13.1 Phân công)

### 3.1. Biên bản Test TC06 (Task lỗi khi đang giữ Mutex $
ightarrow$ Chặn Restart, vào `SAFE_MODE`)

```markdown
Test ID: TC06
Build: v2-resource-aware
Board: STM32F103C8T6 / CPU clock: 72 MHz / tick: 1000 Hz
Setup: ControlTask (task_id = 3, generation = 1) đang giữ Mutex (mutex_held_count = 1) và ngừng report progress (RTSFR_FAULT_NO_PROGRESS tại tick 1520)
Expected: FAULT_DETECTED -> POLICY (SAFE_MODE, reason = 0x0401 RTSFR_POL_REASON_UNSAFE_MUTEX_HELD); không tự động unlock mutex, không restart task.
Actual:
  20261108_01,V2,TC06,1520,601,3,1,FAULT_DETECTED,NO_PROGRESS,14
  20261108_01,V2,TC06,1521,601,3,1,POLICY,SAFE_MODE,0x0401
Verdict: PASS
Owner: Hồng Long (M3) + Vũ Long (M2) + Thủy (M5)
```

### 3.2. Biên bản Test TC07 (Lỗi lặp lại vượt ngân sách Retry $
ightarrow$ Chặn Restart Loop, vào `SAFE_MODE`)

```markdown
Test ID: TC07
Build: v2-resource-aware
Board: STM32F103C8T6 / CPU clock: 72 MHz / tick: 1000 Hz
Setup: CommTask (task_id = 2) cấu hình max_restarts = 2 trong cửa sổ retry_window = 60000 ms; tiêm lỗi NO_PROGRESS liên tiếp 3 lần tại tick 1000, 18000, 35000.
Expected:
  - Lỗi 1 (tick 1000, gen 1): POLICY = RESTART (0x0301) -> RECOVERY_OK (lên gen 2)
  - Lỗi 2 (tick 18000, gen 2): POLICY = RESTART (0x0301) -> RECOVERY_OK (lên gen 3)
  - Lỗi 3 (tick 35000, gen 3): Hết ngân sách retry trong cửa sổ 60s -> Chặn RESTART, chuyển POLICY = SAFE_MODE (reason = 0x0405 RTSFR_POL_REASON_RETRY_EXHAUSTED).
Actual:
  20261108_02,V2,TC07,1000,701,2,1,FAULT_DETECTED,NO_PROGRESS,10
  20261108_02,V2,TC07,1001,701,2,1,POLICY,RESTART,0x0301
  20261108_02,V2,TC07,1045,701,2,2,RECOVERY_OK,FIRST_PROGRESS,44
  20261108_02,V2,TC07,18000,702,2,2,FAULT_DETECTED,NO_PROGRESS,20
  20261108_02,V2,TC07,18001,702,2,2,POLICY,RESTART,0x0301
  20261108_02,V2,TC07,18045,702,2,3,RECOVERY_OK,FIRST_PROGRESS,44
  20261108_02,V2,TC07,35000,703,2,3,FAULT_DETECTED,NO_PROGRESS,30
  20261108_02,V2,TC07,35001,703,2,3,POLICY,SAFE_MODE,0x0405
Verdict: PASS
Owner: Vũ Long (M2) + Tú (M4)
```

---

## 4. Kịch bản Thuyết trình 2 phút & Câu hỏi Vấn đáp trước Giảng viên cho Vũ Long (Mục 14 Phân công)

### 4.1. Kịch bản nói 2 phút của Vũ Long (Mục 14 — Thứ tự trình bày số 3)
> *"Thưa thầy/cô, em là Đinh Vũ Long, phụ trách **Module 2 — Phân loại lỗi và Chính sách phục hồi (Fault Classification & Recovery Policy)**.*
>
> *Trong hệ thống RTSFR, nếu chúng ta chỉ phát hiện task treo rồi xóa đi tạo lại một cách máy móc như phiên bản V1, hệ thống sẽ gặp nguy cơ hỏng dữ liệu dùng chung hoặc rơi vào vòng lặp khởi động lại vô tận. Vì vậy, Module 2 của em được thiết kế theo nguyên lý tách biệt giữa **Policy (Chính sách)** và **Mechanism (Cơ chế)**.*
>
> *Cụ thể, hàm `rtsfr_policy_evaluate()` của em hoạt động như một hàm tất định đi qua **6 cổng kiểm tra an toàn tuần tự**:*
> *1. **Chống sự kiện cũ và trùng lặp:** Kiểm tra số thế hệ `generation` và `event_id` để đảm bảo một sự kiện lỗi của thế hệ cũ không bao giờ làm khởi động lại nhầm thế hệ mới vừa phục hồi.*
> *2. **Lọc lỗi mức Kernel/Phần cứng và chờ hợp lệ:** Nếu gặp lỗi không thể tin cậy như tràn Stack hay Kernel Panic thì yêu cầu `REQUEST_RESET` ngay; nếu task đang chờ hợp lệ (`Expected Wait`) thì trả về `IGNORE` để chống báo động giả.*
> *3. **Quản lý ngân sách thử lại (Retry Budget) theo cửa sổ trượt:** Mặc định mỗi task chỉ được phép `RESTART` tối đa 2 lần trong cửa sổ 60 giây (xử lý bằng phép trừ không dấu để chống tràn bộ đếm Tick 32-bit). Ở lần lỗi thứ 3 trong cửa sổ (`TC07`), Module 2 sẽ chặn `RESTART` và đưa về `SAFE_MODE`.*
> *4. **Cổng an toàn tài nguyên (Resource Safety Gate):** Dựa trên bản chụp tài nguyên từ Module 3, nếu task đang giữ bất kỳ khóa `Mutex` nào (`TC06`), có I/O đang truyền dở, hoặc tài nguyên không thể thu hồi sạch (`can_reclaim_all == false`), Module 2 **tuyệt đối cấm `RESTART`** và chuyển hệ thống về `SAFE_MODE` kèm mã lý do `reason_code` tường minh.*
> *5. Chỉ khi vượt qua toàn bộ các cổng an toàn trên, Module 2 mới cấp quyền `RTSFR_ACT_RESTART` sang cho Module 4 thực thi."*

### 4.2. Bộ 3 câu hỏi Giảng viên hay hỏi nhất ở Module 2 & Cách trả lời ghi điểm
* **Câu hỏi 1:** *"Tại sao khi một task bị treo lúc đang giữ Mutex (`TC06`), em không cho hệ thống tự mở khóa (`give/unlock`) Mutex đó rồi `RESTART` task luôn cho tiện, mà lại phải chuyển sang `SAFE_MODE`?"*
  * **Trả lời:** Thưa thầy/cô, mục đích của Mutex là bảo vệ một vùng dữ liệu dùng chung (Critical Section). Khi một task đang giữ Mutex mà bị lỗi treo, rất có thể nó đang ghi/sửa cấu trúc dữ liệu dùng chung đó dở dang. Nếu hệ thống cưỡng bức mở khóa Mutex cho task khác vào đọc, task khác sẽ đọc phải dữ liệu hỏng (inconsistent state). Vì trên vi điều khiển không có cơ chế giao dịch hoàn nguyên bộ nhớ (transactional memory rollback), việc từ chối `RESTART` và đưa về `SAFE_MODE` là cách duy nhất đảm bảo tính đúng đắn của hệ thống.
* **Câu hỏi 2:** *"Làm sao Module 2 của em biết được khi nào hết 60 giây của cửa sổ Retry nếu biến đếm `TickType_t` của FreeRTOS chạy lâu ngày bị tràn qua 0 (wrap-around)?"*
  * **Trả lời:** Em không dùng phép cộng `window_start_tick + 60000` để so sánh vì phép cộng sẽ bị sai khi tràn số. Thay vào đó, em dùng phép trừ hai số nguyên không dấu: `(TickType_t)(detected_tick - window_start_tick) >= retry_window_ticks`. Trong số học bù 2 không dấu của C, phép trừ này luôn cho khoảng thời gian trôi qua chính xác ngay cả khi `detected_tick` vừa tràn qua 0 (đã được em chứng minh trong bài kiểm thử `UT17`).
* **Câu hỏi 3:** *"Nếu Module 4 đang xóa và tạo lại một task mà Module 1 lại quét và gửi thêm một báo cáo lỗi của task đó tới thì Module 2 xử lý thế nào?"*
  * **Trả lời:** Em chặn ở hai lớp: thứ nhất, nếu cùng `event_id` thì Cổng 1 trả về `RTSFR_ACT_IGNORE` (`DUPLICATE_EVENT`); thứ hai, nếu khác `event_id` nhưng trạng thái task trong Module 2 đang là `QUIESCING/CLEANUP/RECREATE/VERIFYING` thì Cổng 3 trả về `RTSFR_ACT_DEFER` (`RECOVERY_IN_PROGRESS`) nên không bao giờ kích hoạt hai luồng phục hồi chồng chéo nhau.
