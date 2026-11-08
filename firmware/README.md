# Firmware STM32 / FreeRTOS

Toàn bộ source được nạp lên STM32 nằm trong thư mục này; không có bản sao kernel thứ hai ở root repo.

| Vị trí | Nội dung |
|---|---|
| Core/ | main, IRQ, HAL glue và FreeRTOSConfig |
| Drivers/ | STM32 HAL/CMSIS headers |
| Middlewares/ | FreeRTOS V10.3.1 hiện dùng |
| RTSFR/ | Mã mở rộng kernel / module RTSFR |
| App/ | Fault scenarios và UART command M5 |
| board/ | IWDG, UART, cycle counter |
| tests/target/ | Test runner/kịch bản chạy trên STM32 |
| MDK-ARM/ | Project Keil, startup, cấu hình debug dùng chung |
| Operating_System_Project.ioc | Cấu hình CubeMX, mở file tại đây |
| .mxproject | Metadata CubeMX, di chuyển cùng .ioc và source |

Chạy các script từ **root repo**:

```powershell
python tools/check_layout.py
python tools/run_host_tests.py
python tools/build_firmware.py --profile M5_DEBUG_TEST --compiler-dir "<KEIL_ARMCLANG_BIN>"
```

Đầu ra build mới nằm ở root `build/`, không nằm cạnh source. Các artifact Keil cũ và file giao diện cá nhân được giữ cục bộ trong `build/legacy_keil/`, không đưa vào commit tiếp theo. Xem [hướng dẫn chuyển cấu trúc](../docs/repository_layout.md).
