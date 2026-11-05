# M4 — Checklist bàn giao và tích hợp

## Phần Tú có thể tự chốt

- [x] M4 source/header compile trong project thật.
- [x] Không dùng UART hoặc heap allocation trong hook/hot path của M4.
- [x] Restart chỉ sau snapshot hậu-quiesce xác nhận safe.
- [x] Không tự release mutex.
- [x] Descriptor recreate không chứa handle cũ.
- [x] Chống event stale/duplicate và recovery song song.
- [x] Chỉ báo thành công sau first progress generation mới.
- [x] Có rollback false alarm trước cleanup/delete.
- [x] Có SAFE_MODE latch, reason và output callback.
- [x] Có target-test độc lập và hướng dẫn chạy.

## Điểm nối cần yêu cầu từng thành viên

| Chủ sở hữu | Interface cần bàn giao cho Tú | Tiêu chí nhận |
|---|---|---|
| M1 — Việt Anh | active generation/handle; prepare delete; publish generation; first progress; false-alarm cancel | caller handle được xác thực; không giữ stale handle |
| M2 — Vũ Long | `rtsfr_decision_t`; callback nhận result | reason/action thống nhất; retry budget không nằm trong M4 |
| M3 — Hồng Long | snapshot nhất quán và cleanup safe | mutex/unknown/in-flight không bao giờ trả safe |
| M5 — Thủy | trace ring; record result; safe output; reset request | không UART trong callback; IWDG không refresh vô điều kiện |

## Gate tích hợp

1. Header type chung được cả nhóm review và khóa.
2. Mỗi callback được test với `NOT_READY`, `STALE`, `UNSAFE`, `OOM` trước success path.
3. Chạy TC02, TC05, TC06, TC08, TC09, TC10 và TC11.
4. Chứng minh task không lỗi vẫn tăng progress trong TC02.
5. Ghi raw log event ID/task ID/generation/reason/tick.
6. Build lại từ clone sạch và lưu `.hex`, `.map`, build log.

## Không được đánh dấu hoàn tất thay module khác

M4 đã hoàn thành control path và safety gates, nhưng không tự tạo dữ liệu giả để thay M1/M3. Nếu callback resource chưa có, recovery phải đi SAFE_MODE. Nếu first progress chưa được M1 xác thực, recovery phải timeout. Đây là hành vi đúng của thiết kế.

