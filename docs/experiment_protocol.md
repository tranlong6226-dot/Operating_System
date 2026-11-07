# Quy trình thí nghiệm

## Trước khi đo

Điền thông tin còn thiếu trong `missing_information.json`: chip/board thực, compiler/optimization, clock/tick đã xác nhận, FreeRTOS source hash/commit, UART port, LSI hoặc biên tần số dùng thiết kế timeout, version/tag firmware và cấu hình log.

Repo hiện tại dùng FreeRTOS V10.3.1. Không cập nhật version chỉ để khớp gợi ý trong đặc tả. Gói M5 không có firmware V0/V1/V2 đã hoàn thiện; mặc định nhãn CSV là M5_ONLY.

## Ba phiên bản đối chứng

- V0: upstream baseline + cùng workload/injection/instrumentation, không selective recovery.
- V1: phát hiện + restart task không giữ resource.
- V2: resource-aware policy/cleanup/verification + watchdog.

Tú và các owners tạo các phiên bản thật. Chỉ đổi `RTSFR_M5_VERSION` sau khi xác nhận đúng chức năng. Giữ cùng compiler, flags, stack, priorities, clock và cấu hình đo tương đương. Không dùng kích thước M5_ONLY để công bố overhead V2 so với V0.

## Thu log

1. Flash đúng firmware bằng Keil/ST-LINK; ghi build SHA, board và config.
2. Dùng UART1 115200 8N1 theo source hiện tại; xác nhận PA9/PA10 và GND theo board thực.
3. Cài `tools/requirements.txt` trong môi trường Python riêng nếu cần.
4. Chạy `python tools/collect_log.py --port <COM_THAT> --run-id <ID_MOI> --seconds 30 --command "test 2"`.
5. Giữ `.uart.txt`, CSV và `.metadata.json`. Script không ghi đè run ID có sẵn.
6. Kết thúc thu phải có status counters. Malformed lines, RX/TX loss hoặc trace dropped khiến run cần review; không dùng run mất bằng chứng để PASS.
7. Chạy ít nhất 30 lần cho mỗi scenario định lượng sau khi workload/config đã khóa. Đây là kế hoạch; số lần thực tế hiện trống.

## Định nghĩa latency

Detection = FAULT_DETECTED.tick - INJECT.tick (cùng run/version/task/event/generation).

Recovery = FIRST_PROGRESS.tick của generation mới - FAULT_DETECTED.tick (cùng event; có RECOVERY_OK phù hợp).

Script giữ blank nếu thiếu injection, actual first progress, event correlation hoặc có duplicate không phân giải. Không dùng thời điểm arm làm thời điểm injection: lỗi bắt đầu tại checkpoint. Tick wrap hỗ trợ khoảng dưới 2^31 tick.

`python tools/summarize_results.py results/raw/<RUN>.csv --out results/summary/<EXPERIMENT>` xuất n/median/p95 (nearest rank). Không truyền dữ liệu fixtures như thí nghiệm thật. Tick Hz chưa xác nhận thì giữ đơn vị ticks.

## CPU và cycles

M5_BENCHMARK bật RTSFR_M5_MEASURE_TRACE. Trường cycles đo từ entry trace task API đến sau copy record trong critical section. Bao gồm thời gian bị preempt nếu có, không bao gồm exit critical, UART hay cả detection/recovery pipeline. FromISR hiện không tự đo cycles. Giá trị 0 nghĩa là không đo được/chưa bật counter, không phải overhead bằng 0.

DWT init kiểm tra counter tiến triển; status reason=4 ghi 1 nếu có. Ở 72 MHz, một vòng CYCCNT khoảng 59,65 s; chỉ dùng subtraction cho phép đo ngắn hơn một vòng. Ghi clock thật; debug halt có thể ảnh hưởng. Không reset counter giữa một phép đo.

Để báo CPU overhead toàn hệ thống, cần timer/cycle measurement cùng cửa sổ/workload và instrumentation tương đương ở V0/V1/V2. Chưa có số liệu này. UART blocking chỉ ở logger task, nhưng vẫn ảnh hưởng lịch chạy; phải công bố mức log và không tính UART thành chi phí thuật toán kernel.

## RAM/Flash và heap

Compiler/linker map cho kích thước tĩnh + vùng heap/stack đã reserve; không phải RAM high-water thực tế. Dùng map cùng flags ở ba phiên bản. Bản CLI dùng -Oz và scatter giới hạn Flash 0x10000, SRAM 0x5000.

TC11 cần heap_4 thật: idle được chạy/cleanup hoàn tất, so free heap trước/sau ít nhất 100 recovery cycles và kiểm tra xu hướng. Mock create/delete trên host không chứng minh điều này. Heap samples thực hiện trong target integration, lưu raw log; chưa có số đo.

## IWDG

Prescaler /256 và reload 624 chỉ là giá trị đề xuất (~4 s tại LSI danh định 40 kHz). Cần xác nhận biên LSI/board và worst-case recovery trước khi bật RTSFR_IWDG_CONFIG_CONFIRMED=1. Không ghi nhận 4 s như thời gian đo thật. Sau khi start, watchdog không dừng được bằng việc đổi biến phần mềm; dùng reset/reflash phù hợp.

Nguồn: [ST RM0008](https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf), [FreeRTOS critical sections from ISR](https://www.freertos.org/Documentation/02-Kernel/04-API-references/04-RTOS-kernel-control/02-taskENTER_CRITICAL_FROM_ISR_taskEXIT_CRITICAL_FROM_ISR).
