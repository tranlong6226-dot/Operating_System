# Build và host validation M5

Ngày ghi nhận: 08/11/2026 (Asia/Saigon). Nguồn trước thay đổi: repo Operating_System, HEAD quan sát e21042a, nhánh tran-thi-thanh-thuy.

## Firmware compile/link thật

Arm Compiler for Embedded 6.24 / MDK-ARM Lite 5.43; CLI build -Oz, Cortex-M3 Thumb, scatter 64 KiB Flash/20 KiB SRAM. Đây là M5_ONLY; không có kết quả chạy board.

| Profile | Kết quả | Flash (bytes) | RW + ZI / SRAM reserve (bytes) | Phần cứng |
|---|---|---:|---:|---|
| M5_DEBUG_TEST | BUILD_PASS | 17024 | 14584 | NOT_RUN |
| M5_RELEASE | BUILD_PASS | 16664 | 14560 | NOT_RUN |
| M5_BENCHMARK | BUILD_PASS | 17108 | 14584 | NOT_RUN |

Kích thước lấy từ linker map thật, gồm RAM reserve tĩnh/heap/stack. Không phải peak RAM đo khi chạy và không phải overhead V2−V0. Build GUI Keil chưa được chạy; project XML đã được dùng để chọn source/defines/include cho CLI.

Assembler upstream có cảnh báo A1950W (legacy armasm deprecated). Không có warning C từ mã M5 trong các build này. Build logs/maps/HEX/AXF ở build/<PROFILE>/ (gitignored).

## Host tests thực tế

Compiler: MinGW-w64 GCC 8.1.0, C99, -Wall -Wextra -Werror -pedantic. Python 3.13.

| Suite | Kết quả | Giới hạn |
|---|---|---|
| trace | HOST_PASS | FreeRTOS doubles / synthetic data; target NOT_RUN |
| verify | HOST_PASS | FreeRTOS doubles / synthetic data; target NOT_RUN |
| health_gate | HOST_PASS | FreeRTOS doubles / synthetic data; target NOT_RUN |
| recovery_contract | HOST_PASS | FreeRTOS doubles / synthetic data; target NOT_RUN |
| faults_test | HOST_PASS | FreeRTOS doubles / synthetic data; target NOT_RUN |
| faults_release | HOST_PASS | FreeRTOS doubles / synthetic data; target NOT_RUN |
| python_tools | HOST_PASS | FreeRTOS doubles / synthetic data; target NOT_RUN |

Python tool suite gồm 6 test: parser, correlation, missing evidence, stale generation, duplicate evidence, tick wrap/p95. M4 contract test dùng source M4 thật + RTOS doubles, gồm 100 vòng lifecycle mocked. Không có giả lập heap_4/scheduler/hardware reset.

## Chưa nghiệm thu

- TC01–TC14 trên board: NOT_RUN.
- Reset IWDG thực và reset cause sau boot: NOT_RUN.
- UART/COM/ST-LINK thực, clock/tick/LSI đo, safe output: chưa xác nhận.
- Benchmark V0/V1/V2, CPU overhead, false positive, continuity và heap trend thật: chưa có dữ liệu.
- M1/M2/M3 và callback target test: cần nhóm tích hợp; xem docs/m5_interfaces.md.
