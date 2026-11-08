# BUILD, KIỂM THỬ VÀ DEMO HỆ THỐNG RTSFR

> Board: STM32F103C8T6 Blue Pill  
> Giao tiếp: USART1, 115200-8-N-1  
> Kịch bản: `D01 → D02 → D03 → D04 → D05`  
> Dữ liệu hiển thị là telemetry thật từ STM32

Tài liệu này là cách vận hành duy nhất cho firmware: chuẩn bị phần cứng,
build/nạp, kiểm thử, demo D01–D05, lưu log và xử lý sự cố.

## 1. Thông điệp cần chứng minh

```text
FAULT → RESOURCE → POLICY → RECOVERY hoặc SAFE_MODE
```

RTSFR không restart task một cách mù quáng:

- Không giữ tài nguyên nguy hiểm: restart riêng task lỗi.
- Giữ managed buffer có thể thu hồi: cleanup rồi restart.
- Giữ mutex: từ chối restart và vào SAFE_MODE.
- Kernel không còn chạy: IWDG reset toàn bộ STM32.

PC Terminal chỉ gửi lệnh và hiển thị log. Mọi quyết định diễn ra trong STM32.

Trong firmware demo, D02, D03 và D04 là ba acceptance scenario độc lập nên test harness
reset riêng retry window trước mỗi kịch bản. Cơ chế production vẫn giữ retry history;
TC07 tiếp tục kiểm tra trường hợp lỗi lặp lại dẫn tới `RETRY_EXHAUSTED`.

## 2. Chuẩn bị phần cứng, build và terminal

### 2.1. Thiết bị

- STM32F103C8T6 Blue Pill.
- ST-LINK V2.
- USB-UART mức logic 3.3 V.
- Keil µVision 5.

Nối ST-LINK:

| ST-LINK | Blue Pill |
|---|---|
| SWDIO | PA13 |
| SWCLK | PA14 |
| GND | GND |
| 3.3 V/VTref | 3.3 V theo cách cấp nguồn đang sử dụng |
| NRST, nếu dùng | NRST |

Không cấp nguồn 5 V trực tiếp vào chân 3.3 V của board.

Nối USB-UART:

| USB-UART | Blue Pill |
|---|---|
| RX | PA9 / USART1_TX |
| TX | PA10 / USART1_RX |
| GND | GND |

TX/RX phải nối chéo và hai thiết bị phải chung GND.

### 2.2. Build và nạp firmware

1. Mở `firmware/MDK-ARM/Operating_System_Project.uvprojx`.
2. Chọn target `Operating_System_Project`.
3. Chạy **Rebuild all target files**.
4. Chỉ tiếp tục nếu Keil báo `0 Error(s), 0 Warning(s)`.
5. Chọn **Download**, sau đó reset board.

Firmware ArmClang 6.24 phải sử dụng FreeRTOS port:

```text
Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM3
```

### 2.3. Terminal PowerShell có sẵn trong project

Không cần cài PuTTY. Mở PowerShell tại thư mục project và chạy:

```powershell
powershell -ExecutionPolicy Bypass -File .\experiment\tools\rtsfr_terminal.ps1
```

Script sẽ:

1. Liệt kê các cổng COM hiện có.
2. Yêu cầu chọn cổng bằng số.
3. Mở cổng ở 115200-8-N-1.
4. Tự gửi `format terminal`.
5. Cho phép nhập trực tiếp `status`, `verify`, `demo d01`...

Nếu đã biết cổng, ví dụ COM5:

```powershell
powershell -ExecutionPolicy Bypass -File .\experiment\tools\rtsfr_terminal.ps1 -Port COM5
```

Nhấn `Ctrl+C` để đóng cổng đúng cách. Nếu không thấy boot log, nhấn nút RESET trên
Blue Pill trong khi terminal đang mở.

### 2.4. Cấu hình UART khi dùng terminal khác

Nối USB-UART:

| USB-UART | Blue Pill |
|---|---|
| RX | PA9 / USART1_TX |
| TX | PA10 / USART1_RX |
| GND | GND |

Cấu hình PuTTY, Tera Term hoặc Serial Monitor:

```text
Baud rate   : 115200
Data bits   : 8
Parity      : None
Stop bits   : 1
Flow control: None
Line ending : CR hoặc CRLF
```

Không mở đồng thời nhiều terminal hoặc Serial Monitor trên cùng một cổng COM.

## 3. Kiểm tra sau khi boot

Sau reset, terminal phải hiện:

```text
==================================================
 RTSFR TERMINAL DEMO - STM32 FreeRTOS
 Commands: demo d01 | d02 | d03 | d04 | d05
==================================================
[BOOT]     ...
[BOOT]     reset_cause=POWER_ON ...
```

Nếu terminal đang in CSV, gõ:

```text
format terminal
```

Trước khi demo, gõ:

```text
verify
status
```

Kết quả tối thiểu:

```text
[VERIFY]   result=PASS

[STATUS]   tick=...
  SENSOR  Gen=1 Count=... HEALTHY Resource=SAFE
  COMM    Gen=1 Count=... HEALTHY Resource=SAFE
  CONTROL Gen=1 Count=... HEALTHY Resource=SAFE
  SYSTEM safe_mode=NO ...
```

Nếu `VERIFY FAIL`, trace dropped hoặc resource bất thường thì không bắt đầu demo.

## 4. Lời thoại mở đầu

> “Nếu một task FreeRTOS bị lỗi, reset toàn bộ MCU sẽ làm cả các task khỏe bị dừng.
> RTSFR phát hiện task lỗi, kiểm tra task đang giữ tài nguyên gì, rồi mới quyết định
> restart riêng task, chuyển SAFE_MODE hoặc dùng watchdog. Terminal chỉ hiển thị quyết
> định thật từ kernel trên STM32.”

Chỉ cho thầy quan sát bốn loại dòng:

```text
[FAULT]    Task nào lỗi?
[RESOURCE] Task đang giữ gì?
[POLICY]   Kernel quyết định gì?
[VERIFY]   Kết quả phục hồi có đúng không?
```

## 5. D01 — Normal Operation

Reset board, sau đó gõ:

```text
demo d01
```

Trong lúc test chạy, gõ `status` hai lần cách nhau khoảng hai giây.

Phải quan sát được:

- Ba LED đều đổi trạng thái.
- Count của ba task đều tăng.
- Generation của cả ba task giữ nguyên.
- Không có `[FAULT]`, `[POLICY]`, restart hoặc SAFE_MODE.
- Sau khoảng năm giây:

```text
[RESULT]   D01 NORMAL OPERATION result=PASS
```

Kết luận cần nói:

> “Trong điều kiện bình thường RTSFR chỉ giám sát, không can thiệp sai.”

## 6. D02 — Selective Recovery

Gõ:

```text
demo d02
```

Chuỗi chính cần nhìn:

```text
[INJECT]   ... task=COMM ... fault=STOP_PROGRESS
[FAULT]    ... task=COMM ... reason=NO_PROGRESS
[RESOURCE] ... task=COMM mutex=0 buffer=0 ... reclaimable=YES
[POLICY]   ... task=COMM action=RESTART reason=NO_DANGEROUS_RESOURCE
[QUIESCE]  ... task=COMM ...
[RECREATE] ... task=COMM ...
[VERIFY]   ... task=COMM gen=2 result=FIRST_PROGRESS
[VERIFY]   ... task=COMM gen=2 result=RECOVERY_OK
[RESULT]   D02 SELECTIVE RECOVERY result=PASS
```

Sau đó gõ:

```text
status
```

Cần chỉ ra:

- Chỉ CommTask tăng generation.
- SensorTask và ControlTask giữ generation cũ.
- Count của hai task khỏe vẫn tăng.
- MCU không reset toàn bộ.

Kết luận:

> “RTSFR chỉ tạo lại task lỗi, các task khỏe tiếp tục hoạt động.”

## 7. D03 — Resource-aware Recovery

Gõ:

```text
demo d03
```

Chuỗi chính:

```text
[FAULT]    ... task=COMM ... reason=NO_PROGRESS
[RESOURCE] ... task=COMM mutex=0 buffer=1 inflight=NO reclaimable=YES
[POLICY]   ... task=COMM action=RESTART reason=BUFFER_RECLAIMABLE
[QUIESCE]  ...
[CLEANUP]  ... task=COMM ...
[DELETE]   ...
[RECREATE] ...
[VERIFY]   ... result=FIRST_PROGRESS
[VERIFY]   ... result=RECOVERY_OK
[RESULT]   D03 RESOURCE-AWARE RECOVERY result=PASS
```

Điểm cần nhấn mạnh:

- Buffer không bị bỏ quên khi task cũ bị xóa.
- Cleanup diễn ra trước recreate.
- `RECOVERY_OK` chỉ xuất hiện sau first progress của instance mới.

Kết luận:

> “RTSFR xử lý ownership của tài nguyên trước khi restart task.”

## 8. D04 — Unsafe Recovery Prevention

Gõ:

```text
demo d04
```

Chuỗi cần nhìn:

```text
[FAULT]    ... task=COMM ... reason=NO_PROGRESS
[RESOURCE] ... task=COMM mutex=1 buffer=0 ... reclaimable=NO
[POLICY]   ... task=COMM action=SAFE_MODE reason=MUTEX_HELD
[SYSTEM]   ... state=SAFE_MODE ... restart=DENIED
[RESULT]   D04 UNSAFE RECOVERY PREVENTION result=PASS
```

Không được có dòng recreate CommTask sau policy SAFE_MODE.

Cần chỉ ra:

- Kernel không tự give/unlock mutex.
- Kernel không restart CommTask một cách nguy hiểm.
- PC13 sáng ổn định báo SAFE_MODE.

Kết luận:

> “Khi không thể đảm bảo dữ liệu nhất quán, RTSFR ưu tiên an toàn thay vì cố restart.”

SAFE_MODE được latch. Reset board trước D05.

## 9. D05 — IWDG Watchdog Fallback

D05 chỉ chạy sau khi Gate G4 đã xác nhận cấu hình IWDG.

Sau reset, gõ:

```text
inject f08
```

Có thể dùng lệnh tương đương:

```text
demo d05
```

Trước khi kernel stall, terminal phải kịp hiện thông tin bắt đầu/injection. Sau đó sẽ
không còn log vì scheduler/interrupt đã bị dừng.

Kết quả đúng:

1. IWDG không được refresh.
2. STM32 reset phần cứng.
3. Sau boot terminal hiện:

```text
--- MCU REBOOT ---
[BOOT]     reset_cause=IWDG ...
[RESULT]   D05 IWDG WATCHDOG FALLBACK result=PASS
```

Không dùng `NVIC_SystemReset()` thay thế vì không chứng minh watchdog hoạt động.

Nếu thấy:

```text
[ERROR]    D05 IWDG WATCHDOG FALLBACK NOT_READY
```

thì `RTSFR_IWDG_CONFIG_CONFIRMED` vẫn bằng `0`. Đây là guard an toàn.

## 10. Kiểm thử target bổ sung

Ngoài năm kịch bản trình diễn, firmware còn có các test kỹ thuật:

| Test | Lệnh | Mục đích |
|---|---|---|
| TC03 | `test 3` | Busy-loop nhưng supervisor/tick vẫn chạy |
| TC04 | `test 4` | Expected wait hợp lệ không bị báo lỗi |
| TC07 | `test 7` | Hết retry budget thì chuyển SAFE_MODE |
| TC08 | `test 8` | Ép tạo task thất bại và fail-safe |
| TC09 | `test 9` | Progress mới hủy false alarm |
| TC10 | `test 10` | Từ chối event thuộc generation cũ |
| TC11 | `test 11` | Recovery lặp lại và kiểm tra heap stability |
| TC13 | `test 13` | Event queue overflow được ghi nhận |
| TC14 | `test 14` | Stress resource ownership |

Reset board giữa các test độc lập. Những test latch SAFE_MODE bắt buộc phải reset
trước khi chạy kịch bản khác.

## 11. Lưu log và phân tích kết quả

Gõ `format csv`, lưu toàn bộ UART output vào:

```text
experiment/logs/raw/<ten_lan_chay>.csv
```

Từ thư mục gốc project, chạy:

```powershell
python experiment/tools/rtsfr_log_analyzer.py experiment/logs/raw/demo_rtsfr_01.csv -o experiment/reports/demo_rtsfr_01_summary.md
```

Báo cáo tổng hợp PASS/FAIL, detection latency, recovery latency, trace drop,
minimum heap và monitor cycles lớn nhất.

Các reason quan trọng trong output `status`:

| Reason | Ý nghĩa |
|---:|---|
| 0 | Trace dropped |
| 1 | UART RX dropped |
| 2 | UART TX errors |
| 3 | IWDG đã start |
| 4 | DWT cycle counter sẵn sàng |
| 5 / 6 | Free heap / minimum-ever-free-heap |
| 10 | Progress counter của task 1–3 |
| 11 / 12 | Stack high-water mark của application task / bootstrap task |
| 13 | Số vòng supervisor monitor |
| 14 / 15 | Monitor cycles gần nhất / lớn nhất |
| 16 / 17 | Event overflow / resource anomaly |
| 18 / 19 / 20 | Recovery thành công / unsafe restart bị chặn / false alarm rollback |

## 12. Lệnh terminal

| Lệnh | Chức năng |
|---|---|
| `help` | Hiện danh sách lệnh |
| `status` | In progress, generation, health và resource của ba task |
| `verify` | Kiểm tra invariant hệ thống |
| `demo d01` | Normal Operation |
| `demo d02` | Selective Recovery |
| `demo d03` | Resource-aware Recovery |
| `demo d04` | Unsafe Recovery Prevention |
| `demo d05` | IWDG Watchdog Fallback |
| `inject f08` | Chạy trực tiếp fault kernel stall D05 |
| `format terminal` | Log dễ đọc trên terminal |
| `format csv` | Log CSV cho công cụ phân tích |

## 13. Checklist nghiệm thu

- [ ] Build `0 Error(s), 0 Warning(s)` và nạp firmware mới.
- [ ] Boot banner và reset cause xuất hiện.
- [ ] `verify` trả PASS.
- [ ] D01: ba Count tăng, generation không đổi.
- [ ] D02: chỉ CommTask tăng generation.
- [ ] D03: buffer reclaimable, cleanup trước recreate.
- [ ] D04: mutex không reclaimable, SAFE_MODE, không restart.
- [ ] Reset board sau D04.
- [ ] D05: reset cause thực sự là IWDG.

## 14. Xử lý sự cố

### Terminal không có dữ liệu

- Kiểm tra baud 115200 và GND chung.
- Kiểm tra TX/RX đã nối chéo.
- Kiểm tra PA9/PA10 chưa bị ngoại vi khác chiếm.
- Nhấn RESET khi terminal đang mở để quan sát boot banner.

### Có log nhưng lệnh không phản hồi

- Chọn line ending CR hoặc CRLF.
- Kiểm tra USB-UART TX nối vào PA10.
- Kiểm tra USART1 global interrupt và lời gọi `rtsfr_m5_uart_irq()`.

### Build báo thiếu `portmacro.h`

Kiểm tra include path và `port.c` thuộc:

```text
Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM3
```

### Sau khi CubeMX Generate, build hoặc demo bị lỗi

Kiểm tra lại:

1. FreeRTOS GCC/ARM_CM3 port còn trong project.
2. `queue.c` còn các mutex hook RTSFR.
3. `tasks.c` còn `uxTaskGetDeletedTasksWaitingCleanUp()`.
4. USART1 IRQ còn gọi `rtsfr_m5_uart_irq()`.
5. Các group RTSFR/App/board/tests còn trong project Keil.

Không nạp file AXF cũ nếu build mới đang lỗi.

## 15. Câu kết luận

> “RTSFR không chỉ phát hiện và restart task lỗi. Hệ thống kiểm tra trạng thái tài nguyên
> để quyết định có thể phục hồi an toàn hay không, đồng thời giữ các task khỏe tiếp tục
> hoạt động. Khi kernel không còn khả năng tự phục hồi, IWDG cung cấp phương án dự phòng
> ở mức phần cứng.”
