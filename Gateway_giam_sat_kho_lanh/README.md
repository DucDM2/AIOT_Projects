# Gateway giám sát kho lạnh — ESP32 → EMQX

Các bài thử cho **gateway** của giải pháp giám sát kho lạnh: ESP32 nhận dữ liệu cảm biến (giả lập trên chip, hoặc gửi
từ PC qua USB), xếp vào hàng đợi rồi đẩy lên **EMQX Cloud** bằng MQTT/TLS (cổng 8883), theo định dạng bản tin
gateway Milesight UG65.

| Project | Đường truyền | Phần cứng |
|---|---|---|
| [`Test_wifi`](Test_wifi) | WiFi có sẵn trên ESP32 | ESP32 DevKit |
| [`Test_module_4G`](Test_module_4G) | 4G qua module A7680C (lệnh AT) | ESP32 + A7680C |

Bản chạy trên **ESP8266 (Wemos D1)**: [`../Wemos_D1_ESP8266/Test_wifi`](../Wemos_D1_ESP8266/Test_wifi).

## Dùng nhanh

1. Chép `include/secrets.example.h` thành `include/secrets.h`, điền WiFi và thông tin EMQX của **bạn** (file thật đã nằm trong `.gitignore`).
2. Mở project bằng VS Code + PlatformIO → Build → Upload.
3. Firmware có sẵn dữ liệu cảm biến giả lập trên chip; kiểm bản tin trên EMQX bằng [MQTTX](https://mqttx.app).
   Các công cụ PC trong `tools/` (`pc-gui-du-lieu`, `nghe-emqx.mjs`) cần web app giám sát kho lạnh — không nằm trong repo này.

## Phần cứng đang cân nhắc cho gateway thật

- Gateway LoRa dùng chip Semtech SX1302 / SX1308 (băng 433 hoặc 923 MHz)
- Module 4G A7680C, module Ethernet
- Phương án 2: ESP32 làm vi điều khiển chính + module LoRa

> Đây là bài thử đường truyền với dữ liệu giả lập. Web app nhận dữ liệu, cảnh báo và báo cáo thuộc giải pháp
> giám sát kho lạnh của [AIoT Việt](https://aiotviet.com) (không nằm trong repo này).
