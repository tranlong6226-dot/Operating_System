# Test plan TC01–TC14

Tác giả M5: Thủy. Tất cả test trên STM32 hiện **NOT_RUN**. Callback target chưa được owners cung cấp thì `test N` trả NOT_READY, không tự PASS.

## Quy tắc chung

- Ghi build SHA/profile, board, clock/tick, priorities, config, raw log và expected/actual.
- Dùng `run <ID_MOI>` trước mỗi ca; không đổi run ID khi test đang chạy.
- Lệnh `test N` yêu cầu implementation start_test tương ứng; xem m5_interfaces.md.
- Test PASS cần đủ bằng chứng; thiếu evidence là NOT_RUN/INCOMPLETE, không suy diễn từ LED hoặc build.
- Kiểm tra loss counters; test timing nên có ít nhất 30 lần chạy sau khi khóa cấu hình.
- Dữ liệu host/synthetic không được đưa vào kết quả target.

## TC01 — Normal operation

- Fault model: F04/F10
- Cần: M1,M2,workload
- Bước chạy: Khởi tạo 3 task khỏe; bật progress/expected-wait thật; thu log ít nhất 5 phút.
- Kỳ vọng: Không fault/restart/false alarm; task counters tiếp tục; trace/UART không mất bằng chứng.
- Bằng chứng: Raw progress/counters, FAULT count=0, run duration, status counters.
- Actual:
- Verdict: **NOT_RUN**
- Build / raw log:

## TC02 — No-progress restart

- Fault model: F01
- Cần: M1,M2,M3,M4
- Bước chạy: Chọn task không giữ resource; test 2; arm scenario 1; target đi qua work checkpoint.
- Kỳ vọng: INJECT → FAULT_DETECTED → POLICY(RESTART) → first progress generation mới → RECOVERY_OK; healthy task counters tiếp tục.
- Bằng chứng: Trace cùng event ID, resource snapshot, old/new generation, counters task khỏe.
- Actual:
- Verdict: **NOT_RUN**
- Build / raw log:

## TC03 — Busy-loop detection

- Fault model: F02
- Cần: M1,M2,M3,M4,priority contract
- Bước chạy: Task không giữ resource; supervisor priority cao hơn; scenario 2 tại work checkpoint, IRQ vẫn bật.
- Kỳ vọng: Supervisor còn chạy, detect và restart task đích; không dùng reset MCU thay task recovery.
- Bằng chứng: Supervisor progress, GPIO/counters task khỏe, new generation, reset-cause/no reboot.
- Actual:
- Verdict: **NOT_RUN**
- Build / raw log:

## TC04 — Expected wait

- Fault model: F04/F10
- Cần: M1 expected-wait, workload
- Bước chạy: Công bố expected-wait hợp lệ cho khoảng delay; scenario 4; lặp cả trường hợp chờ mutex hợp lệ.
- Kỳ vọng: Không báo lỗi/restart trong hợp đồng; sau khi hết chờ task tiếp tục tiến triển.
- Bằng chứng: Expected-wait start/deadline, fault count, task counters/generation.
- Actual:
- Verdict: **NOT_RUN**
- Build / raw log:

## TC05 — Managed-buffer reclaim

- Fault model: F05
- Cần: M3 buffer pool,M1,M2,M4
- Bước chạy: Cấp buffer độc quyền thật; xác nhận không DMA/consumer khác; scenario 5 ở BUFFER_HELD.
- Kỳ vọng: Quiesce → revalidate → cleanup → restart; buffer thuộc một owner hoặc free, pool count đúng.
- Bằng chứng: Resource snapshots/counts trước-sau, cleanup result, new generation, no double-free.
- Actual:
- Verdict: **NOT_RUN**
- Build / raw log:

## TC06 — Mutex held → safe mode

- Fault model: F06
- Cần: M3 mutex hooks,M2,M4,safe output
- Bước chạy: Task take mutex thành công; scenario 6 tại MUTEX_HELD; chờ timeout.
- Kỳ vọng: Từ chối unsafe restart; SAFE_MODE; không auto-give mutex; đầu ra an toàn được xác nhận.
- Bằng chứng: Owner snapshot, policy reason, no unlock/recreate, output observation.
- Actual:
- Verdict: **NOT_RUN**
- Build / raw log:

## TC07 — Retry budget

- Fault model: F07
- Cần: M2 retry policy,M1,M3,M4
- Bước chạy: Tạo lỗi qua nhiều generation trong retry window thật; ví dụ giới hạn 2 thì thử lần 3 trong 60 s nếu config đó được chọn.
- Kỳ vọng: Không restart thêm khi hết budget; chuyển safe mode/reset theo policy.
- Bằng chứng: Config retry/window, generation sequence, timestamp decisions, restart count.
- Actual:
- Verdict: **NOT_RUN**
- Build / raw log:

## TC08 — Create failure

- Fault model: F03/error path
- Cần: M4 TEST allocation seam,M2
- Bước chạy: Sau khi dọn task cũ, seam TEST của M4 ép đúng lần xTaskCreate trả thất bại; không vô hiệu hóa lỗi production âm thầm.
- Kỳ vọng: CREATE_FAILED → SAFE_MODE; registry không trỏ dangling handle; không lặp create/leak.
- Bằng chứng: Seam/build flag, create status, registry/resource/heap evidence. Host mock hiện có chỉ là contract test.
- Actual:
- Verdict: **NOT_RUN**
- Build / raw log:

## TC09 — Late progress rollback

- Fault model: F01 false alarm
- Cần: M1,M2,M4 controlled interleaving
- Bước chạy: Điều khiển progress đến sau timeout nhưng trước cleanup/delete commit; thử cả trước và sau quiesce trong miền cho hủy.
- Kỳ vọng: Hủy recovery hợp lệ, resume đúng instance nếu cần; task khỏe không bị delete/recreate.
- Bằng chứng: Event/state ordering, original handle/generation, delete count=0, cancellation result.
- Actual:
- Verdict: **NOT_RUN**
- Build / raw log:

## TC10 — Stale-generation event

- Fault model: F01/event lifecycle
- Cần: M1,M2,M4
- Bước chạy: Restart thành công rồi gửi lại fault/decision/progress của generation cũ và duplicate event ID.
- Kỳ vọng: Bỏ qua dữ liệu cũ; không double restart; progress cũ không xác minh instance mới.
- Bằng chứng: Old/current generation, stale reason, unchanged handle/generation/create counter.
- Actual:
- Verdict: **NOT_RUN**
- Build / raw log:

## TC11 — Repeated recovery / heap

- Fault model: F01/F05
- Cần: M1,M2,M3,M4,heap_4,idle progress
- Bước chạy: Chạy ít nhất 100 chu kỳ đủ điều kiện; đảm bảo Idle thật được chạy và kernel cleanup hoàn tất trước so heap.
- Kỳ vọng: Heap không có xu hướng tăng used dài hạn, resource count đúng, không dangling handle.
- Bằng chứng: Heap/free samples cùng checkpoint, idle cleanup evidence, 100 raw outcomes. Mock counts không đủ.
- Actual:
- Verdict: **NOT_RUN**
- Build / raw log:

## TC12 — IWDG fallback

- Fault model: F08/F09 fallback
- Cần: Confirmed IWDG,real board,supervisor health binding
- Bước chạy: Bật IWDG qua health gate thật; gây stall supervisor/tick có kiểm soát; quan sát boot sau reset và RCC reset cause.
- Kỳ vọng: Ngừng refresh khi không còn health evidence; reset thật do IWDG; không selective restart khi kernel không tin cậy.
- Bằng chứng: Pre-reset transcript nếu có, post-boot IWDGRSTF, build/config, thời gian đo. iwdg-reset primitive chưa đủ TC12.
- Actual:
- Verdict: **NOT_RUN**
- Build / raw log:

## TC13 — Kernel event queue saturation

- Fault model: F01/event overflow
- Cần: M1 actual kernel event queue,M2 policy
- Bước chạy: Đưa fault events vượt dung lượng event queue M1 bằng seam TEST hữu hạn; theo dõi overflow path.
- Kỳ vọng: Overflow hiển thị và fail-safe theo policy; không silently drop/restart sai.
- Bằng chứng: Queue size, injected/accepted/dropped counts, overflow reason/action. Tràn trace ring là test khác.
- Actual:
- Verdict: **NOT_RUN**
- Build / raw log:

## TC14 — Resource invariants

- Fault model: F05/F06/resource lifecycle
- Cần: M3 full snapshot,M1 generation,M4
- Bước chạy: Acquire/release buffer và mutex nhiều lần; snapshot đầy đủ ở các checkpoint; thử stale owner/double ownership có kiểm soát.
- Kỳ vọng: Mỗi resource có state hợp lệ; buffer không cấp trùng; owner/generation đúng; unsafe snapshot không dẫn tới restart.
- Bằng chứng: Snapshots, invariant reports, ownership transitions, no double-free. Không phá kernel memory ngẫu nhiên.
- Actual:
- Verdict: **NOT_RUN**
- Build / raw log:

## Bất biến và phạm vi kiểm tra

| Đặc tả | Cách M5 kiểm | Giới hạn |
|---|---|---|
| I1/I2/I6 task/generation | Snapshot + host M4 contract + target TC09/10 | Snapshot không tự chứng minh lifetime của TCB |
| I3 resource ownership | Snapshot checker + TC05/14 | Cần M3 đầy đủ |
| I4 safe restart | Snapshot restarting/resources_safe + M4 hook tests | Cần revalidation thật M4/M3 |
| I5 progress trước success | first_progress/recovery_ok + event correlation | M1 phải báo tại mốc công việc thật |
| I7 không blocking sai context | Source review, trace có API FromISR riêng | Không tuyên bố chứng minh toàn bộ kernel |
| I8 retry budget | Snapshot retries/limit + TC07 | M2 là owner counter/window |
| I9 safe outputs | TC06/07 và safe_outputs_confirmed | Định nghĩa output còn thiếu |
| I10 watchdog có health gate | Host gate + board TC12 | Host không thay reset thật |
