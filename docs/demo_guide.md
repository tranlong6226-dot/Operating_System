# Hướng dẫn build và demo M5

## Build trên Windows

Mở terminal tại root repo. Project giữ target gốc và bổ sung M5_DEBUG_TEST, M5_RELEASE, M5_BENCHMARK.

```powershell
python tools/run_host_tests.py
python tools/build_firmware.py --profile M5_DEBUG_TEST --compiler-dir "C:\Users\thinkbook\AppData\Local\Keil_v5\ARM\ARMCLANG\bin"
python tools/build_firmware.py --profile M5_RELEASE --compiler-dir "C:\Users\thinkbook\AppData\Local\Keil_v5\ARM\ARMCLANG\bin"
python tools/build_firmware.py --profile M5_BENCHMARK --compiler-dir "C:\Users\thinkbook\AppData\Local\Keil_v5\ARM\ARMCLANG\bin"
```

Đổi compiler-dir theo máy; không giả định Keil đã cài trên mọi máy. Output trong build/<PROFILE>: firmware.axf, firmware.hex, firmware.map, build.log, build_summary.json. CLI không flash. Script dùng scratch ASCII nếu đường dẫn repo có tiếng Việt vì công cụ legacy có thể không hỗ trợ Unicode.

Có thể mở MDK-ARM/Operating_System_Project.uvprojx bằng Keil, chọn target tương ứng rồi Build/Rebuild. Build bằng UI cần device pack/CMSIS pack tương ứng project; CLI sử dụng headers đã commit. Flags CLI được công bố riêng; không coi hai đường build là cùng số đo nếu chưa đối chiếu.

Project clone có tham chiếu tests/target/test_m4.c nhưng không có file đó. Tham chiếu được giữ và đánh dấu exclude trong project; không tạo file M4 test giả. Các file M4 hiện có giữ nguyên nội dung. Test hợp đồng M4 trên host do M5 cung cấp ở tests/host/test_recovery_contract.c.

## Kết nối

- Board STM32F103C8T6 thực: CHƯA XÁC NHẬN.
- ST-LINK: CHƯA XÁC NHẬN kết nối.
- UART theo source: USART1, 115200, 8N1; xác nhận chân PA9 TX / PA10 RX.
- USB-UART: mức logic 3,3 V phù hợp, nối chung GND; COM port để trống tới khi kiểm tra máy thật.
- Không dùng UART1 đồng thời cho HAL RX IT/DMA khác vì M5 sở hữu RXNE interrupt.

## Lệnh

```text
help
status
run demo_001
verify
test 2
inject 1 2 1
disarm
watchdog-start
iwdg-reset
```

`inject SCENARIO TASK_ID GENERATION`: scenario 1 stop-progress, 2 busy-loop, 3 self-report, 4 expected-wait, 5 buffer-held, 6 mutex-held. Task ID/generation chỉ là tham số; dùng registry thật để biết giá trị. Không giả định CommTask có ID 2 trên mọi firmware.

Khi chưa bind M1/M3/test implementation: test/inject/verify trả NOT_READY. Đây là phản hồi đúng, không phải PASS. RELEASE từ chối mọi fault injection.

`watchdog-start` cần config confirmed và health evidence thật. `iwdg-reset` chỉ TEST + config confirmed: bắt đầu IWDG rồi tắt IRQ/vòng lặp vô hạn, chờ reset phần cứng. Chỉ chạy trên board demo đã chuẩn bị. Nó không tạo TC12 PASS tự động; đọc RESET_CAUSE ở lần boot sau, xác nhận IWDGRSTF và lưu transcript.

## Trình tự thuyết trình của Thủy

1. Giải thích M5 thu bằng chứng và test cả đường thất bại.
2. Demo host tests và BUILD_PASS, phân biệt với kết quả hardware.
3. Khi module owners tích hợp: demo TC02 task sạch, TC05 buffer, TC06 mutex từ chối restart, TC07 retry, TC12 watchdog.
4. Đưa raw log và bảng so sánh V0/V1/V2 thực. Nếu chưa đo, nói rõ NOT_RUN, không trình bày số mẫu như kết quả.

## Sửa sau CubeMX generate

Thay đổi main/IRQ/config nằm trong USER CODE sections hoặc có comment M5. Kiểm tra lại lifecycle macros, stack bootstrap 512 words khi bật M5, source/include path và nhóm target trong Keil. Không trộn nhiều nguồn FreeRTOS.
