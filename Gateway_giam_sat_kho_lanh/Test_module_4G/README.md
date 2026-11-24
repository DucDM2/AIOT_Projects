# Test module 4G A7680C + ESP32 → EMQX → web app giám sát kho lạnh

ESP32 đóng vai **gateway kho lạnh**: tạo dữ liệu cảm biến giả lập (hoặc nhận từ PC qua USB), gửi qua
**4G (A7680C)** lên **EMQX Cloud (MQTT/TLS 8883)** đúng định dạng gateway Milesight mà web app
`E:\App-giám-sat-kho-lạnh` đang nhận:

```
cảm biến giả lập (ESP32)  ─┐
                           ├─► hàng đợi RAM ─► A7680C ─4G─► EMQX  topic kholanh/<devEUI>/up
PC: "UP {json}" qua USB  ──┘    (600 bản tin)                      │ quy tắc EMQX → /api/uplink (Vercel)
                                                                   ▼
                                                     Supabase → web app (Tổng quan, Báo cáo…)
```

Gateway còn gửi trạng thái lên `kholanh/gw/ug65-0fa31c/status` (mỗi 5 phút; LWT `online:false` khi rớt mạng)
và nhận lệnh ở `kholanh/gw/ug65-0fa31c/cmd` (`ping`, `status`, `http`, `reset_modem`, `sim_on`, `sim_off`;
trả lời ở `.../resp`). Các topic này **không** khớp `kholanh/+/up` nên không lẫn vào dữ liệu app.

## 1. Đấu dây

| ESP32 | Module A7680C | Ghi chú |
|---|---|---|
| GPIO16 (RX2) | TXD | |
| GPIO17 (TX2) | RXD | |
| GPIO4 | PWRKEY | mặc định nối thẳng, ESP32 kéo LOW để “nhấn” (open-drain) |
| GND | GND | **bắt buộc chung GND** |
| — | VIN 5V / VBAT | nguồn **riêng ≥ 2 A** |

**Phải kiểm tra trên board của bạn:**
- **Nguồn**: A7680C có 2G dự phòng, đỉnh dòng tới ~2 A. Không cấp từ chân 3V3/5V của ESP32 qua USB.
  Dùng adapter 5V-2A (board có mạch hạ áp) hoặc nguồn 3.8–4.2 V/2 A vào VBAT, tụ ≥ 470 µF sát module.
  Nguồn yếu = modem reset ngẫu nhiên khi phát (log sẽ thấy “modem tự khởi động lại”).
- **Mức logic UART**: chip A7680C dùng mức 1.8 V. Hầu hết board bán sẵn đã có mạch chuyển mức 3.3 V;
  nếu dùng **module trần** thì phải thêm mạch chuyển mức.
- **PWRKEY**: nếu board có transistor NPN ở chân PWRKEY (nhấn = kéo lên HIGH) thì đặt
  `MODEM_PWRKEY_VIA_TRANSISTOR 1` trong `include/config.h`. Nếu board tự bật khi cấp nguồn thì
  để nguyên — firmware thấy modem trả lời AT sẽ không nhấn PWRKEY.
- **Khuyên dùng cho chạy 24/7**: thêm MOSFET cắt nguồn module, khai `PIN_MODEM_POWER`. Đây là cách
  duy nhất cứu được khi modem treo cứng không nhận PWRKEY (xem kết quả mô phỏng bên dưới).
- Gắn **anten LTE**; SIM đã tắt mã PIN, còn data.

## 2. Build & nạp

Đã cài sẵn trên máy: Python 3.12, PlatformIO, GCC (WinLibs).

```bash
tools/build.bat
```
- Tự tạo `include/secrets.h` từ `E:\App-giám-sat-kho-lạnh\cau-noi\.env` (MQTT_URL/USER/PASS của EMQX),
  build, xuất `release/firmware_merged_0x0.bin` (1 file, nạp ở địa chỉ `0x0`).

```bash
tools/nap.bat COM5
```
Hoặc dùng *Flash Download Tool* của Espressif: chọn `release/firmware_merged_0x0.bin` @ `0x0`.
Hoặc VS Code + PlatformIO: `pio run -t upload` rồi `pio device monitor`.

> ⚠ File `.bin` và `include/secrets.h` chứa **mật khẩu MQTT** → không gửi lên mạng/Git (đã có trong `.gitignore`).
> Nên tạo **tài khoản MQTT riêng cho gateway** trên EMQX (Access Control → Authentication).

## 3. Chạy thử với web app

1. Trên app (Quản lý › Gateway) phải có gateway **UG65-0FA31C** và 5 thiết bị A01, A02, A03, B01, B03
   (đã có sẵn trong dữ liệu mẫu). Muốn dùng mã gateway khác → sửa `CFG_GATEWAY_ID` và thêm trên app.
2. **Tắt** `gateway-gia.mjs` (nếu đang chạy) để không gửi trùng cùng cảm biến.
3. Nạp, mở Serial Monitor 115200. Log bình thường:
   ```
   [GW] da dang ky mang: Viettel, CSQ=20 (-73 dBm)
   [GW] PDP OK, IP=10.x.x.x
   [GW] dong bo gio: 2026-10-10T04:00:00.000Z (UTC)
   [GW] MQTT da ket noi. Hang doi: 5 ban tin
   ```
4. Mở web app → Tổng quan: các cảm biến A/B cập nhật mỗi 5 phút. Theo dõi trực tiếp bằng MQTTX:
   subscribe `kholanh/#`.
5. Thử cảnh báo vượt ngưỡng: gõ `ep A01=12` ở Serial Monitor (bỏ ép: `ep A01=off`).

### Gửi dữ liệu giả lập từ PC qua ESP32 (thay cho dữ liệu tạo trên ESP32)
Dùng **chung bộ mô phỏng của web app** (`cau-noi/lib/mo-phong.mjs`), số liệu giống `gateway-gia.mjs`
nhưng đi qua đường 4G thật:
```bash
cd tools/pc-gui-du-lieu
node gui.mjs --liet-ke
node gui.mjs --cong COM5            # gửi ngay rồi 5 phút/lần; --mot-lan; --ep A01=12; --chu-ky 1
```
(Tắt Serial Monitor trước vì cổng COM chỉ mở được 1 chỗ. Tool tự gõ `sim off` cho ESP32.)

### Lệnh Serial Monitor
`help` · `status` · `at AT+CPSI?` (gửi lệnh AT bất kỳ) · `trace on|off` (xem toàn bộ AT) · `http` ·
`reset_modem` · `reboot` · `sim on|off` · `ep A01=12` · `UP {json}`

## 4. Mô phỏng (đã chạy, tất cả ĐẠT)

Không có modem thật vẫn kiểm được: `test/sim` biên dịch **chính mã firmware** (`lib/A7680`, `lib/KhoLanh`)
cùng một **A7680C giả lập** (trả lời AT theo tài liệu A76XX, có mạng và broker giả, bơm lỗi), chạy thời gian ảo.

```bash
test/sim/run_sim.bat          # ~10 giây, mô phỏng tổng cộng ~11 ngày chạy
node tools/kiem-tra-ban-tin.mjs   # giải mã bản tin bằng CHÍNH bộ giải mã của web app
```
Kết quả đầy đủ: `test/sim/build/ket-qua-mo-phong.txt`.

| Kịch bản | Kết quả |
|---|---|
| Bình thường 6 h | 381/381 bản tin, 0 trùng, 0 reset, khoảng lặng tối đa 4,1 phút |
| Modem đã bật sẵn + echo bật | không tắt/khởi động lại modem đang chạy tốt |
| Mất sóng 30 phút / 5 phút / 90 s (có lần mất im lặng, không có URC) | 100% dữ liệu, gửi bù khi có mạng |
| EMQX sập 2 giờ | đệm 2 giờ dữ liệu, gửi bù 100%, không restart ESP32 |
| Modem treo cứng 2 lần | tự cấp nguồn lại, 100% dữ liệu |
| Modem treo, không nhận PWRKEY, **có** MOSFET | tự phục hồi, 100% |
| Modem treo, không nhận PWRKEY, **không** MOSFET | không thể tự cứu: thử lại thưa dần, restart ESP32 tối đa 1 lần/3 giờ, mất dữ liệu trong thời gian chết; tự lên lại khi modem được cấp điện lại |
| Modem tự khởi động lại 4 lần | phát hiện qua URC, cấu hình lại, 100% |
| Không có SIM 1 giờ | cấp nguồn lại thưa dần (9 lần/giờ), lên mạng ngay khi lắp SIM |
| Sai mật khẩu MQTT 7 giờ | không spam broker (84 lần thử/7 h), không reset modem vô ích, giữ dữ liệu |
| Lệnh xuống gateway (ping, payload 600 byte có CRLF, http, reset) | đúng |
| UART nhiễu: 5% rác, 2% dòng dài, 1% mất phản hồi, 1% CME ERROR — 12 giờ | 100% |
| Bản tin JSON từ PC, có mất sóng | 12/12 tới đúng topic, nguyên văn |
| **Hỗn loạn 7 ngày** (35 lần mất sóng, 7 lần broker sập, treo/reboot, nhiễu, `millis()` tràn số) | 10 562/10 562 bản tin, 0 restart ESP32 |

Mọi kịch bản: **0 cấp phát động** trong vòng lặp firmware (không phân mảnh heap khi chạy lâu), không có vòng lặp bận.

## 5. Thiết kế cho chạy 24/7

- **Không treo**: mọi lệnh AT có timeout; mọi vòng chờ gọi `idle()` có cho ăn watchdog (90 s).
  Bộ đọc AT xử lý URC xen giữa, echo, dấu nhắc `>`, payload nhị phân theo độ dài, cắt dòng quá dài,
  huỷ đọc khi dữ liệu thiếu.
- **Không phân mảnh heap**: không dùng `String`/`malloc`, buffer cố định; hàng đợi tĩnh.
- **`millis()` tràn số** sau 49,7 ngày: mọi so sánh thời gian dùng phép trừ không dấu.
- **Phục hồi bậc thang**: kết nối lại MQTT (backoff 5 s → 5 phút) → kích hoạt lại PDP → CFUN 0/1 →
  cấp nguồn lại modem (giãn cách 30 s → 15 phút) → restart ESP32 nếu **3 giờ** không gửi được gì.
  Broker từ chối (sai user/pass) thì chỉ thử lại thưa, không reset modem.
- **Không mất dữ liệu khi mất mạng**: hàng đợi 600 bản tin (~10 giờ với 5 cảm biến), gửi bù đúng
  **thời điểm đo** (giờ lấy từ mạng/NTP) → app ghi đúng mốc, tự chống trùng nếu gửi lại.
- **TLS** tới EMQX Cloud (bắt buộc với Serverless), SNI bật. Có thể nạp CA để xác thực server:
  dán PEM vào `SECRET_MQTT_CA_PEM`.

## 6. Rủi ro còn lại khi chạy thật (mô phỏng không thay được)

1. **Nguồn điện** – nguyên nhân số 1 gây lỗi 4G. Đo sụt áp lúc modem đăng ký mạng/phát.
2. **SNI/TLS trên firmware module**: EMQX Serverless cần SNI. Nếu log báo `CMQTTCONNECT` lỗi 32 (handshake)
   hoặc lỗi kết nối liên tục, chạy `at AT+CGMR` và cập nhật firmware A7680C, hoặc thử `at AT+CSSLCFG="enableSNI",0,1`.
3. **Mã lỗi MQTT** lấy theo tài liệu A76XX (27–31 = broker từ chối). Nếu khác trên firmware thực tế
   thì gateway vẫn phục hồi theo nhánh chung, chỉ kém tối ưu.
4. **Hàng đợi nằm trong RAM**: ESP32 mất điện/restart thì mất phần chưa gửi.
5. **Chân GPIO**: khi ghép LoRa (SPI) + Ethernet vào gateway thật, kiểm tra không trùng GPIO4/16/17.

Nên chạy thử thật **≥ 72 giờ**, theo dõi `pub_fail`, `hard_rst`, `reboots`, `min_heap` trong bản tin status.

## Cấu trúc

```
include/config.h          chân nối, APN, chu kỳ, ngưỡng phục hồi
include/secrets.h         (tự tạo) broker EMQX + user/pass — KHÔNG đưa lên Git
src/main.cpp              HAL ESP32, watchdog, console USB, danh mục cảm biến
lib/A7680/                driver AT A7680C (MQTT/TLS/HTTP/PDP/nguồn), không phụ thuộc Arduino
lib/KhoLanh/              máy trạng thái gateway, hàng đợi, giả lập cảm biến, bản tin Milesight
test/sim/                 modem A7680C giả lập + 15 kịch bản
tools/                    build.bat, nap.bat, tao-secrets.mjs, kiem-tra-ban-tin.mjs, pc-gui-du-lieu/
release/                  file nạp (.bin)
```
