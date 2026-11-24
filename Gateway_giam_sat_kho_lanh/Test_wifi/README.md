# Test gateway kho lạnh qua WiFi (ESP32) → EMQX → web app giám sát kho lạnh

Cùng chức năng với `Test_module_4G`, chỉ thay đường truyền 4G (A7680C) bằng **WiFi có sẵn trên ESP32**.
ESP32 đóng vai **gateway kho lạnh UG65-0FA31C**: tạo dữ liệu cảm biến giả lập (hoặc nhận từ PC qua USB),
gửi lên **EMQX Cloud (MQTT/TLS 8883)** đúng định dạng gateway Milesight mà web app
`E:\App-giám-sat-kho-lạnh` đang nhận:

```
cảm biến giả lập (ESP32)  ─┐
                           ├─► hàng đợi RAM ─► WiFi ─► EMQX  topic kholanh/<devEUI>/up
PC: "UP {json}" qua USB  ──┘    (600 bản tin)   MQTT QoS1  │ quy tắc EMQX → /api/uplink (Vercel)
                                                           ▼
                                             Supabase → web app (Tổng quan, Báo cáo…)
```

Giống bản 4G: trạng thái gateway gửi lên `kholanh/gw/ug65-0fa31c/status` (5 phút/lần, LWT `online:false`),
nhận lệnh ở `kholanh/gw/ug65-0fa31c/cmd` (`ping`, `status`, `sim_on`, `sim_off`, `reset_wifi`; trả lời ở `.../resp`).
Các topic này **không** khớp `kholanh/+/up` nên không lẫn vào dữ liệu app.

**Khác bản 4G:**
- Không cần phần cứng thêm: chỉ cần board ESP32 (ESP32-DevKit/WROOM) + cáp USB.
- MQTT dùng client tự viết `lib/MiniMqtt` (không tải thư viện ngoài): gửi **QoS1, chờ PUBACK**, chỉ xoá
  bản tin khỏi hàng đợi khi EMQX đã xác nhận nhận được.
- Giờ đo lấy qua **SNTP** (pool.ntp.org, time.google.com).
- Đổi WiFi lúc chạy bằng lệnh Serial `wifi set Ten|MatKhau` (lưu flash, không cần nạp lại).

## 1. Chuẩn bị

1. Sao chép `wifi.env.example` → `wifi.env`, điền tên + mật khẩu WiFi (**chỉ WiFi 2.4 GHz**; router 2 băng tần
   thì dùng tên mạng 2.4G). Có thể khai thêm mạng dự phòng `WIFI_SSID_2` (vd phát từ điện thoại).
2. Thông tin broker EMQX tự lấy từ `E:\App-giám-sat-kho-lạnh\cau-noi\.env` (MQTT_URL/USER/PASS) — không phải điền.

## 2. Build & nạp

Đã cài sẵn trên máy: Python 3.12, PlatformIO, Node, GCC (WinLibs).

```bash
tools/build.bat
```
- Tạo `include/secrets.h` (WiFi từ `wifi.env` + broker từ web app), build, xuất `release/firmware_merged_0x0.bin`.

```bash
tools/nap.bat COM5
```
Hoặc *Flash Download Tool* của Espressif: `release/firmware_merged_0x0.bin` @ `0x0`.
Hoặc VS Code + PlatformIO: `pio run -t upload` rồi `pio device monitor`.

> ⚠ `.bin`, `include/secrets.h`, `wifi.env` chứa **mật khẩu WiFi + MQTT** → không gửi lên mạng/Git (đã có trong `.gitignore`).
> Nên tạo **tài khoản MQTT riêng cho gateway** trên EMQX (Access Control → Authentication).

## 3. Chạy thử với web app

1. Trên app (Quản lý › Gateway) phải có gateway **UG65-0FA31C** và 5 thiết bị A01, A02, A03, B01, B03
   (có sẵn trong dữ liệu mẫu). Muốn dùng mã khác → sửa `CFG_GATEWAY_ID` trong `include/config.h` và thêm trên app.
2. **Tắt** `gateway-gia.mjs` và **không chạy song song gateway 4G** (cùng mã gateway, cùng cảm biến → trùng dữ liệu).
3. Nạp, mở Serial Monitor 115200. Log bình thường:
   ```
   [GW] ket noi WiFi "KhoLanh-2.4G"...
   [GW] WiFi OK "KhoLanh-2.4G" IP=192.168.1.50 RSSI=-58 dBm
   [GW] dong bo gio SNTP: 2026-10-10T04:00:00.000Z (UTC)
   [GW] ket noi MQTT xxxx.emqxsl.com:8883 (TLS)
   [GW] MQTT da ket noi. Hang doi: 5 ban tin
   ```
4. Mở web app → Tổng quan: các cảm biến A/B cập nhật mỗi 5 phút.
   Xem trực tiếp trên PC (chỉ đọc, không ghi gì vào app), giải mã bằng bộ giải mã của app:
   ```bash
   node tools/nghe-emqx.mjs
   node tools/nghe-emqx.mjs --lenh ping
   ```
5. Thử cảnh báo vượt ngưỡng: gõ `ep A01=12` ở Serial Monitor (bỏ ép: `ep A01=off`).

### Gửi dữ liệu giả lập từ PC qua ESP32 (thay cho dữ liệu tạo trên ESP32)
Dùng chung bộ mô phỏng của web app (`cau-noi/lib/mo-phong.mjs`), số liệu giống `gateway-gia.mjs` nhưng đi qua WiFi thật:
```bash
cd tools/pc-gui-du-lieu
node gui.mjs --liet-ke
node gui.mjs --cong COM5            # gửi ngay rồi 5 phút/lần; --mot-lan; --ep A01=12; --chu-ky 1
```
(Tắt Serial Monitor trước vì cổng COM chỉ mở được 1 chỗ. Tool tự gõ `sim off` cho ESP32.)

### Lệnh Serial Monitor
`help` · `status` · `wifi` · `wifi scan` · `wifi set Ten WiFi|matkhau` · `wifi xoa` · `reset_wifi` · `reboot` ·
`sim on|off` · `ep A01=12` · `UP {json}`

### Khi không vào được WiFi — đọc mã lý do trong log
| Mã | Nghĩa |
|---|---|
| 201 | Không thấy mạng: sai tên, ngoài vùng sóng, hoặc mạng chỉ có 5 GHz |
| 15 / 204 / 2 | Gần như chắc là **sai mật khẩu** |
| 202 | Sai mật khẩu / bị từ chối xác thực |
| 200 | Mất sóng (beacon timeout) – đặt gateway gần router hơn |
| 203 | Router từ chối (đầy thiết bị, lọc MAC…) – MAC in ở dòng đầu log |

## 4. Mô phỏng (đã chạy, tất cả ĐẠT)

Không cần ESP32 vẫn kiểm được: `test/sim` biên dịch **chính mã firmware** (`lib/MiniMqtt`, `lib/KhoLanh`)
cùng **router WiFi + Internet + broker EMQX giả lập** (broker phân tích từng byte MQTT mà firmware gửi), chạy thời gian ảo.

```bash
test/sim/run_sim.bat              # ~2 giây, mô phỏng tổng cộng ~10 ngày chạy
node tools/kiem-tra-ban-tin.mjs   # giải mã bản tin bằng CHÍNH bộ giải mã của web app
```
Kết quả đầy đủ: `test/sim/build/ket-qua-mo-phong.txt`. Bộ giải mã của app: **14 465/14 465 bản tin đúng**.

| Kịch bản | Kết quả |
|---|---|
| Bình thường 6 h | 381/381 bản tin, 0 trùng, 1 lần nối MQTT, khoảng lặng tối đa 4,9 phút |
| Router tắt 30 phút | 100% dữ liệu, gửi bù ngay khi có WiFi |
| WiFi chập chờn: 60 lần rớt 10–120 s trong 12 h | 783/783, 0 restart |
| Sai mật khẩu WiFi 40 phút rồi sửa bằng `wifi set` | log gợi ý “SAI MẬT KHẨU”, không spam (30 lần thử), 100% dữ liệu |
| Router mất Internet 1 h (WiFi vẫn có IP) | tự làm mới WiFi, nối lại ngay khi có mạng, 100% |
| EMQX sập 2 h | thử lại giãn dần tới 5 phút, 0 restart, 100% |
| Kết nối “chết im” 15 phút (gói tin mất, không ai báo) | phát hiện qua timeout PUBACK/ping, gửi bù 100% |
| Mất 3% PUBACK trong 12 h | 771/771 (21 bản gửi lại bị trùng — app tự chống trùng theo devEUI+fCnt) |
| Sai mật khẩu MQTT 7 h | 85 lần CONNECT/7 h (không spam), 0 reset WiFi, 0 restart, giữ đủ dữ liệu |
| Router tắt 4 h (quá ngưỡng 3 h) | quét thấy không có mạng → **không** restart (giữ dữ liệu), gửi bù 100% |
| WiFi của ESP32 bị kẹt (vẫn thấy mạng, không vào được) | restart ESP32 đúng 1 lần rồi chạy lại; mất phần đang đệm trong RAM |
| Lệnh xuống gateway (ping, status, lệnh lạ, payload 600 byte, sim_off/on, reset_wifi) | đúng |
| 12 bản tin JSON từ PC (lệnh UP), có rớt WiFi | 12/12 tới đúng topic, nguyên văn; từ chối JSON hỏng |
| **Hỗn loạn 7 ngày** (43 lần tắt router, 15 mất Internet, broker sập, 10 lần chết im, mất ACK, `millis()` tràn số) | 10 517/10 517 bản tin, 0 restart ESP32 |

Mọi kịch bản: **0 cấp phát động** trong vòng lặp firmware, broker không thấy lỗi giao thức MQTT nào.

## 5. Thiết kế cho chạy 24/7

- **Không treo**: mọi thao tác mạng có timeout (TLS handshake 20 s < watchdog 90 s); mọi vòng chờ ăn watchdog.
- **Không mất dữ liệu khi mất mạng**: hàng đợi 600 bản tin (~10 giờ với 5 cảm biến). Gửi QoS1, chỉ xoá khi
  có PUBACK; gửi bù đúng **thời điểm đo** → app ghi đúng mốc, tự chống trùng nếu gửi lại.
- **Phục hồi bậc thang**: nối lại MQTT (5 s → 5 phút) → sau 4 lần lỗi thì tắt/bật hẳn WiFi → mỗi mạng thử 2 lần
  rồi đổi sang mạng dự phòng → restart ESP32 nếu **3 giờ** không gửi được **và** restart có thể giúp
  (vẫn thấy mạng mà không vào được, hoặc có WiFi mà MQTT lỗi). Router tắt hẳn thì không restart (vô ích, mất dữ liệu).
  Broker từ chối user/pass thì chỉ thử lại thưa, không reset gì.
- **Phát hiện kết nối chết im**: timeout PUBACK 10 s, PINGREQ khi rảnh 30 s.
- **Không phân mảnh heap**: không dùng `String`/`malloc` trong vòng lặp; bộ đệm cố định, hàng đợi tĩnh.
- **`millis()` tràn số** sau 49,7 ngày: so sánh thời gian bằng phép trừ không dấu (đã mô phỏng).
- **TLS** tới EMQX Cloud có SNI. Mặc định mã hoá nhưng không xác thực chứng chỉ server (giống bản 4G);
  muốn xác thực: dán PEM CA của EMQX vào `SECRET_MQTT_CA_PEM`.

## 6. Rủi ro còn lại khi chạy thật (mô phỏng không thay được)

1. **Sóng WiFi trong/ cạnh kho lạnh**: vách panel cách nhiệt + cửa thép chắn sóng mạnh. Đặt gateway ngoài kho,
   RSSI nên > −70 dBm (xem `status`). Cảm biến trong kho đi LoRa, chỉ gateway cần WiFi.
2. **Router**: một số router chặn thiết bị lạ/ cổng 8883, hoặc có cổng đăng nhập (captive portal, WiFi khách sạn/
   quán) → ESP32 có IP nhưng không ra Internet. Log sẽ thấy MQTT lỗi “timeout” liên tục.
3. **Nguồn USB**: WiFi phát đỉnh ~300–500 mA; cổng USB/cáp yếu → `rst: brownout` trong bản tin status.
4. **Hàng đợi nằm trong RAM**: ESP32 mất điện/restart thì mất phần chưa gửi.
5. **Heap khi TLS nối lại nhiều lần**: mỗi lần nối TLS cấp phát ~40 KB (trong thư viện của ESP32). Theo dõi `min_heap`.

Nên chạy thử thật **≥ 72 giờ**, theo dõi `pub_fail`, `wifi_lost`, `wifi_rst`, `min_heap`, `rst` trong bản tin status.

## Cấu trúc

```
wifi.env                  (tự tạo từ wifi.env.example) tên/mật khẩu WiFi — KHÔNG đưa lên Git
include/config.h          chu kỳ, timeout, ngưỡng phục hồi, mã gateway
include/secrets.h         (tự tạo) WiFi + broker EMQX + user/pass — KHÔNG đưa lên Git
src/main.cpp              HAL ESP32 (WiFi, TLS, SNTP, NVS), watchdog, console USB, danh mục cảm biến
lib/MiniMqtt/             client MQTT 3.1.1 (QoS1, keepalive, LWT) + giao diện phần cứng WifiHal
lib/KhoLanh/              máy trạng thái gateway WiFi, hàng đợi, giả lập cảm biến, bản tin Milesight
test/sim/                 router/Internet/EMQX giả lập + 14 kịch bản
tools/                    build.bat, nap.bat, tao-secrets.mjs, kiem-tra-ban-tin.mjs, nghe-emqx.mjs, pc-gui-du-lieu/
release/                  file nạp (.bin) sau khi chạy build.bat
```
