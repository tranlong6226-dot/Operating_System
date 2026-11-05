# RTSFR - Resource-aware Task Selective Fault Recovery

Đề tài nghiên cứu và phát triển cơ chế phục hồi lỗi có chọn lọc theo ngữ cảnh tài nguyên (RTSFR) cho FreeRTOS Kernel trên vi điều khiển STM32F103.

---

## 📁 Cấu trúc thư mục dự án

```text
freertos-fault-recovery/
├── docs/                                  # Tài liệu thiết kế & phân công công việc
│   ├── 01_DAC_TA_HE_THONG_RTSFR.md       # Đặc tả hệ thống RTSFR chi tiết
│   └── 02_PHAN_CONG_CONG_VIEC_VA_QUY_UOC_NHOM.md # Giao diện module & quy ước làm việc
├── firmware/                              # Toàn bộ mã nguồn nhúng & cấu hình phần cứng
│   ├── Core/                              # Mã nguồn khởi tạo ngoại vi (HAL, Clock, GPIO, UART)
│   ├── Drivers/                           # STM32 HAL Driver & CMSIS
│   ├── Middlewares/                       # FreeRTOS Kernel nguồn
│   ├── MDK-ARM/                           # Project Keil uVision5
│   │   └── firmware.uvprojx              # File mở project Keil
│   └── firmware.ioc                       # File cấu hình phần cứng STM32CubeMX
├── tests/                                 # Kịch bản kiểm thử (Host tests & Target test scenarios)
├── tools/                                 # Script tiện ích (thu thập UART log, phân tích dữ liệu)
├── .gitignore
└── README.md
```

---

## 🚀 Hướng dẫn mở dự án

### 1. Cấu hình phần cứng (STM32CubeMX)
- Khởi động **STM32CubeMX**.
- Mở file cấu hình tại: [`firmware/firmware.ioc`](file:///d:/Study/HUST/2026/HDH/freertos-fault-recovery/firmware/firmware.ioc).
- Khi bấm **Generate Code**, mã nguồn sẽ được cập nhật đồng bộ trực tiếp vào thư mục `firmware/`.

### 2. Biên dịch & Nạp firmware (Keil uVision5)
- Mở project tại: [`firmware/MDK-ARM/firmware.uvprojx`](file:///d:/Study/HUST/2026/HDH/freertos-fault-recovery/firmware/MDK-ARM/firmware.uvprojx).
- Chọn Target: `firmware`.
- Bấm **F7** (Build Target) để biên dịch.
- Bấm **F8** (Download) để nạp xuống bo mạch STM32 qua ST-Link / J-Link.

---

## 👥 Phân công module kỹ thuật
- **Module 1 (Việt Anh):** Fault Detection & Kernel Integration (Task progress registry, lifecycle hooks).
- **Module 2 (Vũ Long):** Fault Classification & Recovery Policy (Policy table, retry budget).
- **Module 3 (Hồng Long):** Resource Ownership Tracker (Mutex ownership tracking & managed buffer).
- **Module 4 (Tú):** Selective Recovery Manager (Quiesce, cleanup, delete/recreate task, safe-mode).
- **Module 5 (Thủy):** Fault Injection, Verification & Watchdog (Test harness, trace, IWDG).
