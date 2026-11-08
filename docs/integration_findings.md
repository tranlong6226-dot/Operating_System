# Các điểm cần điều phối cùng nhóm

1. M1/M2/M3 chưa hiện diện trong source tree snapshot ban đầu; M5 giữ dependency thiếu là NOT_READY.
2. File firmware/tests/target/test_m4.c được project tham chiếu nhưng chưa được commit. Đã exclude reference để build, không xóa code M4 hay tự giả lập thành target test thật.
3. Callback false-alarm cancellation hiện tại của M4: state được hủy và task resume đúng, nhưng rtsfr_recovery_process() trả RTSFR_ERR_INTERNAL do outcome REJECTED đi qua prv_finish(). Host contract test ghi nhận hành vi hiện hữu. Tú cần thống nhất status mong muốn; M5 chưa sửa logic M4.
4. Heap_4 dùng malloc-failed hook hiện tại có thể trap khi OOM thật. TC08 trên board cần Tú cung cấp seam kiểm soát create-failure (TEST only) hoặc policy OOM phù hợp; không làm cạn heap ngẫu nhiên rồi gọi đó là test recovery thành công.
5. Trace overflow là mất bằng chứng, có counter/record hiển thị; không thay thế TC13 saturation của kernel event queue M1. Hai queue này khác nhau.
6. Snapshot checker chỉ kiểm các quy tắc trên dữ liệu provider đầy đủ, không chứng minh không có memory corruption hoặc dữ liệu chung luôn đúng. I7 (context rules) cần code review/instrumentation; I9 safe-output và I10 watchdog cần target evidence.
7. Host RTOS doubles không mô phỏng scheduler, ISR timing, priority inheritance, actual memory reclamation hay race phần cứng. Cần kiểm thử board riêng.
