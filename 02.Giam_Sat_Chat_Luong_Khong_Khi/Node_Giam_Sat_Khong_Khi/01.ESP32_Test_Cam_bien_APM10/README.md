# Test cảm biến bụi ASAIR APM10 với ESP32

Đọc **PM1.0, PM2.5, PM10** (µg/m³) từ cảm biến laser APM10 (Aosong/ASAIR) mỗi giây, in ra Serial Monitor kèm thống kê, cảnh báo dữ liệu bất thường và gợi ý xử lý khi đọc lỗi.

- Hỗ trợ cả 2 giao tiếp của APM10: **UART** (khuyên dùng) và **I2C**.
- Driver viết riêng theo datasheet (trên mạng chưa có thư viện APM10 nào), tách khỏi phần cứng nên được test kỹ trên máy tính bằng cảm biến giả lập.

## 1. Cấu trúc

```
platformio.ini               3 môi trường: esp32_uart, esp32_i2c, native (test trên PC)
include/config.h             chân nối, chu kỳ đọc, thời gian khởi động
src/main.cpp                 chương trình test chạy trên ESP32
lib/APM10/src/
  apm10_protocol.*           giao thức: CRC8, khung UART, giải mã I2C (C++ thuần)
  apm10_driver.*             driver UART / I2C (qua interface, không phụ thuộc Arduino)
  apm10_arduino.h            nối driver với HardwareSerial / Wire của ESP32
  pm25_level.h               phân mức PM2.5 (Tốt, Trung bình, ...)
test/test_protocol/          37 test giao thức
test/test_driver/            21 test driver + cảm biến APM10 giả lập (fake_apm10.h)
```

## 2. Nối dây

Chân APM10 (đầu nối MX1.25-8Y): 1 VCC · 2 GND · 3 SET · 4 RX/SDA · 5 TX/SCL · 6,7 NC · 8 PWM

### Chế độ UART (khuyên dùng — đọc đủ PM1.0/PM2.5/PM10 ở cả 2 bản datasheet)

| APM10 | ESP32 |
|---|---|
| Pin 1 VCC | **5V** (VIN) — cảm biến cần 4.75–5.25 V, dòng tới 100 mA |
| Pin 2 GND | GND |
| Pin 3 SET | để hở (hoặc nối 3.3V) |
| Pin 4 RX | GPIO17 (TX2) |
| Pin 5 TX | GPIO16 (RX2) |

### Chế độ I2C

| APM10 | ESP32 |
|---|---|
| Pin 1 VCC | 5V |
| Pin 2 GND | GND |
| Pin 3 SET | **GND** |
| Pin 4 SDA | GPIO21 + điện trở kéo lên 4.7 kΩ về **3.3V** |
| Pin 5 SCL | GPIO22 + điện trở kéo lên 4.7 kΩ về **3.3V** |

> ⚠️ **Mức điện áp — kiểm tra trước khi cắm vào ESP32.** Cảm biến chạy 5V, còn chân ESP32 chỉ chịu tối đa 3.6V. Datasheet không ghi mức logic của chân TX, và bảo kéo lên SDA/SCL về VCC (5V) — **đừng làm vậy với ESP32**, hãy kéo lên 3.3V.
> Trước khi nối: cấp 5V cho riêng cảm biến, dùng đồng hồ đo **pin 5 (TX) so với GND**:
> - ≤ 3.3V → nối thẳng vào GPIO16.
> - ~5V → thêm cầu chia áp (ví dụ 10 kΩ nối tiếp + 20 kΩ xuống GND) hoặc mạch chuyển mức.
>
> Ở chế độ I2C, đo SDA/SCL khi chưa gắn điện trở kéo lên: nếu đã có ~5V (cảm biến tự kéo lên bên trong) thì phải dùng mạch chuyển mức.
>
> Vỏ kim loại của cảm biến nối với nguồn bên trong — không cho chạm vào mạch khác.

Đổi chân SET thì phải **rút nguồn cảm biến rồi cấp lại** để cảm biến nhận chế độ mới.

## 3. Test trên máy tính (không cần mạch)

```bash
pio test -e native
```

Kết quả hiện tại: **58/58 test PASS**. Các test kiểm tra:

- CRC8 khớp vector chuẩn và khớp byte `F6` trong lệnh Start của datasheet; các byte lệnh khớp datasheet (`FE A5 00 01 A6`, `00 10 05 00 F6`, `01 04`, `03 00`).
- Giải mã khung UART với cả 2 kiểu byte LEN (xem mục 6), khung có rác phía trước, header giả, dữ liệu chứa `FE A5`, khung đứt đoạn, hai khung liền nhau, nhận từng byte.
- **Mọi khung bị hỏng 1 byte đều bị loại**; mọi lỗi lật 1 bit ở dữ liệu I2C đều bị CRC bắt.
- Fuzz 20.000 buffer ngẫu nhiên: không crash, không nhận khung sai checksum.
- Driver với cảm biến giả lập chạy đúng tốc độ 1200 baud: cảm biến im lặng (timeout), phản hồi chậm, byte cũ sót trong buffer, 100 byte nhiễu, cảm biến chỉ trả PM2.5.
- **Chạy liền 3.000 lần đọc UART và 3.000 lần đọc I2C có chèn lỗi ngẫu nhiên**: mọi lần báo OK đều ra đúng giá trị cảm biến gửi.

Đã thử cố tình cài 7 lỗi khác nhau vào code (sai đa thức CRC, sai vị trí PM10, bỏ kiểm CRC, bỏ xóa buffer, ...): lỗi nào cũng bị test bắt.

## 4. Build và nạp xuống ESP32

```bash
pio run -e esp32_uart -t upload -t monitor
```

Dùng I2C thì thay bằng `esp32_i2c`. Serial Monitor chạy ở 115200 baud.

Muốn chạy lại bộ unit test ngay trên ESP32 thật (chỉ cần cắm USB, chưa cần cảm biến):

```bash
pio test -e esp32_uart
```

### Kết quả mong đợi trên Serial Monitor

```
================================================
 TEST CAM BIEN BUI ASAIR APM10 - PM1.0/PM2.5/PM10
================================================
Che do giao tiep: UART (1200 baud, 8N1)
Lenh doc 3 thong so: FE A5 00 01 A6
[    2s] PM1.0=   9  PM2.5=  14  PM10=  17  ug/m3 | Tot        | dang khoi dong
[    3s] PM1.0=  10  PM2.5=  15  PM10=  18  ug/m3 | Tot        | dang khoi dong
```

Phím lệnh: `h` trợ giúp · `r` bật/tắt byte thô (hex) · `s` thống kê · `c` xóa thống kê · `p` tạm dừng · `2` (UART) đọc thử lệnh riêng PM2.5 · `x`/`g` (I2C) dừng/bắt đầu đo.

## 5. Kịch bản kiểm tra trên mạch thật

1. **Đo mức điện áp** chân TX (hoặc SDA/SCL) như cảnh báo ở mục 2.
2. Cấp nguồn: quạt trong cảm biến phải quay (nghe tiếng rì nhẹ).
3. Nạp `esp32_uart`, mở Serial Monitor, bấm `r` để xem byte thô. Khung đúng có dạng `FE A5 .. .. (6 byte) CS`.
4. Để yên 1–2 phút trong phòng: các giá trị ổn định, **PM1.0 ≤ PM2.5 ≤ PM10**, không có cảnh báo.
5. Đốt một nén nhang cách đầu hút gió khoảng 30 cm: cả ba giá trị phải tăng rõ trong vài giây, bỏ nhang ra thì giảm dần.
6. Bấm `s` sau ~5 phút: tỷ lệ thành công nên là **100%** (nếu thấp hơn 99% thì kiểm tra dây/GND/nhiễu).
7. Rút dây TX trong lúc chạy: phải thấy `LOI: NO_RESPONSE` và sau 5 lần thì in gợi ý kiểm tra. Cắm lại: tự đọc lại được.
8. (Tùy chọn) Chuyển SET xuống GND, rút nguồn rồi cấp lại cảm biến, nạp `esp32_i2c` và so sánh với kết quả UART.

## 6. Những điểm datasheet ghi mâu thuẫn và cách code xử lý

| Vấn đề | Cách xử lý |
|---|---|
| Phản hồi lệnh `0x01` ghi `FE A5 02 00` + 6 byte dữ liệu, tức byte LEN = 02 dù có 6 byte data | Parser ưu tiên độ dài ứng với lệnh đã gửi (6 byte) và dùng checksum để xác nhận. Chấp nhận cả LEN = 02 lẫn 06. Thống kê sẽ hiện "Byte LEN khac datasheet" — chuyện này bình thường. |
| Bảng dữ liệu I2C: bản tiếng Anh (2022) ghi byte 9–11 là "PM1.0" (lặp lại, nhiều khả năng là PM10); bản tiếng Trung (2021) chỉ có PM2.5 | Đọc word0 = PM1.0, word1 = PM2.5, word3 = PM10. Nếu cảm biến của bạn chạy firmware cũ, PM1.0/PM10 qua I2C có thể bằng 0 → **hãy dùng UART**. |
| Datasheet không ghi khoảng chờ giữa lệnh đọc I2C và lúc đọc dữ liệu | Có STOP rồi chờ 5 ms (`kDefaultReadDelayMs`) trước khi đọc 30 byte. |

## 7. Bảng lỗi

| Thông báo | Nguyên nhân hay gặp |
|---|---|
| `NO_RESPONSE` (UART) | Đảo ngược RX/TX, SET đang ở GND, cảm biến chưa có 5V, quên nối GND chung |
| `BAD_CHECKSUM` / `BAD_FRAME` | Nhiễu, dây dài, mức điện áp sai; bấm `r` để xem byte thô |
| `INCOMPLETE` | Mất byte giữa khung (nhiễu, dây lỏng) |
| `I2C_NACK` | SET không nối GND, thiếu điện trở kéo lên, sai SDA/SCL |
| `CRC_ERROR` (I2C) | Nhiễu bus, điện trở kéo lên quá yếu |
| Cảnh báo `PM1<=PM2.5<=PM10 sai` | Dữ liệu bất thường, hoặc PM1.0/PM10 qua I2C không được firmware cảm biến hỗ trợ |

Mức chất lượng không khí in kèm (Tốt / Trung bình / Kém / Xấu / Rất xấu / Nguy hại) dùng ngưỡng PM2.5 của VN_AQI và **chỉ để tham khảo**: AQI chuẩn tính trên giá trị trung bình theo giờ, không phải giá trị tức thời.

## Tài liệu

- Datasheet APM10, tiếng Anh, 07/2022: https://robu.in/wp-content/uploads/2022/06/datasheet-apm10-2022_07_09.pdf
- Datasheet APM10, tiếng Trung, 07/2021: https://static.chipdip.ru/lib/569/DOC043569932.pdf
