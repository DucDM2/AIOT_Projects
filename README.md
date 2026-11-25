# AIoT Việt — Code mẫu & dự án thử nghiệm IoT

**Tiếng Việt** · [English](#english)

Repo này chứa **code mẫu và các bài thử phần cứng** của [AIoT Việt](https://aiotviet.com): ESP32, ESP8266, STM32 (RAK811 / WisNode-LoRa), LoRa, 4G, MQTT/EMQX.

- Mỗi thư mục là **một project độc lập**, build được, có README hướng dẫn nối dây → nạp → chạy.
- Nhiều project có **unit test chạy trên máy tính** (cảm biến giả lập hoặc mô phỏng thanh ghi), không cần mạch.
- Code lõi viết theo hướng sản phẩm: không cấp phát động, có kiểm lỗi, firmware ESP32 kiểm theo **MISRA C:2012**.

> **Đây là code mẫu, không phải bản thương mại.** Bản đầy đủ — firmware ESP32/ESP8266 có cập nhật từ xa (OTA), lịch chạy ngay trên chip, tự đăng ký thiết bị; máy chủ EMQX + Supabase; web app điều khiển và giám sát — được **chuyển giao kèm mã nguồn** cho doanh nghiệp.
> Xem hệ thống chạy thật: **[aiotviet.com](https://aiotviet.com)** · [Đặt lịch demo 30 phút](https://aiotviet.com/#dat-lich)

## Danh sách project

| Project | Phần cứng | Làm gì | Công cụ | Test trên máy tính |
|---|---|---|---|---|
| [`ESP32_relay_AC_x2_board`](ESP32_relay_AC_x2_board) | ESP32-WROOM-32E, bo ESP32 Relay AC X2 | Điều khiển 2 relay qua web nội bộ, nút bấm và MQTTS (EMQX) | ESP-IDF (PlatformIO), C99, MISRA C:2012 | ✔ mô phỏng thanh ghi GPIO/IO_MUX |
| [`02.Giam_Sat_Chat_Luong_Khong_Khi`](02.Giam_Sat_Chat_Luong_Khong_Khi) | ESP32 + cảm biến ASAIR APM10 / ACD10 / AGS10 | Bụi PM1.0/2.5/10, CO₂ (NDIR), TVOC | Arduino (PlatformIO) | ✔ cảm biến giả lập |
| [`Node_giam_sat_kho_lanh`](Node_giam_sat_kho_lanh) | RAK811 (STM32L151 + SX1276) | Nhiệt độ – độ ẩm, cửa mở/đóng, điện áp pin, LoRa điểm-điểm | Bare-metal CMSIS, arm-none-eabi-gcc | — |
| [`Test_RAK811_and_AM2301`](Test_RAK811_and_AM2301) | RAK811 + AM2301 (DHT21) | Nhiệt độ – độ ẩm, chẩn đoán tín hiệu | Bare-metal CMSIS | ✔ test driver |
| [`Gateway_giam_sat_kho_lanh`](Gateway_giam_sat_kho_lanh) | ESP32 (WiFi) · ESP32 + A7680C (4G) | Gateway gửi dữ liệu kho lạnh lên EMQX (MQTT/TLS), dữ liệu giả lập có sẵn trên chip | Arduino (PlatformIO) | — |
| [`Wemos_D1_ESP8266`](Wemos_D1_ESP8266) | Wemos D1 (ESP8266) | Gateway WiFi, bản chuyển từ ESP32 sang ESP8266 | Arduino (PlatformIO) | — |

## Bắt đầu

1. **ESP32 / ESP8266:** cài [VS Code](https://code.visualstudio.com) + extension **PlatformIO**, mở thư mục project, bấm Build / Upload.
2. **RAK811 (STM32):** chạy `setup_toolchain.ps1` trong project để cài arm-none-eabi-gcc, rồi `build.ps1`.
3. **Thông tin bí mật** (WiFi, MQTT): chép file `*.example.h` / `*.env.example` thành file thật rồi điền. File thật đã nằm trong `.gitignore` — **không bao giờ commit mật khẩu**.
4. Chi tiết từng bước nằm trong README của mỗi project.

> ⚠️ **An toàn điện 220 V:** chỉ nạp firmware khi bo cấp nguồn qua USB-TTL, không cắm điện lưới cùng lúc. Thử relay với tải nhỏ trước.

## Giấy phép

Code của AIoT Việt trong repo này dùng giấy phép [MIT](LICENSE). Các file CMSIS của Arm trong `drivers/cmsis/` giữ giấy phép Apache-2.0 gốc (file `LICENSE.txt` / `LICENSE.md` đi kèm).

---

<a id="english"></a>

# AIoT Viet — IoT sample code & hardware test projects

This repository contains **sample code and hardware bring-up projects** from [AIoT Viet](https://en.aiotviet.com): ESP32, ESP8266, STM32 (RAK811 / WisNode-LoRa), LoRa, 4G, MQTT/EMQX.

- Every folder is a **self-contained project** that builds on its own, with a README covering wiring → flashing → running (README files are in Vietnamese).
- Several projects ship **host-side unit tests** (simulated sensors or a register-level simulator) — no board required.
- Core code is written product-style: no dynamic allocation, defensive error handling, and the ESP32 firmware is checked against **MISRA C:2012**.

> **This is sample code, not the commercial product.** The full system — ESP32/ESP8266 firmware with OTA updates, on-chip schedules and device self-registration; EMQX + Supabase backend; control & monitoring web app — is delivered to businesses **with full source code**.
> See it running: **[en.aiotviet.com](https://en.aiotviet.com)** · [Book a 30-minute demo](https://en.aiotviet.com/#dat-lich)

| Project | Hardware | What it does | Toolchain | Host tests |
|---|---|---|---|---|
| [`ESP32_relay_AC_x2_board`](ESP32_relay_AC_x2_board) | ESP32-WROOM-32E, ESP32 Relay AC X2 board | 2-relay control via local web page, button and MQTTS (EMQX) | ESP-IDF (PlatformIO), C99, MISRA C:2012 | ✔ GPIO/IO_MUX register simulator |
| [`02.Giam_Sat_Chat_Luong_Khong_Khi`](02.Giam_Sat_Chat_Luong_Khong_Khi) | ESP32 + ASAIR APM10 / ACD10 / AGS10 | Air quality: PM1.0/2.5/10, CO₂ (NDIR), TVOC | Arduino (PlatformIO) | ✔ simulated sensors |
| [`Node_giam_sat_kho_lanh`](Node_giam_sat_kho_lanh) | RAK811 (STM32L151 + SX1276) | Cold-storage node: temperature/humidity, door, battery voltage, LoRa point-to-point | Bare-metal CMSIS, arm-none-eabi-gcc | — |
| [`Test_RAK811_and_AM2301`](Test_RAK811_and_AM2301) | RAK811 + AM2301 (DHT21) | Temperature/humidity with signal diagnostics | Bare-metal CMSIS | ✔ driver tests |
| [`Gateway_giam_sat_kho_lanh`](Gateway_giam_sat_kho_lanh) | ESP32 (WiFi) · ESP32 + A7680C (4G) | Cold-storage gateway publishing to EMQX (MQTT/TLS), with on-chip simulated data | Arduino (PlatformIO) | — |
| [`Wemos_D1_ESP8266`](Wemos_D1_ESP8266) | Wemos D1 (ESP8266) | WiFi gateway, ESP32 project ported to ESP8266 | Arduino (PlatformIO) | — |

**Getting started:** ESP32/ESP8266 projects build with VS Code + PlatformIO; RAK811 projects use `setup_toolchain.ps1` then `build.ps1`. Copy `*.example.h` / `*.env.example` to real files for WiFi/MQTT credentials — the real files are git-ignored, never commit secrets.

**License:** AIoT Viet code in this repository is [MIT](LICENSE)-licensed. Arm CMSIS files under `drivers/cmsis/` keep their original Apache-2.0 license.
