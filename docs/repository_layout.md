# Cấu trúc repo sau khi gom firmware

```text
Operating_System/
├── firmware/
│   ├── App/
│   ├── board/
│   ├── Core/
│   ├── Drivers/
│   ├── Middlewares/Third_Party/FreeRTOS/
│   ├── RTSFR/Inc/
│   ├── RTSFR/Src/
│   ├── tests/target/
│   ├── MDK-ARM/
│   │   ├── Operating_System_Project.uvprojx
│   │   ├── Operating_System_Project.uvoptx
│   │   ├── startup_stm32f103xb.s
│   │   ├── DebugConfig/
│   │   └── RTE/
│   ├── Operating_System_Project.ioc
│   ├── .mxproject
│   └── README.md
├── tests/
│   ├── host/
│   └── fixtures/
├── tools/
├── docs/
├── results/
│   ├── raw/
│   └── summary/
├── build/                     # gitignored
│   ├── M5_DEBUG_TEST/
│   ├── M5_RELEASE/
│   ├── M5_BENCHMARK/
│   ├── host/
│   ├── keil_<TARGET>/
│   └── legacy_keil/           # chỉ có trên máy đã chuyển cấu trúc
├── .gitignore
└── README.md
```

## File nào đã chuyển?

`App`, `board`, `Core`, `Drivers`, `Middlewares`, `RTSFR`, `MDK-ARM`, `.ioc` và `.mxproject` từ root vào `firmware/`. `tests/target/` vào `firmware/tests/target/` vì chạy trên STM32; `tests/host/` và dữ liệu mẫu giữ ở root.

Source C/header/startup và cấu hình .ioc được giữ nguyên nội dung. Các đường dẫn relative giữa Core/Drivers/Middlewares/RTSFR/App/board/MDK-ARM vẫn giữ quan hệ thư mục cũ. Không đổi compiler, clock, task priority, logic phục hồi hoặc API M1–M5.

## Cách mở và build

- Keil: mở `firmware/MDK-ARM/Operating_System_Project.uvprojx`.
- CubeMX: mở `firmware/Operating_System_Project.ioc`; `.mxproject` đi cùng tại `firmware/`.
- Python: tiếp tục chạy `python tools/...` từ root repo; không cần cd vào firmware.
- Source/include/test path trong scripts đã trỏ về firmware. OutputDirectory và ListingPath của cả 4 target Keil trỏ về `../../build/keil_<TARGET>/` từ thư mục MDK-ARM.
- `python tools/check_layout.py` kiểm tra source/include/output paths và vị trí CubeMX. Việc kiểm tra này không chạy CubeMX regenerate hoặc flash phần cứng.

## Artifact cũ

Thư mục build cũ `MDK-ARM/Operating_System_Project/`, ba m4_*.log và file .uvguix.Admin được đưa vào `build/legacy_keil/`. Chúng được lưu cục bộ để đối chiếu và bị bỏ qua bởi Git. Sau commit cấu trúc mới, clone sạch sẽ không có những file sinh tự động đó; build lại để tạo artifact mới.

Giữ `.uvoptx`, RTE và DebugConfig dùng chung để bảo toàn cấu hình debug. File .sct trong thư mục build cũ là scatter file sinh tự động; project dùng scatter tự sinh, không tham chiếu file đó như linker script viết tay.

## Phối hợp nhóm

Từ đây, Tú sửa M4 ở `firmware/RTSFR/`, Việt Anh/Hồng Long tích hợp kernel ở `firmware/Middlewares/...`, Thủy sửa firmware M5 ở `firmware/App/`, `firmware/board/`, `firmware/RTSFR/` và test target ở `firmware/tests/target/`.

Các nhánh cũ cần đồng bộ commit chuyển cấu trúc trước khi sửa tiếp, tránh tạo lại Core/RTSFR ở root. Git có thể nhận diện rename khi stage/commit vì nội dung source được giữ nguyên. Tài liệu này không tự gửi thông báo cho nhóm hoặc đổi nhánh người khác.

## Trạng thái kiểm chứng

Xem `results/summary/layout_validation.md`. Test thực trên STM32, reset IWDG và benchmark V0/V1/V2 vẫn giữ trạng thái chưa chạy như trước. Thay cấu trúc không biến các trường NOT_RUN thành PASS.
