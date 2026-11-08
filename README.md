# Operating_System — RTSFR

Bài tập lớn Hệ điều hành 2026.1, FreeRTOS trên STM32F103. Phần M5 (Thủy): fault injection, trace, verification, watchdog và công cụ kiểm thử.

## Cấu trúc thư mục

Mã nhúng nằm trong [firmware/](firmware/README.md), gồm Core, Drivers, Middlewares, RTSFR, App, board, test target và project Keil/CubeMX. `tests/host/`, `tools/`, `docs/` và `results/` giữ ở root repo. File sinh tự động nằm trong `build/` và bị bỏ qua bởi Git.

Xem [cây thư mục và hướng dẫn chuyển cấu trúc](docs/repository_layout.md). Mở Keil tại `firmware/MDK-ARM/Operating_System_Project.uvprojx`, CubeMX tại `firmware/Operating_System_Project.ioc`.

## Bắt đầu

- [Tổng quan phần M5](docs/m5_overview.md)
- [Hướng dẫn build/demo](docs/demo_guide.md)
- [Giao diện kết nối M1–M4](docs/m5_interfaces.md)
- [Bộ test TC01–TC14](docs/test_plan.md)
- [Quy trình đo V0/V1/V2](docs/experiment_protocol.md)
- [Thông tin còn thiếu](docs/missing_information.json)
- [Build và host validation](results/summary/build_validation.md)
- [Kết quả target](results/summary/test_results.csv)

Mặc định firmware mang nhãn **M5_ONLY**. M1–M3 chưa có implementation trong snapshot repo ban đầu; không có provider thật thì M5 trả NOT_READY. Kết quả STM32 và benchmark V0/V1/V2 hiện **NOT_RUN**. Các file fixtures là dữ liệu tự tạo cho unit test.

```powershell
python tools/check_layout.py
python tools/run_host_tests.py
python tools/build_firmware.py --profile M5_DEBUG_TEST --compiler-dir "<KEIL_ARMCLANG_BIN>"
```

Target Keil mới: M5_DEBUG_TEST (injection bật), M5_RELEASE (injection tắt), M5_BENCHMARK (injection và cycle instrumentation bật). Không tự bật IWDG khi cấu hình chưa được xác nhận. Xem hướng dẫn trước khi flash board; gói này không tự flash, commit hay push.
