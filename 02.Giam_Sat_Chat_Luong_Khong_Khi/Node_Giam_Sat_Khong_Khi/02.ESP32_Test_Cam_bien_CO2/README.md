# Test cảm biến CO₂ ASAIR ACD10 với ESP32

Đọc **CO₂ (400–5000 ppm)** từ cảm biến NDIR **ACD10** (Aosong/ASAIR) mỗi 2 giây, in kết quả kèm thống kê
và gợi ý xử lý khi đọc lỗi ra Serial Monitor (115200). Hỗ trợ cả hai giao tiếp của ACD10: **I2C** (mặc định) và **UART**.

## Nối dây

| ACD10 | ESP32 — chế độ I2C (chân SET để hở) | ESP32 — chế độ UART (chân SET nối GND) |
|---|---|---|
| pin 1 | SDA ↔ GPIO21 | RX ← GPIO17 (TX2) |
| pin 2 | SCL ↔ GPIO22 | TX → GPIO16 (RX2) |

ACD10 dùng mức logic 5 V — đi qua **mạch chuyển mức 5 V ↔ 3,3 V** trước khi vào ESP32. I2C chạy 50 kHz
(datasheet cho phép tối đa 100 kHz) để dư phòng khi dây dài. Đổi chân và thông số trong `include/config.h`.

## Build, nạp, test

```bash
pio test -e native                          # unit test trên máy tính, không cần mạch
pio run  -e esp32_i2c -t upload -t monitor   # nạp bản I2C và mở Serial Monitor
pio run  -e esp32_uart -t upload -t monitor  # nạp bản UART
```

## Lệnh trên Serial Monitor (gõ rồi bấm Enter)

| Lệnh | Việc |
|---|---|
| `h` | Trợ giúp |
| `i` | Thông tin cảm biến: phiên bản, số serial, chế độ hiệu chuẩn |
| `r` / `s` / `c` / `p` | Bật/tắt byte thô · in thống kê · xóa thống kê · tạm dừng |
| `auto` / `manual` | Hiệu chuẩn tự động (mặc định xuất xưởng) / thủ công |
| `cal <ppm>` | Hiệu chuẩn 1 điểm, ví dụ `cal 420` — cần xác nhận `y` |
| `factory` | Khôi phục hiệu chuẩn gốc — cần xác nhận `y` |
