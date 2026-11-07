# Giao diện tích hợp M5

## Khởi tạo

Bootstrap gọi `rtsfr_m5_service_init()` trước khi các module bắt đầu phát sự kiện. Sau đó kernel integration coordinator gọi `rtsfr_m5_bind()` một lần với các callback có thật. Bindings không bị thay nóng trong lúc chạy. Không dùng callback trả OK rỗng.

```c
rtsfr_m5_bindings_t bindings = {0};
/* Owners cung cấp implementation phù hợp registry/resource model thật: */
bindings.validate_instance = /* M1: TODO */;
bindings.report_fault      = /* M1: TODO */;
bindings.start_test        = /* target integration: TODO */;
bindings.snapshot          = /* M1 + M2 + M3: TODO */;
/* Chỉ gọi khi các hàm thật đã có. Không copy nguyên ví dụ TODO này để build. */
```

`validate_instance(id, generation, caller_is_target)` phải kiểm tra ID, generation hiện hành và handle của task gọi nếu `caller_is_target=true`. Gọi arm từ command không yêu cầu command task là task đích. Instance cũ không được inject/báo progress cho instance mới.

`snapshot(out)` phải tạo ảnh nhất quán, hữu hạn của registry và tài nguyên. Không đặt `registry_complete`/`resources_complete=true` khi chưa kiểm được đầy đủ. Bất kỳ con trỏ/phần dữ liệu không tin cậy phải khiến provider trả lỗi hoặc NOT_READY; không dereference TCB private từ M5.

`start_test(test_id)` khởi động kịch bản thực, bất đồng bộ, và trả NOT_READY nếu kịch bản chưa có. Sau khi kiểm đủ expected/actual, implementation gọi `rtsfr_m5_test_finish(id, passed, reason)`. Không coi việc arm thành công hoặc thấy RECOVERY_OK là toàn bộ TC đã PASS.

## Kết nối M1/M2

- M1 gọi `rtsfr_m5_on_fault(event_id, task_id, generation, reason)` tại lúc phát hiện.
- M2 gọi `rtsfr_m5_on_decision(&decision)` khi ra quyết định thật.
- M1 gọi `rtsfr_m5_on_first_progress(event_id, id, new_generation)` tại mốc công việc thực sự hoàn tất đầu tiên của instance mới; đồng thời báo M4 bằng API của M4.
- Trace FIRST_PROGRESS của M4 được giữ là RECOVERY_STEP vì M4 có thể xử lý nó muộn hơn mốc công việc thực. Script không suy diễn thời điểm first progress từ lúc tạo task hoặc lúc M4 quét.
- Trong TEST, M1 có thể dùng `rtsfr_fault_correlation(id, generation)` để lấy reserved injection ID (>=0x80000000); nếu khác zero dùng ID đó xuyên suốt fault/decision/recovery. Khi bằng zero dùng allocator event ID bình thường. Tránh trùng namespace.
- `test_id` trong trace được set trước khi test bắt đầu. Chạy một target test tại một thời điểm.

## Kết nối M4

Gắn vào cấu trúc hooks hiện có của M4, giữ nguyên callback chức năng M1/M3:

```c
hooks.trace = rtsfr_m5_on_recovery_trace;
hooks.record_result = rtsfr_m5_on_recovery_result;
```

Nếu đã có consumer khác, Tú viết callback tổng gọi cả hai, không âm thầm ghi đè chức năng. Mã M4 hiện có không bị sửa trong gói này. Hai hook trên chỉ ghi ring, không block/UART.

## Fault checkpoints

```c
/* Trong task đích, sau khi hoàn thành một đơn vị công việc: */
if (rtsfr_fault_checkpoint(task_id, generation, M5_POINT_WORK)) {
    /* Gọi progress thật của M1 tại đây. */
}
/* Sau khi acquire buffer độc quyền thật / mutex take thành công thật: */
(void)rtsfr_fault_checkpoint(task_id, generation, M5_POINT_BUFFER_HELD);
(void)rtsfr_fault_checkpoint(task_id, generation, M5_POINT_MUTEX_HELD);
```

Không gọi hai checkpoint resource vào cùng chỗ nếu task không thật sự đang giữ tài nguyên tương ứng. Các ca giữ tài nguyên cố ý busy-loop, giữ nguyên mutex/buffer để framework quan sát. Busy-loop không tắt tick; supervisor phải có priority cao hơn task đích. Idle có thể bị đói; không dùng scenario này để kết luận heap cleanup ổn định.

Scenario expected-wait chỉ delay 1000 ms. M1/target phải công bố expected-wait contract trước checkpoint; thiếu contract thì không được tính TC04 PASS.

## Health gate và IWDG

- Kernel integration owner bind đúng supervisor handle qua `rtsfr_health_bind_supervisor()`.
- Chính supervisor gọi `rtsfr_health_publish()`, cung cấp sequence tăng, timestamp, trạng thái scheduler/tasks, invariant/resource evidence và recovery budget.
- `invariants_verified` chỉ true sau check thành công với snapshot đầy đủ. `resources_consistent` và `safe_outputs_confirmed` phải có chứng cứ thực; không đặt hằng true.
- Safe mode phải có xác nhận đầu ra an toàn. Recovery phải có hạn thời gian hữu hạn; không kéo dài budget mỗi lần poll.
- Sequence đã refresh không được tái sử dụng; timestamp/sequence dùng khoảng dưới nửa miền số để phân biệt wrap và dữ liệu cũ.
- Chỉ task gọi `rtsfr_iwdg_start()` thành công được `rtsfr_iwdg_poll()` refresh. Gate/write/consume-sequence nằm cùng critical section ngắn.
- Không start tự động nếu chưa xác nhận timeout/config. `RTSFR_IWDG_CONFIG_CONFIRMED` mặc định 0.

## Context và ownership

Trace task API dùng được trong nested kernel critical section. API FromISR chỉ cho ngắt có priority được FreeRTOS cho phép; không NMI, HardFault, stack-overflow tùy ý. USART1 RX do M5 sở hữu trong build bật M5; không đồng thời dùng HAL receive interrupt/DMA trên UART1. Một consumer xuất UART từ bootstrap. Metadata RX và trace đều có critical section; không dùng volatile thay synchronization.

Snapshot verifier là bounded O(T² + R² + TR), T<=3, R<=8. Trace push/pop là O(1), dung lượng mặc định 32. Đây là giới hạn cấu trúc; WCET phải đo thật.
