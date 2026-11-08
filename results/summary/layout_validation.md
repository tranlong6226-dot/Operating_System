# Kiểm chứng sau khi chuyển sang firmware/

Ngày: 08/11/2026 (Asia/Saigon). Nhánh quan sát: tran-thi-thanh-thuy; commit nguồn trước chuyển: b04a031.

- 139 file C/header/startup/CubeMX metadata được di chuyển và giữ nguyên từng byte.
- Kiểm tra đường dẫn source/include/output của 4 target Keil: PASS (172 active source references).
- 7 nhóm host tests: PASS; Python suite có 6 test như lần kiểm tra trước.
- Source/include path của host tests đã trỏ về firmware; test chạy trên máy tính vẫn ở root tests/host/.

| Cấu hình | Build | Flash (bytes) | RAM reserve (bytes) | HEX giống trước khi chuyển |
|---|---|---:|---:|---|
| M5_DEBUG_TEST | BUILD_PASS | 17024 | 14584 | Có |
| M5_RELEASE | BUILD_PASS | 16664 | 14560 | Có |
| M5_BENCHMARK | BUILD_PASS | 17108 | 14584 | Có |

Arm Compiler 6.24 / -Oz; số liệu lấy từ linker map, không phải RAM high-water trên board. Assembler upstream vẫn có warning A1950W (legacy armasm deprecated).

CubeMX .ioc/.mxproject đã chuyển cùng firmware; quan hệ đường dẫn giữ nguyên. Chưa chạy regenerate bằng CubeMX hoặc build qua GUI Keil. CLI dùng chính source/defines/include paths trong project Keil mới.

TC01–TC14 trên STM32, IWDG reset thật và benchmark V0/V1/V2 vẫn NOT_RUN. Không có số đo phần cứng mới trong lần sắp xếp này.

Các artifact cũ được lưu cục bộ trong build/legacy_keil/ và bỏ qua bởi Git; source M4/kernel không thay nội dung.
