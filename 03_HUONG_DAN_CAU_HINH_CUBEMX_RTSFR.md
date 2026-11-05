# HƯỚNG DẪN CẤU HÌNH STM32CUBEMX CHO DỰ ÁN RTSFR

> **Dự án:** Resource-aware Task Selective Fault Recovery (RTSFR)  
> **Vi điều khiển:** STM32F103C8T6 (Blue Pill)  
> **Công cụ sinh project:** STM32CubeMX 6.18.1 / STM32Cube FW_F1 V1.8.7  
> **IDE mục tiêu:** Keil µVision5, MDK-ARM V5.32, ARM Compiler 5/RVDS  
> **Ngày đối chiếu project:** 05/11/2026  
> **Phạm vi:** Cấu hình phần cứng, FreeRTOS nền và quy trình generate code. Các module RTSFR và hook kernel không được CubeMX tự sinh.

---

## 1. Mục tiêu của cấu hình CubeMX

CubeMX chỉ chịu trách nhiệm tạo nền tảng phần cứng và FreeRTOS gồm:

- Clock hệ thống 72 MHz từ HSE 8 MHz.
- SWD để nạp và debug.
- TIM4 làm HAL timebase, nhường SysTick cho FreeRTOS.
- USART1 để nhận lệnh test và xuất trace.
- GPIO PC13 làm LED trạng thái.
- FreeRTOS/CMSIS-RTOS2 với dynamic allocation, `heap_4`, mutex và task deletion.
- IWDG ở giai đoạn kiểm thử watchdog cuối dự án.

CubeMX **không** chịu trách nhiệm tạo:

- Task registry, fault event hoặc health contract.
- Policy RESTART/SAFE_MODE/RESET.
- Resource ownership tracker.
- Recovery state machine.
- Trace ring buffer và invariant checker.
- Các hook tùy biến trong `tasks.c` và `queue.c`.

Các thành phần trên phải được nhóm quản lý trong mã nguồn riêng và thêm thủ công vào Keil project.

---

## 2. Chiến lược cấu hình theo giai đoạn

Không bật tất cả tính năng ngay từ đầu. Nên chia thành hai cấu hình vận hành:

### 2.1. Giai đoạn G0–G3: phát triển và debug

- HSE, SWD, TIM4, USART1, PC13 và FreeRTOS: bật.
- IWDG: **chưa bật**.
- Fault injection: bật bằng build flag khi cần.
- UART log: bật, nhưng không transmit blocking trong kernel hook.
- Build cấu hình: `DEBUG_TEST` hoặc `RELEASE_DEMO` chưa có IWDG.

### 2.2. Giai đoạn G4 và release watchdog

- Bật IWDG với timeout danh định khoảng 4 giây.
- Chỉ một health gate của RTSFR được refresh IWDG.
- Bật đọc reset cause khi MCU khởi động.
- Chạy TC12 trên bo thật.
- Build cấu hình: `RELEASE_DEMO` hoặc `BENCHMARK_IWDG`.

Lý do tách hai giai đoạn: IWDG bắt đầu đếm ngay khi được khởi tạo và không thể dừng bằng phần mềm thông thường. Nếu bật quá sớm, MCU có thể reset liên tục khi dừng tại breakpoint.

---

## 3. Chọn MCU và project

Trong CubeMX:

1. Mở file `Embedded_System_Project.ioc`.
2. Kiểm tra MCU là `STM32F103C8Tx`, package `LQFP48`.
3. Trong **Project Manager**:
   - Project Name: `Embedded_System_Project`.
   - Toolchain/IDE: `MDK-ARM V5.32`.
   - Firmware package: `STM32Cube FW_F1 V1.8.7` trong baseline hiện tại.
   - Không đồng ý tự động migrate firmware package sau khi đã tạo tag baseline.

### Trạng thái hiện tại

- MCU `STM32F103C8T6`: **đã đúng**.
- Package `LQFP48`: **đã đúng**.
- Target toolchain `MDK-ARM V5.32`: **đã đúng**.
- File Keil đang dùng ARM Compiler 5/RVDS và port `portable/RVDS/ARM_CM3`: **đã đúng**.
- Trong `.ioc` còn metadata `ProjectManager.CompilerLinker=GCC`, trong khi target thật là MDK-ARM. Cần mở lại **Project Manager**, chọn lại `MDK-ARM V5.32`, lưu `.ioc` và kiểm tra metadata sau khi generate.

---

## 4. Cấu hình RCC, SYS và clock

### 4.1. RCC

Trong **Pinout & Configuration → System Core → RCC**:

| Tham số | Giá trị |
|---|---|
| High Speed Clock (HSE) | Crystal/Ceramic Resonator |
| Low Speed Clock (LSE) | Disable nếu không có crystal 32.768 kHz |
| LSI | CubeMX sẽ dùng khi bật IWDG |

Blue Pill thường dùng crystal HSE 8 MHz. Không chọn HSE Bypass trừ khi board nhận clock ngoài thay vì crystal.

### 4.2. SYS

Trong **System Core → SYS**:

| Tham số | Giá trị |
|---|---|
| Debug | Serial Wire |
| Timebase Source | TIM4 |

Không dùng SysTick làm HAL timebase vì FreeRTOS dùng SysTick cho kernel tick.

### 4.3. Clock Configuration

Thiết lập:

| Clock | Giá trị |
|---|---:|
| HSE | 8 MHz |
| PLL source | HSE |
| HSE predivider | /1 |
| PLL multiplier | ×9 |
| SYSCLK | 72 MHz |
| AHB/HCLK | 72 MHz |
| APB1 | 36 MHz |
| APB1 timer clock | 72 MHz |
| APB2 | 72 MHz |

### Trạng thái hiện tại

- HSE crystal: **đã cấu hình**.
- PLL HSE ×9: **đã cấu hình**.
- SYSCLK/HCLK 72 MHz: **đã cấu hình**.
- APB1 36 MHz, APB2 72 MHz: **đã cấu hình**.
- Serial Wire: **đã cấu hình**.
- HAL timebase TIM4: **đã cấu hình**.

Không cần thay đổi phần clock ở thời điểm hiện tại.

---

## 5. Cấu hình USART1 cho log và lệnh fault injection

Trong **Connectivity → USART1**:

1. Chọn mode `Asynchronous`.
2. CubeMX gán:
   - `PA9` → `USART1_TX`.
   - `PA10` → `USART1_RX`.
3. Parameter Settings:

| Tham số | Giá trị |
|---|---|
| Baud rate | 115200 bit/s |
| Word length | 8 bits |
| Parity | None |
| Stop bits | 1 |
| Hardware flow control | None |
| Oversampling | 16 |

4. NVIC Settings:
   - Enable `USART1 global interrupt`.
   - Preemption priority: `6`.
   - Subpriority: `0`.

5. DMA Settings:
   - Chưa cần DMA trong MVP.
   - Có thể bổ sung UART TX DMA sau khi V2 ổn định nếu UART drain task gây tải CPU đáng kể.

### Quy tắc sử dụng

- ISR RX chỉ nhận byte/lệnh ngắn và chuyển dữ liệu qua API `...FromISR()` hoặc ring buffer ISR-safe.
- Không gọi `printf()` hoặc `HAL_UART_Transmit()` trong `tasks.c`, `queue.c`, ISR hoặc critical section.
- Trace event được ghi vào ring buffer; một task thấp ưu tiên chịu trách nhiệm xuất UART.

### Trạng thái hiện tại

- USART1: **chưa được bật trong `.ioc`**.
- Chưa có PA9/PA10 trong pinout.
- `HAL_UART_MODULE_ENABLED`: **đang tắt**.
- Chưa có `MX_USART1_UART_Init()`.
- Chưa có `stm32f1xx_hal_uart.c` trong Keil project.
- Chưa có UART IRQ phục vụ lệnh test.

**Kết luận:** Đây là cấu hình bắt buộc phải bổ sung ngay để hoàn thành G0.

---

## 6. Cấu hình LED PC13 và output SAFE_MODE

Chọn `PC13 → GPIO_Output` và đặt User Label `STATUS_LED`.

GPIO Settings:

| Tham số | Giá trị |
|---|---|
| GPIO output level | High |
| GPIO mode | Output Push Pull |
| Pull-up/Pull-down | No pull |
| Maximum output speed | Low |
| User label | STATUS_LED |

Trên nhiều board Blue Pill, LED PC13 active-low:

```c
HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin, GPIO_PIN_RESET); /* LED ON */
HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin, GPIO_PIN_SET);   /* LED OFF */
```

Mẫu trạng thái đề xuất:

| Trạng thái | LED |
|---|---|
| HEALTHY | Nháy chậm |
| RECOVERY | Hai nháy ngắn |
| SAFE_MODE | Sáng liên tục hoặc nháy nhanh |
| Boot sau IWDG reset | Ba nháy ngắn |

LED chỉ là tín hiệu trình diễn. Nếu `ControlTask` đại diện cho một actuator, cần thêm một GPIO riêng làm `CONTROL_OUTPUT` và định nghĩa mức an toàn cụ thể khi vào SAFE_MODE.

### Trạng thái hiện tại

- PC13 chưa xuất hiện trong `.ioc`.
- `MX_GPIO_Init()` hiện chỉ bật clock GPIOA và GPIOD, chưa cấu hình output.

**Kết luận:** Cần bổ sung PC13 trước khi tạo demo task.

---

## 7. Cấu hình FreeRTOS

Trong **Middleware → FreeRTOS** chọn interface `CMSIS_V2`.

CMSIS-RTOS2 được giữ để tương thích project CubeMX, nhưng RTSFR nội bộ sử dụng FreeRTOS native API. Không để CMSIS và M4 đồng thời sở hữu vòng đời của cùng một monitored task.

### 7.1. Kernel settings

| Tham số | Giá trị mục tiêu | Ghi chú |
|---|---:|---|
| `configUSE_PREEMPTION` | 1 | Bắt buộc |
| `configTICK_RATE_HZ` | 1000 | Tick 1 ms |
| `configMAX_PRIORITIES` | 56 | CMSIS-RTOS2 hiện tại yêu cầu đúng 56 |
| `configMINIMAL_STACK_SIZE` | 128 words | Cortex-M3: 512 byte |
| `configTOTAL_HEAP_SIZE` | 8192 byte ban đầu | Điều chỉnh sau khi có linker map/high-water mark |
| `configUSE_16_BIT_TICKS` | 0 | Dùng tick 32 bit |
| `configUSE_TRACE_FACILITY` | 1 | Cần cho trace/quan sát task |
| `configUSE_MUTEXES` | 1 | Bắt buộc cho F06 |
| `configUSE_RECURSIVE_MUTEXES` | 1 | Giữ để test đúng đường recursive mutex |
| `configUSE_COUNTING_SEMAPHORES` | 1 | Có thể dùng cho test/harness |
| `configQUEUE_REGISTRY_SIZE` | 8 | Đủ cho demo ban đầu |
| `configUSE_PORT_OPTIMISED_TASK_SELECTION` | 0 | CMSIS-V2 dùng 56 priority |

### 7.2. Memory allocation

| Tham số | Giá trị |
|---|---:|
| Static allocation support | Enabled |
| Dynamic allocation support | Enabled |
| Heap implementation | `heap_4.c` |

M4 baseline dùng `xTaskCreate()` và dynamic allocation. Static allocation chỉ dùng khi đã chứng minh rõ lifetime của TCB/stack qua delete/recreate.

Heap 8192 byte là giá trị khởi đầu, không phải kết quả tối ưu cuối. Sau khi chạy project phải ghi:

```c
xPortGetFreeHeapSize();
xPortGetMinimumEverFreeHeapSize();
uxTaskGetStackHighWaterMark(task_handle);
```

Không tăng heap đến sát 20 KB SRAM mà không kiểm tra `.data`, `.bss`, main stack và trace buffer trong linker map.

### 7.3. Software timer

Giữ cấu hình hiện tại:

| Tham số | Giá trị |
|---|---:|
| `configUSE_TIMERS` | 1 |
| Timer task priority | 2 |
| Timer queue length | 10 |
| Timer task stack | 256 words |

CMSIS-RTOS2 wrapper hiện tại có các API timer dựa trên FreeRTOS software timer. Chỉ tắt timer nếu đã tắt các CMSIS timer API tương ứng và build xác nhận thành công.

### 7.4. API include parameters

Bật các mục sau:

```c
#define INCLUDE_vTaskPrioritySet             1
#define INCLUDE_uxTaskPriorityGet            1
#define INCLUDE_vTaskDelete                  1
#define INCLUDE_vTaskSuspend                 1
#define INCLUDE_vTaskDelayUntil              1
#define INCLUDE_vTaskDelay                   1
#define INCLUDE_xTaskGetSchedulerState       1
#define INCLUDE_xTimerPendFunctionCall       1
#define INCLUDE_xQueueGetMutexHolder         1
#define INCLUDE_uxTaskGetStackHighWaterMark  1
#define INCLUDE_xTaskGetCurrentTaskHandle    1
#define INCLUDE_eTaskGetState                1
```

### 7.5. Debug safety settings

Nếu CubeMX có trường tương ứng, cấu hình:

```c
#define configCHECK_FOR_STACK_OVERFLOW 2
#define configUSE_MALLOC_FAILED_HOOK   1
```

Nếu CubeMX không hiển thị, thêm trong vùng `USER CODE BEGIN Defines` của `FreeRTOSConfig.h` để giảm nguy cơ bị ghi đè.

Cài hai hook trong vùng USER CODE của application:

```c
void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void) task;
    (void) task_name;
    taskDISABLE_INTERRUPTS();
    for (;;) {
    }
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    for (;;) {
    }
}
```

Trong giai đoạn hoàn thiện, hai hook cần ghi fault/reset reason tối thiểu nếu trạng thái còn tin cậy; không hứa selective recovery cho stack overflow hoặc kernel heap corruption.

### Trạng thái hiện tại

| Mục | Hiện tại | Cần làm |
|---|---:|---|
| CMSIS-V2 | Đã bật | Giữ nguyên |
| Preemption | Đã bật | Giữ nguyên |
| Tick rate 1000 Hz | Đã đúng | Giữ nguyên |
| Max priorities 56 | Đã đúng | Giữ nguyên |
| `heap_4` | Đã đúng | Giữ nguyên |
| Static + dynamic allocation | Đã bật | Giữ nguyên |
| Heap size | 3072 byte | Tăng tạm lên 8192 byte và đo lại |
| Trace facility | Đã bật | Giữ nguyên |
| Mutex/recursive mutex | Đã bật | Giữ nguyên |
| Task deletion/suspend/state APIs | Đã bật | Giữ nguyên |
| Stack overflow check | Chưa cấu hình | Bổ sung mức 2 |
| Malloc failed hook | Chưa cấu hình | Bổ sung |
| RTSFR config macros | Chưa có | Thêm thủ công sau khi khóa interface |

---

## 8. Cấu hình task trong CubeMX

### 8.1. Chỉ tạo BootstrapTask bằng CubeMX

Đổi `defaultTask` thành:

| Thuộc tính | Giá trị |
|---|---|
| Task name | `bootstrapTask` |
| Entry function | `StartBootstrapTask` |
| Priority | Normal (`24`) |
| Stack size | 128 words |
| Allocation | Dynamic |

BootstrapTask thực hiện:

1. Khởi tạo board-level logger và trace.
2. Gọi `rtsfr_init()`.
3. Tạo ba task demo qua task descriptor chung.
4. Tạo `RTSFR_Supervisor` sau khi registry đã sẵn sàng.
5. Tự xóa bằng `vTaskDelete(NULL)`.

Không tạo ba monitored task bằng CubeMX vì M4 phải có toàn quyền tái tạo chúng từ descriptor bất biến. Điều này cũng giúp V0, V1 và V2 dùng cùng workload và stack/priority.

### 8.2. Priority đề xuất ban đầu

Vì CMSIS-RTOS2 dùng thang 56 priority, có thể dùng:

| Task | Priority FreeRTOS/CMSIS | Stack khởi đầu |
|---|---:|---:|
| Trace logger | 8 (`Low`) | 192 words |
| SensorTask | 16 (`BelowNormal`) | 128 words |
| CommTask | 24 (`Normal`) | 192 words |
| ControlTask | 32 (`AboveNormal`) | 192 words |
| RTSFR_Supervisor | 40 (`High`) | 256 words |

Các giá trị stack phải được điều chỉnh theo high-water mark. Supervisor phải cao hơn mọi monitored task nhưng luôn block giữa các lần scan.

### Trạng thái hiện tại

- Chỉ có `defaultTask`, priority 24, stack 128 words.
- Task hiện tại chỉ gọi `osDelay(1)` và chưa khởi tạo RTSFR.
- Chưa có SensorTask, CommTask, ControlTask, Supervisor hoặc Trace logger.

**Kết luận:** Đổi `defaultTask` thành BootstrapTask và tạo các task còn lại trong mã nguồn do nhóm quản lý.

---

## 9. Cấu hình NVIC

Giữ Priority Group là `NVIC_PRIORITYGROUP_4`.

| Interrupt | Preemption priority | Subpriority | Ghi chú |
|---|---:|---:|---|
| PendSV | 15 | 0 | FreeRTOS context switch |
| SysTick | 15 | 0 | FreeRTOS tick |
| TIM4 | 15 | 0 | HAL timebase |
| USART1 | 6 | 0 | Có thể gọi API `...FromISR()` |

Với `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5`, ISR gọi FreeRTOS API phải có priority số học từ 5 trở lên. Chọn 6 cho USART1 để tránh nằm đúng boundary.

Không thực hiện detection, policy hoặc recovery trong ISR.

### Trạng thái hiện tại

- Priority Group 4: **đã đúng**.
- PendSV, SysTick và TIM4 priority 15: **đã đúng**.
- USART1 IRQ: **chưa có**, cần bổ sung cùng USART1.

---

## 10. Cấu hình IWDG ở giai đoạn G4

Trong **System Core → IWDG** bật `Activated`.

Giá trị khởi đầu:

| Tham số | Giá trị |
|---|---:|
| Prescaler | 64 |
| Reload | 2499 |
| Window | Không dùng/window disable nếu giao diện cho phép |

Timeout danh định với LSI 40 kHz:

```text
T = (Reload + 1) × Prescaler / f_LSI
  = 2500 × 64 / 40000
  ≈ 4 giây
```

LSI có sai số đáng kể nên phải đo thời gian reset thực trên bo. Timeout phải lớn hơn tổng worst-case detection, quiesce, cleanup, recreate và verification cùng safety margin.

### Quy tắc phần mềm

- Không refresh IWDG trong ISR định kỳ.
- Không tạo một task chuyên refresh vô điều kiện.
- Chỉ health gate của RTSFR được refresh khi supervisor và các invariant cần thiết còn hợp lệ.
- Khi SAFE_MODE đã đưa output về trạng thái an toàn, policy mới quyết định tiếp tục refresh hay để reset.
- Khi boot, đọc `RCC_CSR`/HAL reset flags trước khi xóa flag để xác định IWDG reset.

### Trạng thái hiện tại

- IWDG chưa bật trong `.ioc`: đúng cho giai đoạn debug hiện tại.
- `HAL_IWDG_MODULE_ENABLED` đang tắt.
- Chưa có HAL IWDG driver hoặc `MX_IWDG_Init()`.
- Chưa có code đọc reset cause.

**Kết luận:** Chưa cần bật ngay; phải bổ sung trước TC12/G4.

---

## 11. Code Generator và bảo vệ thay đổi kernel

Trong **Project Manager → Code Generator**:

- Bật `Keep User Code when re-generating`.
- Bật backup file đã generate trước khi generate lại nếu CubeMX cung cấp tùy chọn.
- Chọn copy các thư viện cần thiết vào project.
- Không tự động migrate firmware package.
- Generate project cho MDK-ARM.

### Cảnh báo quan trọng

`Keep User Code` chỉ bảo vệ các vùng `USER CODE BEGIN/END`. Nó không bảo vệ patch tùy biến đặt trực tiếp trong `tasks.c` hoặc `queue.c`.

Trước khi thêm RTSFR kernel hooks phải:

1. Khóa FreeRTOS version/tag/commit.
2. Tạo baseline Git tag `v0-baseline`.
3. Chọn một nguồn kernel chính thức duy nhất.
4. Tốt nhất fork/copy kernel ra vùng nhóm kiểm soát và cập nhật Keil project.
5. Không để CubeMX middleware và kernel fork cùng được build.
6. Sau khi patch kernel, không generate đè middleware hoặc phải audit diff ngay sau mỗi lần generate.

---

## 12. File và group phải kiểm tra sau Generate Code

### 12.1. Phần CubeMX phải sinh

Sau khi bật USART1:

```text
Core/Inc/main.h                         # pin label STATUS_LED
Core/Src/main.c                         # MX_USART1_UART_Init, GPIO init
Core/Src/stm32f1xx_hal_msp.c            # USART1 GPIO/clock
Core/Src/stm32f1xx_it.c                 # USART1_IRQHandler
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_uart.c
```

Sau khi bật IWDG ở G4:

```text
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_iwdg.c
MX_IWDG_Init()
```

### 12.2. Group thêm thủ công vào Keil

```text
RTSFR
├── rtsfr_detect.c
├── rtsfr_policy.c
├── rtsfr_resource.c
├── rtsfr_recovery.c
├── rtsfr_verify.c
└── rtsfr_trace.c

Application
├── demo_tasks.c
└── fault_scenarios.c

Board
├── board_uart.c
└── board_iwdg.c
```

Include paths cần bổ sung theo cấu trúc nhóm chọn, ví dụ:

```text
../App/Inc
../RTSFR/Include
../Board/Inc
```

---

## 13. Bảng so sánh tổng hợp với project hiện tại

| Hạng mục | Mục tiêu RTSFR | Project hiện tại | Trạng thái | Hành động |
|---|---|---|---|---|
| MCU | STM32F103C8T6 | STM32F103C8T6 | Đạt | Không đổi |
| HSE | Crystal 8 MHz | Đã bật | Đạt | Không đổi |
| SYSCLK | 72 MHz | 72 MHz | Đạt | Không đổi |
| HAL timebase | TIM4 | TIM4 | Đạt | Không đổi |
| Debug | Serial Wire | Serial Wire | Đạt | Không đổi |
| Toolchain | MDK-ARM V5.32/RVDS | Keil target đúng, `.ioc` còn metadata GCC | Cần xác nhận | Chọn lại MDK-ARM và save `.ioc` |
| FreeRTOS interface | CMSIS-V2 + native API nội bộ | CMSIS-V2 | Đạt một phần | Giữ CMSIS, thống nhất native API cho RTSFR |
| FreeRTOS version | Một version + SHA cố định | V10.3.1, chưa có SHA/tag | Thiếu | Khóa version và ghi tài liệu |
| Tick | 1000 Hz | 1000 Hz | Đạt | Không đổi |
| Heap | heap_4, đủ cho 4–5 task | heap_4, 3072 byte | Chưa đạt | Tăng tạm 8192 byte, đo lại |
| Dynamic allocation | Bật | Đã bật | Đạt | Không đổi |
| Static allocation | Bật tùy baseline | Đã bật | Đạt | Không đổi |
| Trace facility | Bật | Đã bật | Đạt | Không đổi |
| Mutex | Bật | Đã bật | Đạt | Không đổi |
| Task delete/suspend | Bật | Đã bật | Đạt | Không đổi |
| Stack overflow check | Mức 2 | Chưa có | Thiếu | Bổ sung |
| Malloc failed hook | Bật | Chưa có | Thiếu | Bổ sung |
| USART1 | PA9/PA10, 115200 8N1 | Chưa cấu hình | Thiếu bắt buộc | Bật ngay |
| USART1 IRQ | Priority 6 | Chưa có | Thiếu | Bật ngay |
| LED status | PC13 output | Chưa cấu hình | Thiếu | Bật ngay |
| IWDG | Khoảng 4 s, health gate | Chưa cấu hình | Chưa cần ở G0 | Bật tại G4 |
| Demo tasks | Sensor/Comm/Control | Chỉ defaultTask | Thiếu | Tạo qua descriptor |
| Supervisor | Priority cao hơn monitored tasks | Chưa có | Thiếu | Tạo trong RTSFR init |
| UART trace ring | Non-blocking producer | Chưa có | Thiếu | M5 triển khai |
| Kernel hooks | `tasks.c`, `queue.c` | Chưa có | Thiếu | Chỉ làm sau khi khóa kernel |
| Kernel source control | Một kernel fork chính thức | Kernel trong CubeMX Middlewares | Rủi ro | Fork/freeze trước khi patch |

---

## 14. Thứ tự thao tác CubeMX đề xuất ngay bây giờ

Thực hiện theo đúng thứ tự sau:

1. Sao lưu project hoặc tạo commit Git trước khi generate.
2. Mở `.ioc`, xác nhận target `MDK-ARM V5.32`.
3. Bật PC13 `STATUS_LED`.
4. Bật USART1 PA9/PA10, 115200 8N1.
5. Bật USART1 IRQ priority 6.
6. Giữ nguyên RCC, clock, SYS timebase TIM4 và SWD.
7. Trong FreeRTOS tăng heap từ 3072 lên 8192 byte.
8. Đổi `defaultTask` thành `bootstrapTask`.
9. Bật stack-overflow checking và malloc-failed hook nếu UI hỗ trợ.
10. Chưa bật IWDG.
11. Generate Code cho Keil MDK-ARM.
12. Build ngay khi chưa thêm RTSFR để xác nhận CubeMX baseline.
13. Flash bo và kiểm tra UART + LED.
14. Lưu linker map và heap/stack baseline.
15. Tạo tag `v0-baseline`.
16. Khóa/fork kernel rồi mới bắt đầu sửa `tasks.c` và `queue.c`.

---

## 15. Checklist nghiệm thu cấu hình G0

### CubeMX và build

- [ ] MCU đúng STM32F103C8T6.
- [ ] Clock hệ thống đo/xác nhận 72 MHz.
- [ ] SysTick dành cho FreeRTOS, HAL tick chạy bằng TIM4.
- [ ] Keil build không error và không có warning nghiêm trọng.
- [ ] Linker map được lưu cùng build.
- [ ] Chỉ có một bản FreeRTOS kernel trong build.

### UART và GPIO

- [ ] USART1 truyền được log ở 115200 8N1.
- [ ] USART1 nhận được lệnh test đơn giản.
- [ ] UART IRQ priority là 6 hoặc giá trị hợp lệ tương đương.
- [ ] PC13 LED hoạt động đúng active-low của board thực.
- [ ] SAFE_MODE có output được định nghĩa rõ.

### FreeRTOS

- [ ] BootstrapTask khởi động được.
- [ ] SensorTask, CommTask và ControlTask chạy đồng thời.
- [ ] Idle Task vẫn có thời gian chạy.
- [ ] Không có stack overflow trong bài test bình thường.
- [ ] `xPortGetMinimumEverFreeHeapSize()` còn safety margin.
- [ ] High-water mark của từng task được ghi lại.
- [ ] Heap, priority và stack được ghi trong tài liệu build environment.

### Trước khi sửa kernel

- [ ] FreeRTOS version/tag/SHA đã khóa.
- [ ] Toolchain và optimization level đã khóa.
- [ ] `v0-baseline` đã build/flash/test.
- [ ] Đã có Git tag hoặc commit baseline.
- [ ] Đã xác định cách ngăn CubeMX ghi đè kernel patch.

---

## 16. Kết luận hiện trạng

Project hiện tại đã có nền MCU, clock, SysTick/TIM4, FreeRTOS/CMSIS-V2, mutex, trace facility, dynamic allocation và Keil RVDS port phù hợp. Các phần nền này không cần làm lại.

Những cấu hình CubeMX còn thiếu và cần làm ngay để đạt G0:

1. USART1 PA9/PA10, 115200 8N1 và USART1 IRQ.
2. PC13 làm LED trạng thái.
3. Tăng FreeRTOS heap tạm thời từ 3072 lên 8192 byte.
4. Đổi defaultTask thành BootstrapTask.
5. Bật stack overflow checking và malloc failed hook.
6. Chọn/lưu lại đúng metadata toolchain MDK-ARM trong `.ioc`.

IWDG chưa phải việc làm ngay. Chỉ bật sau khi detection và recovery V1 đã chạy ổn định, trước khi triển khai TC12 và Gate G4.

