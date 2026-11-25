# ESP32_Relay_AC_X2 – firmware thử nghiệm (ESP-IDF, C, MISRA C:2012)

Điều khiển 2 relay của bo **ESP32_Relay_AC_X2** (module **ESP32-WROOM-32E N4**) qua Wi-Fi:

- **Web cục bộ** – mở trình duyệt, bấm Bật/Tắt từng relay.
- **MQTTS tới EMQX Cloud** – gửi trạng thái JSON, nhận lệnh JSON (bước sau: EMQX → Vercel → Supabase,
  giống web app giám sát kho lạnh).
- **Nút BOOT** trên bo: nhấn ngắn = đảo Relay 1, nhấn giữ 1,5 s = đảo Relay 2.
- **Kiểm thử trên PC bằng bộ mô phỏng thanh ghi GPIO/IO_MUX của ESP32** – 330 phép kiểm, không cần bo mạch.

## 1. Phần cứng

| Chức năng | GPIO | Ghi chú |
|---|---|---|
| Relay 1 (ngoài cùng) | **GPIO16** | mức cao = hút (đổi trong menuconfig nếu bo của bạn ngược) |
| Relay 2 | **GPIO17** | |
| LED trạng thái | **GPIO23** | mức thấp = sáng |
| Nút BOOT | **GPIO0** | có điện trở kéo lên trên bo, nhấn = mức thấp |

Nguồn bo: AC 90–250 V **hoặc** DC 7–30 V (hoặc 5 V) trên các cầu đấu riêng. Tiếp điểm relay: COM/NO/NC,
tối đa 10 A – 250 VAC / 30 VDC.

> Không tìm thấy sơ đồ nguyên lý chính thức. Sơ đồ chân trên lấy từ trang ESPHome và wiki OctoPrint_SIOControl
> (hai nguồn khớp nhau); có một nguồn khác (Cirkit Designer) ghi GPIO26/27. **Hãy dò lại bằng đồng hồ / nhìn
> chữ in trên bo trước khi đấu tải.** Nếu khác, đổi trong `pio run -t menuconfig` → *ESP32 Relay X2*.

> ⚠️ **An toàn điện 220 V:** chỉ nạp firmware khi bo được cấp nguồn **qua cổng USB-TTL (5 V)**, **không** cắm
> điện lưới cùng lúc. Không chạm vào mặt dưới bo khi đang cấp AC. Thử relay với tải nhỏ (bóng đèn) trước.

### Nạp firmware

Bo không có cổng USB → dùng mạch USB-TTL (CP2102/CH340) **3,3 V**: TX→RX, RX→TX, GND→GND, 5V→5V.
Giữ nút **IO0/BOOT**, bấm **EN/RST** (hoặc rút/cắm lại nguồn USB-TTL), thả IO0 → bo vào chế độ nạp.

## 2. Cấu trúc project

```
components/relay_core/        Lõi C99 thuần, MISRA – KHÔNG phụ thuộc ESP-IDF
  include/esp32_regs.h        Địa chỉ thanh ghi GPIO/IO_MUX (theo TRM ESP32)
  include/reg_io.h            "Đường may" duy nhất chạm phần cứng
  src/gpio_drv.c              Driver GPIO mức thanh ghi (W1TS/W1TC, IO_MUX, GPIO matrix)
  src/relay.c                 2 relay: khởi động TẮT, chống đảo quá nhanh, đọc lại thanh ghi
  src/button.c, led_pattern.c Chống dội phím, nhấn ngắn/giữ; kiểu nháy LED
  src/cmd_parser.c            Parser lệnh JSON (dùng chung MQTT + HTTP), không đệ quy/không malloc
  src/status_json.c, str_buf.c Bản tin trạng thái JSON, bộ đệm chuỗi có giới hạn
  port/esp32/reg_io_esp32.c   Truy cập thanh ghi thật (volatile)
main/                         Glue ESP-IDF: Wi-Fi, web server, MQTTS, task nút/LED
  web/index.html              Trang điều khiển nhúng trong firmware
  app_secrets.example.h       Mẫu Wi-Fi / EMQX (copy thành app_secrets.h)
tests/host/                   Unit test chạy trên PC + bộ mô phỏng thanh ghi
tools/misra_check.sh          Kiểm tra MISRA bằng cppcheck
docs/MISRA.md                 Phạm vi, kết quả, các lệch chuẩn đã chấp thuận
```

## 3. Cấu hình

1. Lần build đầu tự tạo `main/app_secrets.h` từ mẫu. Mở ra điền:
   - `APP_WIFI_SSID`, `APP_WIFI_PASS` – Wi-Fi nhà (để trống = chỉ phát Wi-Fi riêng).
   - `APP_AP_PASS` – mật khẩu Wi-Fi riêng của thiết bị (≥ 8 ký tự, **đổi trước khi lắp thật**).
   - `APP_MQTT_URI`, `APP_MQTT_USER`, `APP_MQTT_PASS` – EMQX Cloud (để trống URI = tắt MQTT).
   - `APP_HTTP_TOKEN` – tuỳ chọn, khoá bảo vệ API web.
2. Chân, cực tính relay/LED, tiền tố topic, chu kỳ heartbeat: `pio run -t menuconfig` → **ESP32 Relay X2**.

`app_secrets.h` đã nằm trong `.gitignore`.

## 4. Build & nạp

```bash
pio run
```

```bash
pio run -t upload
```

```bash
pio device monitor
```

Log khởi động in `device_id` (vd `relay-a1b2c3`), địa chỉ AP và IP Wi-Fi nhà.
Đã build thử: Flash 37,8 % (767 KB / 2 MB), RAM tĩnh 10 %.

## 5. Dùng thử

**Qua Wi-Fi riêng:** nối điện thoại vào `ESP32-Relay-xxxxxx` (mật khẩu `APP_AP_PASS`) → mở `http://192.168.4.1`.
**Qua Wi-Fi nhà:** mở `http://<IP trong log>`.

LED: nháy nhanh = chưa có Wi-Fi nhà · nháy chậm = có Wi-Fi, chưa nối EMQX · sáng = đã nối EMQX.

API HTTP:

| Phương thức | Đường dẫn | Thân | Trả về |
|---|---|---|---|
| GET | `/api/status` | – | JSON trạng thái |
| POST | `/api/relay` | lệnh JSON (bảng dưới) | 200 + trạng thái · 400 lệnh sai · 401 sai khoá · 409 relay vừa đổi |

## 6. MQTT (EMQX Cloud, MQTTS cổng 8883)

TLS kiểm chứng chỉ bằng gói CA gốc có sẵn của ESP-IDF – không cần nạp CA riêng cho EMQX Cloud.
Tiền tố mặc định `relay2`:

| Topic | Chiều | QoS / retain | Nội dung |
|---|---|---|---|
| `relay2/<device_id>/state` | thiết bị → | 1 / **retain** | trạng thái; khi mất kết nối broker tự phát LWT `{"device":"…","online":false}` |
| `relay2/<device_id>/cmd` | → thiết bị | 1 | lệnh |
| `relay2/<device_id>/ack` | thiết bị → | 0 | `{"ok":true,"error":null}` hoặc `{"ok":false,"error":"busy"}` |

Lệnh (dùng chung cho MQTT và HTTP):

```json
{"relay":1,"state":"on"}
{"relay":2,"state":"off"}
{"relay":1,"state":"toggle"}
{"relay":2,"state":true}
{"query":"status"}
```

Trạng thái:

```json
{"device":"relay-a1b2c3","fw":"0.1.0","online":true,"relay1":true,"relay2":false,
 "seq":3,"uptime_s":125,"rssi":-61,"mqtt":true,"reason":"button"}
```

`reason`: `boot | connect | web | mqtt | button | heartbeat | query` · `seq` tăng mỗi lần relay đổi
(app web dùng để chống ghi trùng) · gửi khi đổi trạng thái, khi nối lại, và định kỳ 60 s.

Thử nhanh không cần app web: dùng **MQTTX** (client của EMQX) – subscribe `relay2/#`, publish lệnh vào `…/cmd`.

## 7. Kiểm thử trên PC (mô phỏng thanh ghi)

```bash
cmake -S tests/host -B tests/host/build -G Ninja
```

```bash
cmake --build tests/host/build
```

```bash
ctest --test-dir tests/host/build --output-on-failure
```

In nhật ký đọc/ghi từng thanh ghi của kịch bản đầy đủ (khởi động → nhấn nút → lệnh MQTT → nhấn giữ → JSON → LED):

```bash
tests/host/build/relay_tests --trace
```

Bộ mô phỏng (`tests/host/sim/reg_io_sim.c`) tái hiện hành vi phần cứng: `W1TS/W1TC` đặt/xoá bit, `GPIO_IN`
phản ánh mức pad theo `ENABLE`/`FUN_IE`/`MCU_SEL`, truy cập sai địa chỉ → fault, ép chập pad. Bảng IO_MUX
trong mô phỏng viết độc lập với driver (lấy từ TRM) để kiểm chéo. Những gì được kiểm:

- Cấu hình chân ra: push-pull, `FUNCn_OUT_SEL = 0x100`, `MCU_SEL = 2`, giữ nguyên sức lái/bit kéo.
- **Chốt mức trước khi bật lái ra** → relay không giật lúc khởi động (cả active-high và active-low).
- Chỉ ghi qua `W1TS/W1TC`, không đọc-sửa-ghi `GPIO_OUT`; không đụng bit của chân khác.
- Chặn GPIO flash (6–11), UART0 (1, 3), chân không tồn tại.
- Relay: chống đảo nhanh hơn 300 ms, đúng khi bộ đếm ms tràn vòng; tự kiểm thanh ghi phát hiện lệch.
- Nút: chống dội, nhấn ngắn/giữ, giữ lúc khởi động bị bỏ qua.
- Parser: 40+ trường hợp lệnh hỏng / biên / tràn.

Kiểm tra MISRA:

```bash
bash tools/misra_check.sh
```

Kết quả và lệch chuẩn: [docs/MISRA.md](docs/MISRA.md).

## 8. Bước tiếp theo

1. Nạp thử lên bo, kiểm relay bằng web cục bộ.
2. Tạo deployment EMQX Cloud, điền `app_secrets.h`, kiểm bằng MQTTX.
3. Làm app web (Next.js trên Vercel + Supabase): quy tắc EMQX chuyển tiếp
   `relay2/+/state` tới `POST /api/…` → lưu Supabase; app gửi lệnh qua EMQX REST API vào `…/cmd`.
4. Tuỳ chọn: OTA, lưu/khôi phục trạng thái relay (mặc định hiện tại: luôn TẮT khi khởi động – an toàn hơn),
   cấu hình Wi-Fi qua trang web thay vì sửa `app_secrets.h`.

## Nguồn tham khảo phần cứng

- [ESPHome – ESP32 Relay x2](https://devices.esphome.io/devices/esp32-relay-x2)
- [OctoPrint_SIOControl – ESP32WRM Relay X2 Board Info](https://github.com/jcassel/OctoPrint_SIOControl_Firmware/wiki/ESP32WRM-Relay-X2-Board-Info)
- [Blakadder – LC Technology ESP32_Relay_X2](https://templates.blakadder.com/ESP32_Relay_X2.html)
- ESP32 Technical Reference Manual – chương IO_MUX & GPIO Matrix
