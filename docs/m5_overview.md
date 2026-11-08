# M5 — Phần việc của Thủy

M5 cung cấp tạo lỗi có kiểm soát, trace, kiểm tra bất biến, watchdog và công cụ kiểm thử/đo kết quả. Đây là implementation M5 trong repo hiện tại, không phải tuyên bố toàn bộ RTSFR đã hoạt động trên board.

## Trạng thái bàn giao

- Có mã nguồn trace ring buffer, fault checkpoints, kiểm tra snapshot, health gate và driver IWDG.
- Có command UART, adapter cho M1–M4, target runner và hướng dẫn TC01–TC14.
- Có host tests dùng doubles của FreeRTOS và test với mã M4 thật.
- Các build và host test thực tế được ghi ở `results/summary/build_validation.md`.
- Kết quả phần cứng, benchmark V0/V1/V2 và reset IWDG thực: **NOT_RUN**.
- M1/M2/M3 chưa có trong snapshot repo được dùng để triển khai. Callback không được cung cấp sẽ trả `RTSFR_ERR_NOT_READY`.

## Đọc theo thứ tự

1. `firmware/App/fault_scenarios.c`: arm lỗi cho đúng task/generation; task gọi checkpoint tại vị trí phù hợp.
2. `firmware/RTSFR/Src/rtsfr_trace.c`: ghi sự kiện vào ring hữu hạn; không UART, malloc hay chờ trong hook.
3. `firmware/RTSFR/Src/rtsfr_verify.c`: kiểm tra snapshot nhất quán do module owners cung cấp.
4. `firmware/RTSFR/Src/rtsfr_health.c`: xác thực bằng chứng sức khỏe và supervisor identity.
5. `firmware/board/iwdg.c`: một owner được refresh sau health gate.
6. `firmware/App/m5_command.c`: nhận lệnh, điều phối và xuất log từ bootstrap task.
7. `firmware/tests/target/m5_test_runner.c`: chỉ phát PASS/FAIL khi implementation test thật báo hoàn tất.

## Luồng

`UART → command → fault arm → target checkpoint → M1/M2/M3/M4 → trace → UART → CSV → summary`.

M5 không thay scheduler, không tự thêm thuật toán phát hiện/policy/resource tracker của người khác. Lifecycle trace được gắn vào traceTASK_CREATE/DELETE của FreeRTOS trong `FreeRTOSConfig.h`. Đây là quan sát đường kernel; không thay thế phần kernel recovery M1–M4.

## Hai loại kết quả phải phân biệt

**HOST_PASS**: code chạy với RTOS doubles trên Windows. Test OOM ở đây là xTaskCreate double trả thất bại; không phải heap_4 thật cạn bộ nhớ. 100 vòng mocked recovery chỉ kiểm tra hợp đồng API/lifecycle, không chứng minh heap ổn định.

**BUILD_PASS**: compile/link thành công; không chứng minh firmware đã chạy trên STM32.

**TC PASS trên board**: cần workload thật, log/raw data, build/tag, expected/actual và đầy đủ điều kiện test. `results/summary/test_results.csv` hiện giữ NOT_RUN cho cả 14 ca.

## Thông tin còn thiếu

Xem `docs/missing_information.json`. Các trường chưa xác nhận dùng `null`; không tự điền COM port, LSI đo thực, thời gian watchdog an toàn hay kết quả V0/V1/V2.

## Phạm vi hoàn thành M5

Phần source/tooling độc lập đã triển khai. Nghiệm thu tích hợp cần owners nối callback, kiểm tra giao diện chung, xác nhận cấu hình IWDG và chạy test trên board. Không đổi nhãn firmware M5_ONLY sang V2 trước khi thực sự có đủ M1–M4 và kiểm thử.
