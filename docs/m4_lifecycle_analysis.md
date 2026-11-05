# M4 — Phân tích vòng đời task FreeRTOS 10.3.1

## 1. Phạm vi đã kiểm tra

Phân tích này áp dụng trực tiếp cho source đang được build trong project:

- `Middlewares/Third_Party/FreeRTOS/Source/tasks.c`
- FreeRTOS Kernel V10.3.1
- `heap_4.c`, dynamic allocation được bật
- STM32F103C8, ArmClang 6.24
- M4 chạy trong task supervisor, không chạy từ ISR hay hook xóa task

M4 chỉ tự động phục hồi **task khác**. Supervisor không được yêu cầu tự xóa hoặc tự phục hồi chính nó.

## 2. Create task

`xTaskCreate()` cấp phát hai vùng động: stack và TCB. Sau khi khởi tạo context, kernel đưa TCB mới vào ready list. Vì vậy descriptor phục hồi phải giữ dữ liệu bất biến sau, không giữ con trỏ vào stack/TCB cũ:

- entry function;
- argument có lifetime dài hơn mọi lần restart;
- stack depth;
- priority;
- task name;
- stable `task_id`;
- generation hiện hành;
- timeout chờ first progress;
- callback reset context sạch.

Nếu `xTaskCreate()` trả khác `pdPASS`, M4 không còn instance hợp lệ để chạy tiếp và phải latch SAFE_MODE với reason `RTSFR_REASON_CREATE_FAILED`.

## 3. Suspend/quiesce

`vTaskSuspend(handle)` loại task đích khỏi ready/delayed list. Trong thiết kế M4:

1. xác minh `task_id`, `generation` và handle;
2. cấm handle trùng `xTaskGetCurrentTaskHandle()`;
3. suspend task đích;
4. chỉ sau đó mới lấy resource snapshot mới từ M3.

Quiesce chỉ bảo đảm task không tiếp tục được scheduler chọn. Nó không rollback phần cập nhật shared-state đang dang dở. Do đó mutex, tài nguyên unknown hoặc I/O in-flight luôn chặn restart.

Nếu M1 xác nhận báo nhầm trước CLEANUP/DELETE, `rtsfr_recovery_cancel_pending()` có thể rollback. M4 chỉ resume đúng handle và generation đã lưu khi nhận decision. Sau khi cleanup hoặc delete bắt đầu, yêu cầu cancel bị từ chối bằng `RTSFR_ERR_BUSY`.

## 4. Delete và lifetime TCB/stack

Trong phiên bản `tasks.c` hiện tại có hai đường xóa:

| Trường hợp | Xử lý của kernel | Hệ quả cho M4 |
|---|---|---|
| Task tự xóa chính nó | đưa vào `xTasksWaitingTermination`; Idle Task gọi `prvDeleteTCB()` sau | bộ nhớ chưa được thu hồi ngay |
| Một task xóa task khác | gọi `prvDeleteTCB()` ngay trong `vTaskDelete()` | TCB/stack động được giải phóng trước khi API trả về |

M4 dùng trường hợp thứ hai vì supervisor xóa task đích đã suspend và cấm self-recovery. Với `heap_4.c`, stack/TCB cũ được giải phóng ngay trước bước recreate; không cần delay tùy ý để “chờ Idle”.

Giới hạn quan trọng: kết luận trên chỉ đúng với source FreeRTOS đang khóa trong project. Nếu đổi kernel/version hoặc cho phép task tự xóa, phải audit lại đường `vTaskDelete()` và Idle cleanup.

## 5. Generation và stale handle

- `task_id` định danh vai trò task, không phải địa chỉ handle.
- `generation` bắt đầu từ giá trị khác 0 và tăng sau mỗi lần recreate thành công.
- Event có generation khác instance đang active bị trả `RTSFR_ERR_STALE`.
- Handle cũ được xóa khỏi descriptor ngay sau `vTaskDelete()`.
- M4 không ghi `RECOVERY_OK` chỉ vì `xTaskCreate()` thành công; instance mới phải báo first progress đúng generation.

## 6. OOM, heap và kiểm thử lặp

Baseline dùng cấp phát động. M4 không cấp phát trong hook/critical section của chính nó; cấp phát chỉ xảy ra ở control path qua `xTaskCreate()`.

TC11 cần đo `xPortGetFreeHeapSize()`:

1. trước fault;
2. sau mỗi recovery hoàn tất;
3. sau 100 chu kỳ;
4. trong cùng workload và logging configuration.

Không yêu cầu mọi mẫu heap phải bằng nhau từng byte khi các module khác còn cấp phát. Tiêu chí là không có xu hướng giảm tích lũy do M4 và không còn handle/generation cũ trong registry/resource tracker.

## 7. Kết luận thiết kế

Đường an toàn được khóa là:

`validate → suspend other task → re-snapshot → cleanup được chứng minh → delete → recreate → publish generation → first progress`

Bất kỳ điểm nào không chứng minh được an toàn sau quiesce đều đi SAFE_MODE. M4 không tự unlock mutex, không đoán tài nguyên, không tái dùng TCB/stack tĩnh.

