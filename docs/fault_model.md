# Fault model M5

| Fault | Tình huống | Cơ chế M5 | Kết quả hệ thống mong đợi |
|---|---|---|---|
| F01 | Không progress | inject 1 + work checkpoint | Restart nếu đủ điều kiện |
| F02 | Busy loop, supervisor/tick còn chạy | inject 2 | Restart nếu an toàn |
| F03 | Tự báo lỗi | inject 3 + callback M1 | Policy quyết định |
| F04 | Delay/block hợp lệ | inject 4 + expected-wait contract thật | Không báo nhầm |
| F05 | Giữ managed buffer độc quyền | inject 5 tại buffer checkpoint | Cleanup đúng rồi restart |
| F06 | Giữ mutex | inject 6 tại mutex checkpoint | SAFE_MODE, không unlock tự động |
| F07 | Lỗi lặp lại | target implementation/host lặp nhiều generation | Hết retry budget thì ngừng restart |
| F08 | Kernel/supervisor không chạy | target TC12; IWDG thật | Reset, reset cause IWDG ở boot sau |
| F09 | HardFault/stack/kernel corruption | Chưa có injection phá bộ nhớ | Reset fallback; không selective restart |
| F10 | Chờ mutex còn hợp lệ | target workload + wait contract | Không báo nhầm |

F01/F02 là triệu chứng trong fault model, không phải bằng chứng mọi task chậm đều hỏng. Timeout và expected-wait cần workload thật.

Injection mặc định chỉ bật ở M5_DEBUG_TEST/M5_BENCHMARK. M5_RELEASE trả NOT_READY, checkpoint không thay đổi luồng. Không tự phá con trỏ/stack/kernel RAM. Hỏng memory/kernel không thuộc cam kết phục hồi cục bộ.

`iwdg-reset` chỉ là phép thử primitive phần cứng thủ công, không đủ để gọi TC12 end-to-end PASS. TC12 còn yêu cầu health owner/supervisor integration thật, stall thật và reset cause sau reboot.
