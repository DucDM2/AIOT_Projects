# Test RAK811 (WisNode-LoRa) + cảm biến AM2301 (DHT21)

Firmware bare-metal (chỉ dùng thanh ghi CMSIS, không cần STM32 HAL) cho module **RAK811**
(STM32L151CBU6) trên board **WisNode-LoRa**. Firmware đọc **AM2301** (nhiệt độ −40…80 °C ±0.5 °C,
độ ẩm 0…99.9 %RH ±3 %RH) mỗi 3 giây và in kết quả qua cổng USB (CH340), kèm thông tin chẩn đoán tín hiệu.

Dựa trên project `Node_giam_sat_kho_lanh/Test_nhiet_do&do_am` (dùng lại clock, UART, watchdog,
startup, linker), thay DS18B20 + SHT31 bằng driver AM2301 mới. Datasheet: `docs/AM2301_Product_Manual.pdf`.

**Một file firmware dùng chung cho RAK811(H) và RAK811(L)**, vì chân PB5 giống nhau ở hai bản.

```
=== Test RAK811 (WisNode-LoRa) + AM2301 (DHT21) ===
Firmware dung chung RAK811(H) va RAK811(L)
Reset do  : cap nguon (POR)
Chan SDA  : PB5 (header P3-9), can pull-up 4.7k-5.1k len 3.3V
Self-test giai ma (vi du datasheet): OK
Chu ky doc: 3 s | phim: d = chi tiet xung, s = thong ke
Cho cam bien on dinh sau cap nguon...
[2 s] AM2301: T=25.0 C, RH=60.0 %RH  (lan dau: gia tri lan do truoc -> bo qua, khong tinh thong ke)
[5 s] AM2301: T=25.1 C, RH=59.8 %RH
```

---

## 1. Đấu dây

AM2301 có 3 dây. Cấp **3.3 V**, không dùng 5 V.

| Dây AM2301 | Nối vào WisNode-LoRa |
|---|---|
| Đỏ (VDD) | P3-10 (VCC_3V3) |
| Đen (GND) | GND (vd. P5-1) |
| Vàng (SDA) | P3-9 (**PB5**) |
| — | **Điện trở 4.7 kΩ – 5.1 kΩ nối giữa dây vàng (SDA) và 3.3 V** |

Ghi chú:
- **Bắt buộc có điện trở kéo lên.** Datasheet khuyên dùng 5.1 kΩ; 4.7 kΩ cũng được. Pull-up nội của STM32 (~40 kΩ) quá yếu, chỉ để dự phòng.
- **Cấp 3.3 V, không dùng 5 V.** Nếu cấp 5 V, SDA sẽ lên mức 5 V khi cảm biến kéo bus. Datasheet cho phép cấp 3.3 V; firmware không cần đổi gì.
- **Ở 3.3 V, dây nối không quá 1 m** (datasheet mục 5.1). Dây zin của cảm biến dài 24 cm nên dùng được ngay.
- **Đừng cắm chung với DS18B20 trên PB5.** Hai giao thức khác nhau và không chia sẻ bus được.

## 2. Build

Yêu cầu: Windows + PowerShell. `build.ps1` tự tìm toolchain ARM GCC đã có ở
`Node_giam_sat_kho_lanh\Test_nhiet_do&do_am\tools\`, nên không cần tải lại.

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

Kết quả: `build/rak811_am2301.hex` (cũng có `.bin` và `.elf`), dung lượng khoảng 5 KB flash.

Nếu máy chưa có toolchain, chạy `setup_toolchain.ps1` một lần.

## 3. Nạp firmware

> ⚠️ Nạp firmware này sẽ **xoá firmware AT/LoRaWAN gốc của RAK**.

**Cách A — qua USB (UART bootloader):**
1. Jumper **P1** ở vị trí nối UART1 với CH340.
2. Chuyển jumper **P2 (BOOT0)** sang **VCC_3V3**, cắm USB, nhấn **RESET**.
3. Mở **STM32CubeProgrammer** và chọn **UART**, cổng COM của CH340, baud **115200**, parity **Even**. Bấm **Connect**.
4. Chọn `build/rak811_am2301.hex` rồi bấm **Download**.
5. Đưa **P2 (BOOT0)** về **GND**, nhấn **RESET**.

**Cách B — qua ST-Link (header P4):**

```powershell
STM32_Programmer_CLI -c port=SWD -w build/rak811_am2301.hex -v -rst
```

## 4. Xem kết quả và kiểm tra

Mở cổng COM của CH340 bằng PuTTY, Tera Term hoặc Arduino Serial Monitor: **115200 baud, 8N1**. Nhấn RESET để xem banner.

| Phím | Tác dụng |
|---|---|
| `d` | Bật/tắt in độ dài từng loại xung mỗi lần đọc |
| `s` | In bảng thống kê ngay (tự in mỗi 20 lần đọc) |

**Quy trình kiểm tra trên mạch thật:**
1. `Self-test giai ma ... OK` → bộ giải mã đúng với các ví dụ trong datasheet.
2. Số đo hợp lý so với nhiệt kế/ẩm kế trong phòng. Sai số cho phép: ±0.5 °C, ±3 %RH.
3. Bấm `d` và so độ dài xung với datasheet (dòng in ngay bên dưới). Lệch vài µs là bình thường.
4. Thở vào cảm biến hoặc nắm tay vào: RH và T phải tăng sau 1–2 lần đọc.
5. Để chạy 5–10 phút rồi bấm `s`. Tỉ lệ OK nên là **100 %**; thỉnh thoảng có 1 lần lỗi vẫn chấp nhận được.
6. Rút dây vàng ra rồi cắm lại → phải báo `khong co tra loi`, sau đó `[hoi phuc sau N lan loi]`.

**Lưu ý: AM2301 luôn trả về kết quả của lần đo TRƯỚC.**
- Mỗi lần đọc sẽ kích hoạt một lần đo mới (datasheet mục 5.3).
- Vì vậy giá trị đầu tiên sau khi bật nguồn bị bỏ qua.
- Khi môi trường thay đổi, số đo hiện ra chậm hơn một lần đọc (3 s).

## 5. Khắc phục sự cố

| Thông báo | Nguyên nhân thường gặp |
|---|---|
| `khong co tra loi` | Thiếu nguồn hoặc GND, sai chân (phải là P3-9), dây lỏng |
| `SDA bi keo thap` | SDA chập GND, đấu ngược dây đỏ/đen, cảm biến treo (rút nguồn cảm biến rồi cắm lại) |
| `xung tra loi sai do dai` / `xung bit sai do dai` | Thiếu pull-up hoặc pull-up quá yếu, dây dài, nhiễu |
| `mat tin hieu giua khung du lieu` | Tiếp xúc chập chờn, nhiễu mạnh |
| `sai checksum` | Nhiễu; nếu xảy ra thường xuyên thì giảm pull-up xuống 4.7 kΩ và rút ngắn dây |
| `khung du lieu toan 0` / `gia tri ngoai dai` | Cảm biến lỗi hoặc hàng nhái kém chất lượng |
| `[cam bien gui T am dang bu 2, da tu xu ly]` | Một số bản nhái DHT21 mã hoá nhiệt độ âm khác datasheet. Firmware tự nhận ra, số đo vẫn đúng |
| Không thấy gì trên COM | BOOT0 còn ở VCC, jumper P1 sai vị trí, sai baud |

Watchdog (IWDG ~28 s) tự reset nếu firmware bị treo.

## 6. Kiểm thử đã chạy (trước khi nạp mạch thật)

### 6.1 Test driver trên PC — `test/test_am2301.c`

Biên dịch **đúng file `src/am2301.c`** với một lớp chân/đồng hồ giả lập. Thời gian chạy theo đồng hồ ảo
(16 tick/µs). Mỗi lần đọc chân tốn thời gian như CPU thật, và có ngắt SysTick chen vào.
Mô hình cảm biến tạo dạng sóng theo Bảng 7 datasheet.

```powershell
powershell -ExecutionPolicy Bypass -File test\run_test.ps1     # hoac: make test
```

Kết quả: **48 DAT, 0 KHONG DAT**. Các nhóm đã kiểm:

| Nhóm | Nội dung |
|---|---|
| Giải mã | Ví dụ datasheet (65.8 %RH / 26.9 °C, sai checksum, −10.1 °C) |
| | Mọi giá trị T từ −40.0 đến 80.0 °C, cả dạng dấu-độ lớn và bù 2 |
| | Ngoài dải, khung toàn 0, khung toàn 0xFF |
| Khung ngẫu nhiên | 9000 khung, độ dài từng xung ngẫu nhiên trong min/max datasheet |
| | Ba tốc độ CPU, đến mức vòng lặp chậm hơn ~3 lần mã thật |
| Góc timing | Tất cả min; tất cả max; ngắt dài 10 µs |
| Lỗi | Không có cảm biến; SDA chập GND; sai checksum; mất tín hiệu sau 0/1/7/8/20/31/39 bit |
| | Bit mơ hồ (45 µs); gai 5 µs; xung trả lời quá ngắn hoặc quá dài; Tgo quá dài; hồi phục sau lỗi |
| Phía MCU | Xung bắt đầu ~2 ms (chuẩn 0.8–20 ms) |
| | Không bao giờ kéo bus khi cảm biến đang gửi |
| | Không đọc lại trước 2 s |

### 6.2 Mô phỏng toàn bộ firmware trên Renode — `sim/`

Chạy **đúng file `build/rak811_am2301.elf`** trên STM32L151 ảo. Model AM2301 (`sim/AM2301Model.cs`) nối vào PB5:
- phát dạng sóng theo datasheet;
- trả về giá trị của lần đo trước;
- không trả lời nếu bị đọc lại trong vòng 2 s hoặc trong 2 s đầu sau cấp nguồn.

```powershell
powershell -ExecutionPolicy Bypass -File sim\run_sim.ps1 -Renode "<đường-dẫn-tới>\renode.exe"
```

Kịch bản `sim/am2301_test.resc` chạy khoảng 75 s thời gian ảo (mất khoảng 3 phút thật). Log UART đầy đủ nằm trong
`sim/ket_qua_mo_phong_uart.txt`.

| # | Tình huống | Kết quả firmware |
|---|---|---|
| 1 | 25.0 °C / 60.0 %RH, phím `d` | Đúng; xung đo được Trel/Treh 77–78 µs, TH0 23–25 µs, TH1 67–69 µs |
| 2 | Kho lạnh −18.5 °C / 85.0 %RH | Đúng, hiện chậm một lần đọc (đúng hành vi cảm biến) |
| 3 | Bản nhái gửi T âm dạng bù 2 (−5.2 °C) | Đúng, có cảnh báo |
| 4 | Rút cảm biến 2 lần đọc, cắm lại | `khong co tra loi` ×2, rồi `hoi phuc sau 2 lan loi` |
| 5 | Sai checksum | `sai checksum`, lần sau hồi phục |
| 6 | Mất tín hiệu sau 20 bit | `mat tin hieu giua khung` (19/40 bit), lần sau hồi phục |
| 7 | SDA chập GND | `SDA bi keo thap` ×2, gỡ ra thì hồi phục |
| 8 | Bit "1" méo còn 45 µs | `xung bit sai do dai`, lần sau hồi phục |
| 9 | −40.0 °C / 99.9 %RH và 80.0 °C / 0.1 %RH | Đúng |
| 10 | Kiểm tra phía cảm biến | 22 xung bắt đầu, dài 2000–2002 µs |
| | | 0 lần đọc sớm hơn 2 s; 0 lần đọc trong 2 s đầu |

**Giới hạn của mô phỏng:**
- Renode không mô phỏng điện (điện trở kéo lên, điện dung dây, nhiễu) và không mô phỏng sai số của thạch anh HSI.
- Dòng `Reset do: watchdog` trong log mô phỏng là do Renode không có thanh ghi RCC_CSR. Trên mạch thật sẽ hiện `cap nguon (POR)` hoặc `nut RESET`.
- Vì vậy bước 3 và bước 5 ở mục 4 vẫn phải kiểm trên mạch thật.

**Biên an toàn của bộ giải mã:**
- Bit được phân loại theo độ dài mức cao, ngưỡng 48 µs. Khoảng (38, 58) µs bị coi là mơ hồ và báo lỗi.
- Datasheet cho bit "0" tối đa 30 µs và bit "1" tối thiểu 68 µs. Như vậy còn dư khoảng 8–10 µs mỗi phía.
- Mức dư này đủ chịu sai số đo (~2 µs) cộng với lệch xung nhịp HSI vài % khi chạy trong kho lạnh.

## 7. Cấu trúc mã nguồn

```
src/board.h         Chan PB5, chu ky doc 3 s, cho 2.5 s sau cap nguon
src/hal.c/.h        Clock HSI 16 MHz, SysTick, GPIO, USART1 (TX + RX phim lenh), IWDG
src/am2301.c/.h     Driver: xung bat dau 2 ms, do do dai tung xung, giai ma, checksum, chan doan
src/am2301_port.*   Ghep driver voi PB5 + bo dem SysTick (inline de vong do nhanh ~1.7 us)
src/main.c          Vong doc 3 s, self-test giai ma, thong ke, phim d / s
test/               Test driver tren PC (mo hinh cam bien theo thoi gian ao)
sim/                Model AM2301 cho Renode, kich ban, script chay, log ket qua
docs/               Datasheet AM2301 (ASAIR V1.0)
drivers/, linker/   CMSIS + startup + linker script (giong project goc)
```
