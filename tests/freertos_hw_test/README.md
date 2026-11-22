# FreeRTOS vs RTSFR Dual-Mode Hardware Test

> **Board mục tiêu:** STM32F103C8T6 (Blue Pill)  
> **Môi trường:** Keil MDK-ARM (Arm Compiler 6)  
> **Đặc điểm:** Không cần USB-to-UART! Kiểm thử và quan sát 100% bằng **3 đèn LED** và **1 sợi dây cắm (Jumper)**.  
> **Chuyển đổi chế độ:** Bằng duy nhất biến `#define USE_RTSFR` trong file `Core/Inc/main.h`.

---

## 1. Cơ Chế Chuyển Đổi (1 Project - 2 Chế Độ)

Trong file [main.h](Core/Inc/main.h#L29):

```c
/* ==============================================================================
 * CẤU HÌNH BẬT / TẮT HỆ THỐNG RTSFR ĐỐI CHỨNG
 * ==============================================================================
 * 1: BẬT RTSFR (Tự phục hồi, mã hóa thế hệ Gen ra nhịp chớp LED, Safe Mode)
 * 0: TẮT RTSFR (Vanilla FreeRTOS thuần, không có giám sát, không phục hồi)
 * ============================================================================== */
#define USE_RTSFR 1   // Đổi thành 0 nếu muốn kiểm tra FreeRTOS gốc
```

---

## 2. Sơ Đồ Đấu Nối Phần Cứng

Chỉ cần nối 2 LED rời và 1 dây cắm jumper:

| Linh kiện | Chân STM32 | Chức năng | Ý nghĩa quan sát |
| :--- | :--- | :--- | :--- |
| **LED Đỏ** | `PB0` (Output PP) | **Task 1: SensorTask** | **Đồng hồ nhịp 1Hz (Metronome):** Luôn chớp 500ms ON / 500ms OFF. Chứng minh MCU và Kernel không bao giờ bị reset. |
| **LED Xanh** | `PB1` (Output PP) | **Task 2: CommTask** | **Chịu lỗi & Đếm thế hệ:**<br>• `USE_RTSFR = 1`: Chớp theo số xung bằng đúng `Generation` (Gen 1: 1 nhịp, Gen 2: 2 nhịp, Gen 3: 3 nhịp).<br>• `USE_RTSFR = 0`: Chớp đều 500ms, khi lỗi thì tắt lịm. |
| **LED Bo** | `PC13` (Active LOW) | **Supervisor / Safe Mode** | • Bình thường: Nháy nhẹ 50ms mỗi giây (Heartbeat).<br>• Đang tái sinh Task: Chớp Strobe nhanh liên tục (~12Hz).<br>• Kích hoạt Safe Mode: SÁNG ĐỨNG CỐ ĐỊNH. |
| **Dây Cắm** | `PA1` (Input Pull-Down) | **Bơm lỗi vật lý** | • **Cắm 3.3V:** Ngoại vi bình thường (Mức 1).<br>• **Rút ra (0V):** Giả lập ngoại vi treo / mất tín hiệu cảm biến. |

---

## 3. Các Bước Thử Nghiệm Thực Tế

### THỬ NGHIỆM 1: Chế độ CÓ RTSFR (`#define USE_RTSFR 1`)

#### Bước 1.1: Khởi động bình thường
* Cắm sợi dây `PA1` vào chân `3.3V`.
* Nạp code và nhấn nút RESET đen trên bo.
* **Hiện tượng:**
  * LED Đỏ (`PB0`) chớp đều 1Hz.
  * LED Xanh (`PB1`) chớp **1 nhịp rồi nghỉ 800ms** (Báo hiệu đang ở **Generation 1**).
  * LED Bo (`PC13`) nháy nhẹ 50ms mỗi 1 giây (Heartbeat của RTSFR Supervisor).

#### Bước 1.2: Lỗi tạm thời & Tự phục hồi thế hệ mới (Self-Healing)
* **Rút dây `PA1` ra khỏi 3.3V** (chân `PA1` rơi về 0V):
  * LED Xanh (`PB1`) lập tức tắt lịm (Task bị kẹt ngoại vi, ngừng báo tiến độ).
  * LED Đỏ (`PB0`) vẫn chớp đều (chứng minh các task khác không bị ảnh hưởng).
  * Đúng 2 giây sau (Timeout): **LED Bo (`PC13`) chớp Strobe nhanh** trong ~0.5 giây (Module 1 bắt lỗi, Module 4 tiêu hủy task cũ và tạo task thế hệ 2).
* **Cắm lại dây `PA1` vào 3.3V** (sau 1–2 giây):
  * **LED Xanh (`PB1`) TỰ ĐỘNG SÁNG LẠI**, đổi nhịp thành: **CHỚP KÉP 2 NHỊP RỒI NGHỈ** (`Tách - Tách ... Nghỉ 800ms`)!
  * **Ý nghĩa:** Mắt thường đếm được 2 nhịp chớp, chứng minh task mới thế hệ 2 (**Generation 2**) đã thay thế task cũ.

*(Nếu lặp lại rút và cắm lại lần nữa: LED Xanh sẽ chớp **3 nhịp** = Generation 3!)*

#### Bước 1.3: Lỗi vĩnh viễn & Chế độ An toàn (Safe Mode)
* **Rút dây `PA1` ra và ĐỂ NGUYÊN không cắm lại**:
  * RTSFR tự tái sinh `Gen 2` -> `Gen 3` -> `Gen 4`.
  * Sau 3 lần tái sinh không thành công, Module 2 kết luận lỗi phần cứng vĩnh viễn và ra lệnh vào **SAFE MODE**.
* **Hiện tượng:**
  * **LED Bo (`PC13`) SÁNG ĐỨNG CỐ ĐỊNH** (Solid ON).
  * LED Xanh (`PB1`) tắt hẳn và Task 2 bị đình chỉ để bảo vệ CPU/RAM.
  * LED Đỏ (`PB0`) vẫn tiếp tục chớp bình thường.

---

### THỬ NGHIỆM 2: Chế độ KHÔNG DÙNG RTSFR (`#define USE_RTSFR 0`)

1. Mở [Core/Inc/main.h](Core/Inc/main.h), sửa thành `#define USE_RTSFR 0`.
2. Nhấn **F7** (Build) trong Keil MDK-ARM và nhấn **F8** (Download) nạp lại mạch.
3. Cắm dây `PA1` vào `3.3V` -> Cả 3 LED chớp bình thường (LED Xanh chớp đều 500ms thông thường, không có đếm nhịp).
4. **Rút dây `PA1` ra:**
   - **LED Xanh (`PB1`) tắt lịm** (rơi vào vòng lặp lỗi Deadlock vô hạn).
   - FreeRTOS hoàn toàn không phát hiện lỗi, không có đèn Strobe, không có tái sinh.
5. **CẮM LẠI DÂY `PA1` VÀO 3.3V:**
   - **LED XANH VẪN TẮT CỨNG, KHÔNG THỂ CHẠY LẠI!**
   - *Nguyên nhân:* Task cũ đã bị kẹt cứng trong vòng lặp lỗi (Error Trap), dù ngoại vi có tín hiệu lại thì task cũng không thể tự thoát ra.
   - Muốn chạy lại? Người dùng **bắt buộc phải bấm nút RESET đen trên bo**, làm mất luôn nhịp của LED Đỏ!
   - *So sánh với RTSFR:* Khi có RTSFR (`USE_RTSFR = 1`), task kẹt này đã được tiêu hủy và thay thế bằng một Task mới tinh, nên cắm lại dây là **tự động chạy lại với 2 nhịp chớp (Gen 2)!**

---

## 4. Hướng dẫn Mở và Build Project

1. Khởi động Keil µVision 5.
2. Mở file project:
   ```text
   tests/freertos_hw_test/MDK-ARM/freertos_hw_test.uvprojx
   ```
3. Nhấn **F7** (Rebuild): Đảm bảo `0 Error(s), 0 Warning(s)`.
4. Cắm mạch ST-Link và nhấn **F8** (Download) để bắt đầu thử nghiệm.
