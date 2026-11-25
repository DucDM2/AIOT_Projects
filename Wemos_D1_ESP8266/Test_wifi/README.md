# Test gateway kho lạnh qua WiFi (Wemos D1 – ESP8266) → EMQX → web app giám sát kho lạnh

Bản chuyển từ `Gateway_giam_sat_kho_lanh/Test_wifi` (ESP32) sang **Wemos D1 (wemos.cc, bản 2015,
dạng Arduino UNO, ESP-12 / ESP8266, flash 4 MB, USB CH340)**. Cùng chức năng: board đóng vai **gateway kho lạnh
UG65-0FA31C**, tạo dữ liệu cảm biến giả lập (hoặc nhận từ PC qua USB), gửi lên **EMQX Cloud (MQTT/TLS 8883)** đúng
định dạng gateway Milesight mà web app giám sát kho lạnh của AIoT Việt (không nằm trong repo này) đang nhận:

```
cảm biến giả lập (ESP8266) ─┐
                            ├─► hàng đợi RAM ─► WiFi ─► EMQX  topic kholanh/<devEUI>/up
PC: "UP {json}" qua USB  ───┘  (300 bản tin)   MQTT QoS1  │ quy tắc EMQX → /api/uplink (Vercel)
                                                          ▼
                                            Supabase → web app (Tổng quan, Báo cáo…)
```

Trạng thái gateway gửi lên `kholanh/gw/ug65-0fa31c/status` (5 phút/lần, thêm trường `"hw":"esp8266"`, LWT `online:false`),
nhận lệnh ở `kholanh/gw/ug65-0fa31c/cmd` (`ping`, `status`, `sim_on`, `sim_off`, `reset_wifi`; trả lời ở `.../resp`).
Các topic này **không** khớp `kholanh/+/up` nên không lẫn vào dữ liệu app.

**Đèn LED xanh trên board (GPIO2):** sáng liên tục = đã nối EMQX; nhấp nháy = đang vào WiFi / nối MQTT.

## Khác bản ESP32 (vì ESP8266 chỉ có ~80 KB RAM)

| | ESP32 | Wemos D1 (ESP8266) |
|---|---|---|
| Hàng đợi khi mất mạng | 600 bản tin (~10 giờ) | **300 bản tin (~5 giờ)** + 6 bản tin JSON từ PC |
| TLS | mbedTLS | BearSSL, **bộ đệm 1 KB nhờ MFLN** (thay vì 16 KB) |
| Lưu WiFi đổi bằng `wifi set` | NVS | EEPROM giả lập trong flash |
| Watchdog | 90 s (tự đặt) | có sẵn của chip: ~3 s phần mềm / ~8 s phần cứng |
| CPU | 240 MHz | 160 MHz (đã đặt trong `platformio.ini`) |

**MFLN** (giới hạn kích thước gói TLS) là điều kiện để TLS chạy được trên ESP8266. Đã kiểm với broker thật
`eca17a2a…emqxsl.com:8883` (chỉ bắt tay TLS, không gửi dữ liệu): EMQX trả lời MFLN 512/1024, và chạy lại
**đúng từng byte** hàm dò `probeMaxFragmentLength` của core ESP8266 cũng ra kết quả "có".
Log khi nối sẽ có dòng `[NET] broker ho tro MFLN -> bo dem TLS 1 KB`.

## 1. Chuẩn bị

1. `wifi.env` đã được chép sẵn từ bản ESP32 (cùng mạng WiFi). Muốn đổi thì sửa file này
   (**chỉ WiFi 2.4 GHz, WPA2**; router 2 băng tần thì dùng tên mạng 2.4G). Có thể khai thêm mạng dự phòng `WIFI_SSID_2`.
2. Thông tin broker EMQX: chép `include/secrets.example.h` thành `include/secrets.h` rồi điền host, user, mật khẩu MQTT của **deployment EMQX của bạn** (file `secrets.h` đã nằm trong `.gitignore`).
3. Driver USB: Wemos D1 dùng chip **CH340**. Cắm vào mà Device Manager không thấy cổng COM → cài driver CH340.

## 2. Build & nạp

```bash
tools/build.bat
```
Tạo `include/secrets.h` (WiFi từ `wifi.env` + broker từ web app), build, xuất `release/firmware_esp8266_0x0.bin`.

```bash
tools/nap.bat COM5
```
Hoặc VS Code + PlatformIO: `pio run -t upload` rồi `pio device monitor`.
Nạp lỗi → đóng mọi Serial Monitor đang mở cổng, đổi cáp USB (cáp có dây dữ liệu), hoặc hạ tốc độ trong `nap.bat`.

> ⚠ `.bin`, `include/secrets.h`, `wifi.env` chứa **mật khẩu WiFi + MQTT** → không gửi lên mạng/Git (đã có trong `.gitignore`).

## 3. Chạy thử với web app

1. Trên app (Quản lý › Gateway) có gateway **UG65-0FA31C** và 5 thiết bị A01, A02, A03, B01, B03 (có sẵn trong dữ liệu mẫu).
2. **Chỉ chạy MỘT gateway mang mã UG65-0FA31C**: tắt `gateway-gia.mjs`, rút bản ESP32 (4G/WiFi) → nếu không sẽ trùng dữ liệu.
3. Nạp, mở Serial Monitor 115200 (vài dòng ký tự rác lúc khởi động là bình thường với ESP8266). Log bình thường:
   ```
   === Gateway kho lanh WiFi (Wemos D1 / ESP8266) ===
   reset: poweron | heap ~30000 (block ...) | gateway UG65-0FA31C | broker ....emqxsl.com:8883
   [GW] ket noi WiFi "..."...
   [GW] WiFi OK "..." IP=192.168.1.50 RSSI=-58 dBm
   [GW] dong bo gio SNTP: 2026-10-10T04:00:00.000Z (UTC)
   [GW] ket noi MQTT ....emqxsl.com:8883 (TLS)
   [NET] broker ho tro MFLN -> bo dem TLS 1 KB
   [GW] MQTT da ket noi. Hang doi: 5 ban tin
   ```
4. Web app → Tổng quan: các cảm biến A/B cập nhật mỗi 5 phút. Xem trực tiếp trên PC (chỉ đọc):
   ```bash
   node tools/nghe-emqx.mjs
   node tools/nghe-emqx.mjs --lenh ping
   ```
5. Thử cảnh báo vượt ngưỡng: gõ `ep A01=12` ở Serial Monitor (bỏ ép: `ep A01=off`).
6. Gõ `status` để xem RAM: dòng `ESP8266 : heap lien tuc lon nhat ...` — nên luôn > 8 000 byte.

### Gửi dữ liệu giả lập từ PC qua board

> ⚠️ `tools/pc-gui-du-lieu` và `tools/nghe-emqx.mjs` dùng thư viện mô phỏng của web app giám sát kho lạnh
> (không nằm trong repo này) nên chỉ chạy được trong bộ chuyển giao đầy đủ. Firmware chạy độc lập với dữ liệu
> giả lập có sẵn trên board; có thể kiểm bản tin trên EMQX bằng MQTTX.

```bash
cd tools/pc-gui-du-lieu
node gui.mjs --liet-ke
node gui.mjs --cong COM5 --mot-lan
```

### Lệnh Serial (115200, Enter)
`help` · `status` · `wifi` · `wifi scan` · `wifi set Ten|MatKhau` · `wifi xoa` · `reset_wifi` · `reboot` ·
`sim on|off` · `ep A01=12` · `ep A01=off` · `UP {json}`

## 4. Đã kiểm tra trên PC (trước khi chạy mạch thật)

| Kiểm tra | Kết quả |
|---|---|
| Build firmware ESP8266 (`-Wall -Wextra`, lỗi format = lỗi build) | 0 cảnh báo; RAM tĩnh 49,4 KB/80 KB, flash 40 % |
| Mô phỏng 14 kịch bản lỗi mạng (`test/sim/run_sim.bat`, cùng kích thước hàng đợi ESP8266) | **14/14 ĐẠT**, 0 cấp phát động trong vòng lặp |
| Giải mã bằng chính bộ giải mã của app (`node tools/kiem-tra-ban-tin.mjs`) | **14 318/14 318** bản tin đúng |
| EMQX thật có hỗ trợ MFLN (openssl + tái hiện hàm dò của ESP8266) | Có (512 và 1024) |
| Bắt tay TLS với đúng bộ mã BearSSL (ChaCha20 / AES-GCM, chữ ký RSA-PKCS1) | OK |

Kịch bản "Sai mật khẩu MQTT 7 giờ": vượt sức chứa 5 giờ nên bỏ bản tin cũ nhất (đúng thiết kế, có log).

## 5. Rủi ro còn lại khi chạy thật (mô phỏng không thay được)

1. **Heap**: ước tính lúc chạy còn ~15 KB trống. Nếu log báo `do MFLN that bai ... 16 KB` lặp lại hoặc `TLS ... loi`
   kèm `block` < 20 000 → gửi log cho mình. Theo dõi `min_heap` trong bản tin status.
2. **Sóng WiFi**: ăng-ten PCB của ESP-12 yếu hơn ESP32 DevKit; RSSI nên > −70 dBm. Đặt board ngoài kho lạnh.
3. **Nguồn**: cấp qua USB máy tính là đủ; dùng jack DC thì 7–12 V. Nguồn yếu → reset lý do `hw_wdt`/`exception`.
4. **Hàng đợi nằm trong RAM**: mất điện/khởi động lại thì mất phần chưa gửi.
5. Bắt tay TLS trên ESP8266 mất ~1–3 s mỗi lần nối lại — bình thường.

Nên chạy thử thật **≥ 24–72 giờ**, theo dõi `pub_fail`, `wifi_lost`, `min_heap`, `rst` trong bản tin status.

## Cấu trúc

```
wifi.env                  tên/mật khẩu WiFi — KHÔNG đưa lên Git
include/config.h          chu kỳ, timeout, ngưỡng phục hồi, mã gateway, kích thước bộ đệm TLS
include/secrets.h         (tự tạo) WiFi + broker EMQX + user/pass — KHÔNG đưa lên Git
src/main.cpp              HAL ESP8266 (WiFi, TLS BearSSL + MFLN, SNTP, EEPROM, LED), console USB, danh mục cảm biến
lib/MiniMqtt/             client MQTT 3.1.1 (QoS1, keepalive, LWT) — dùng chung với bản ESP32
lib/KhoLanh/              máy trạng thái gateway, hàng đợi, giả lập cảm biến, bản tin Milesight — dùng chung
test/sim/                 router/Internet/EMQX giả lập + 14 kịch bản
tools/                    build.bat, nap.bat, tao-secrets.mjs, kiem-tra-ban-tin.mjs, nghe-emqx.mjs, pc-gui-du-lieu/
```
