# RTSFR — Resource-Aware Task Supervision and Fault Recovery

RTSFR là phần mở rộng cho FreeRTOS trên STM32F103, được xây dựng để phát hiện
task mất tiến triển, kiểm tra tài nguyên task đang sở hữu và chọn hành động phục
hồi phù hợp. Hệ thống có thể restart riêng task lỗi, cleanup tài nguyên có hợp
đồng thu hồi, chuyển `SAFE_MODE` khi trạng thái không an toàn hoặc dùng IWDG để
reset MCU khi kernel không còn hoạt động.

```text
Detection → Resource Tracking → Policy → Recovery / SAFE_MODE / IWDG Reset
```

PC chỉ gửi lệnh và hiển thị telemetry qua UART. Quyết định phát hiện, policy và
phục hồi được thực hiện trên STM32 chạy FreeRTOS.

## Chức năng chính

- Giám sát progress và health của các application task.
- Phát hiện no-progress, expected wait, stale generation và fault lặp lại.
- Theo dõi mutex, managed buffer và trạng thái resource ownership.
- Selective restart: chỉ tạo lại task lỗi, giữ các task khỏe tiếp tục chạy.
- Cleanup managed buffer trước khi recreate task.
- Từ chối restart khi task giữ mutex hoặc tài nguyên không an toàn.
- Recovery pipeline: `QUIESCING → CLEANUP → RECREATE → VERIFYING`.
- Xác nhận instance mới bằng first progress và generation mới.
- Retry budget để chống restart storm.
- `SAFE_MODE` cho lỗi không thể phục hồi an toàn.
- IWDG fallback khi scheduler/kernel ngừng hoạt động.
- Terminal UART và log CSV để đo detection/recovery latency.

## Nền tảng

| Thành phần | Cấu hình |
|---|---|
| MCU/board | STM32F103C8T6 Blue Pill |
| RTOS | FreeRTOS 10.3.1 / CMSIS-RTOS v2 |
| IDE | Keil µVision 5 |
| Compiler | ArmClang 6.24 |
| CubeMX toolchain metadata | MDK-ARM V5.32 |
| Giao tiếp | USART1, 115200-8-N-1 |
| LED trạng thái | PC13, active-low |
| Watchdog | IWDG |

## Cấu trúc repository

```text
Operating_System_Project/
├── README.md
├── docs/
│   ├── 01_DAC_TA_HE_THONG_RTSFR.md
│   ├── 02_PHAN_CONG_CONG_VIEC_VA_QUY_UOC_NHOM.md
│   ├── 03_CAU_HINH_CUBEMX_RTSFR.md
│   └── 04_BUILD_TEST_VA_DEMO_RTSFR.md
├── firmware/
│   ├── Operating_System_Project.ioc
│   ├── Core/
│   ├── Drivers/
│   ├── Middlewares/          # FreeRTOS và CMSIS-RTOS
│   ├── RTSFR/                # Detection, policy, resource, recovery, trace
│   ├── App/                  # UART commands và fault injection
│   ├── board/                # USART1, DWT cycle counter và IWDG
│   ├── tests/target/         # Test runner chạy trên STM32
│   └── MDK-ARM/              # Project Keil và build output
└── experiment/
    ├── tools/                # Terminal PowerShell và log analyzer
    ├── logs/raw/
    ├── results/
    └── reports/
```

`firmware/tests/target` thuộc firmware vì các test này được biên dịch và chạy
trực tiếp trên STM32. `experiment` chỉ chứa công cụ và kết quả phía PC.

## Kết nối phần cứng

### ST-LINK

| ST-LINK | Blue Pill |
|---|---|
| SWDIO | PA13 |
| SWCLK | PA14 |
| GND | GND |
| 3.3 V/VTref | 3.3 V |
| NRST, nếu dùng | NRST |

### USB-UART

| USB-UART | Blue Pill |
|---|---|
| RX | PA9 / USART1_TX |
| TX | PA10 / USART1_RX |
| GND | GND |

USB-UART phải dùng logic 3.3 V. TX/RX phải nối chéo và hai thiết bị phải chung
GND.

## Build và nạp firmware

1. Mở `firmware/MDK-ARM/Operating_System_Project.uvprojx` bằng Keil.
2. Chọn target `Operating_System_Project`.
3. Chạy **Rebuild all target files**.
4. Chỉ nạp khi build báo `0 Error(s), 0 Warning(s)`.
5. Chọn **Download** và reset board.

FreeRTOS port dùng với ArmClang nằm tại:

```text
firmware/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM3
```

Không cần CubeMX Generate sau khi clone hoặc di chuyển project. Chỉ generate khi
thay đổi GPIO, clock, UART, FreeRTOS hoặc ngoại vi. Trước khi generate cần sao
lưu project và kiểm tra lại custom source groups, include paths, `tasks.c`,
`queue.c` và USART1 IRQ.

## Mở terminal

Tại thư mục gốc project, chạy:

```powershell
powershell -ExecutionPolicy Bypass -File .\experiment\tools\rtsfr_terminal.ps1
```

Script sẽ liệt kê các cổng COM để lựa chọn. Nếu đã biết cổng, ví dụ COM5:

```powershell
powershell -ExecutionPolicy Bypass -File .\experiment\tools\rtsfr_terminal.ps1 -Port COM5
```

Sau khi kết nối, reset board rồi kiểm tra:

```text
verify
status
```

`verify` phải trả PASS và progress counter của ba task phải tăng trước khi chạy
demo.

## Demo D01–D05

| Demo | Lệnh | Kết quả cần chứng minh |
|---|---|---|
| D01 | `demo d01` | Ba task hoạt động; không false alarm hoặc restart |
| D02 | `demo d02` | Restart riêng task mất progress; task khỏe tiếp tục chạy |
| D03 | `demo d03` | Cleanup managed buffer rồi recreate task |
| D04 | `demo d04` | Task giữ mutex không bị restart; hệ thống vào SAFE_MODE |
| D05 | `demo d05` | Kernel stall làm IWDG reset MCU và xác nhận reset cause |

`SAFE_MODE` được latch, vì vậy cần reset board sau D04 và các target test có
kết quả fail-safe trước khi chạy kịch bản độc lập tiếp theo.

### Lệnh terminal chính

| Lệnh | Chức năng |
|---|---|
| `help` | Hiển thị danh sách lệnh |
| `status` | In progress, generation, health và resource |
| `verify` | Kiểm tra invariant hệ thống |
| `demo d01` … `demo d05` | Chạy các kịch bản trình diễn |
| `test 1` … `test 14` | Chạy target test kỹ thuật |
| `inject f08` | Tạo kernel stall cho watchdog |
| `format terminal` | Log dễ đọc trên terminal |
| `format csv` | Log CSV dùng cho phân tích kết quả |

## Phân tích log

Lưu log CSV vào `experiment/logs/raw`, sau đó chạy:

```powershell
python experiment/tools/rtsfr_log_analyzer.py experiment/logs/raw/demo.csv -o experiment/reports/demo_summary.md
```

Công cụ tổng hợp PASS/FAIL, detection latency, recovery latency, trace drop,
minimum heap và monitor cycles lớn nhất.

## Cấu hình quan trọng hiện tại

| Macro | Giá trị |
|---|---:|
| `configTOTAL_HEAP_SIZE` | 8192 bytes |
| `configCHECK_FOR_STACK_OVERFLOW` | 2 |
| `configUSE_MALLOC_FAILED_HOOK` | 1 |
| `configRTSFR_M5_ENABLE` | 1 |
| `RTSFR_IWDG_CONFIG_CONFIRMED` | 1 |
| `configRTSFR_M4_TARGET_TEST` | 0 |

Build kiểm tra gần nhất sau khi tái cấu trúc thư mục:

```text
Code=40260  RO-data=3748  RW-data=60  ZI-data=16068
0 Error(s), 0 Warning(s)
```

Log build: `firmware/MDK-ARM/restructure_rebuild.log`.

## Tài liệu

- [Đặc tả và ma trận tuân thủ](docs/01_DAC_TA_HE_THONG_RTSFR.md)
- [Phân công và quy ước nhóm](docs/02_PHAN_CONG_CONG_VIEC_VA_QUY_UOC_NHOM.md)
- [Cấu hình CubeMX](docs/03_CAU_HINH_CUBEMX_RTSFR.md)
- [Build, kiểm thử và demo](docs/04_BUILD_TEST_VA_DEMO_RTSFR.md)

## Lưu ý

- Không mở đồng thời nhiều terminal trên cùng một cổng COM.
- Không đánh dấu target test PASS nếu chưa có telemetry thật từ STM32.
- Không dùng `NVIC_SystemReset()` thay cho D05 vì không chứng minh được IWDG.
- Không tự động unlock mutex của task lỗi.
- Không nạp file AXF cũ khi lần build hiện tại đang lỗi.
