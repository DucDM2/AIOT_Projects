# Test cảm biến TVOC ASAIR AGS10 với ESP32

Đọc **TVOC** từ cảm biến **AGS10** (Aosong/ASAIR) trên module **MakerEdu MKE-S17** qua I2C, mỗi 3 giây.
In kết quả kèm thống kê và gợi ý xử lý khi đọc lỗi ra Serial Monitor (115200); quét bus I2C khi khởi động.

## Nối dây (module MKE-S17)

| MKE-S17 | ESP32 |
|---|---|
| GND | GND |
| 5V | 5V / VIN (module có mạch nguồn riêng cho cảm biến 3,0 V — **cảm biến trần không được cấp > 3 V**) |
| SDA | GPIO21 |
| SCL | GPIO22 |

Trước khi nối, đo điện áp SDA/SCL của module khi chưa gắn ESP32 — nếu > 3,6 V cần mạch chuyển mức logic.
I2C chạy **10 kHz** (datasheet: tối đa 15 kHz). Cảm biến cần **khoảng 120 giây làm nóng** sau khi cấp điện mới
trả số đo hợp lệ; đọc nhanh hơn 1,5 s/lần sẽ làm hỏng dữ liệu. Đổi thông số trong `include/config.h`.

## Build, nạp, test

```bash
pio test -e native                     # unit test giao thức trên máy tính, không cần mạch
pio run  -e esp32 -t upload -t monitor  # nạp và mở Serial Monitor
pio test -e esp32                      # chạy lại unit test ngay trên ESP32 thật
```

## Phím lệnh trên Serial Monitor

| Phím | Việc |
|---|---|
| `h` | Trợ giúp |
| `r` / `s` / `c` / `p` | Bật/tắt byte thô · in thống kê · xóa thống kê · tạm dừng |
| `v` / `o` | Đọc phiên bản firmware cảm biến · đọc điện trở cảm biến |
| `z` | Hiệu chuẩn điểm 0 bằng điện trở hiện tại — cần xác nhận `y` |
| `f` | Trả điểm 0 về giá trị nhà sản xuất — cần xác nhận `y` |
