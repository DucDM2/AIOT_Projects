# Test LoRa ping-pong — 2 mạch RAK811 (WisNode-LoRa)

Firmware bare-metal (chỉ dùng thanh ghi CMSIS) để kiểm tra liên lạc **LoRa điểm-điểm** (không cần gateway / LoRaWAN)
giữa 2 module **RAK811** (STM32L151 + SX1276). **Một file firmware chung cho cả 2 mạch**. Chế độ chọn bằng phím
gõ qua cổng COM và được lưu vào EEPROM, nên vẫn giữ sau khi mất điện.

| Phím | Chế độ | Mô tả |
|---|---|---|
| `a` | **AUTO** *(mặc định)* | Tự ping-pong: cả 2 node đều bắt đầu gửi PING; node nào nghe được PING trước sẽ **tự chuyển sang SLAVE** và trả PONG. Nạp xong là chạy, không cần cấu hình gì. |
| `m` | MASTER | Chỉ gửi PING mỗi 2 s, chờ PONG: đo **RTT**, **tỉ lệ thành công**, RSSI/SNR **cả 2 chiều** |
| `s` | SLAVE | Chỉ nghe PING và trả PONG |
| `t` | TX-only | **Node chuyên truyền**: gửi gói DATA mỗi 2 s |
| `r` | RX-only | **Node chuyên nhận**: in RSSI/SNR từng gói, đếm gói mất theo số thứ tự |
| `i` / `c` / `h` | — | In thống kê / xóa thống kê / trợ giúp |

Ví dụ log thật từ mô phỏng 2 node (xem mục 6):

```
Node A (MASTER)                                        Node B (tự thành SLAVE)
[1.1 s] Gui PING #1                                    [1.1 s] >>> Nghe thay node 39E8 -> chuyen sang SLAVE
[1.3 s] PONG #1 tu 6542 | RTT 329 ms                   [1.3 s] PING #1 tu 39E8 | RSSI -72 dBm, SNR 9.50 dB -> da tra PONG
    chieu di  (slave do): RSSI -72 dBm, SNR 9.50 dB
    chieu ve  (master do): RSSI -72 dBm, SNR 9.50 dB
    thanh cong 1/1 (100.00 %)
```

---

## 1. ⚠️ Lắp anten TRƯỚC khi cấp nguồn

Phát sóng khi **không có anten** (hoặc anten sai băng tần) có thể **làm hỏng tầng PA** của SX1276.
Dùng anten đúng băng: bản H → anten 868/915/923 MHz, bản L → anten 433/470 MHz.

## 2. Chọn đúng file theo phiên bản RAK811

Hai phiên bản RAK811 nối SX1276 vào **các chân STM32 khác nhau** (lấy từ firmware gốc
[RAK811_LoRaNode](https://github.com/RAKWireless/RAK811_LoRaNode) của RAK, đối chiếu với
[RAK811_BreakBoard](https://github.com/RAKWireless/RAK811_BreakBoard) và target `MTB_RAK811` của mbed-os):

| | RESET | DIO0 | Công tắc RF (CRX / CBT / CTX) | File nạp |
|---|---|---|---|---|
| **RAK811(H)** 868/915/AS923 | PB13 | PA11 | PB6 / PB7 / PA4 | `build/H/rak811_lora_pingpong_H.hex` |
| **RAK811(L)** 433/470 | PA8 | PB4 | PB3 / PB8 / PA4 | `build/L/rak811_lora_pingpong_L.hex` |

Hai bản dùng chung: SPI1 (PA5/PA6/PA7), NSS = PB0, cấp nguồn bộ dao động = PH1, đường ra **PA_BOOST**.
Nếu nạp nhầm file, log sẽ báo `SX1276: LOI ... RegVersion=0x..` thay vì `OK (RegVersion=0x12)`.

> Hai node **phải cùng phiên bản** (cùng băng tần). Bản H và bản L không nói chuyện được với nhau.

## 3. Thông số sóng (sửa trong [`src/lora_config.h`](src/lora_config.h))

| | Bản H | Bản L |
|---|---|---|
| Tần số | 921.4 MHz (AS923-2) | 433.175 MHz |
| Công suất | 14 dBm | 10 dBm |
| SF / BW / CR | SF9 / 125 kHz / 4/5 | SF9 / 125 kHz / 4/5 |
| Sync word | 0x12 (mạng riêng) | 0x12 |
| Airtime 1 gói 16 byte | 165 ms | 165 ms |

Giới hạn công suất ở Việt Nam (chỉ để tham khảo, hãy kiểm tra quy định hiện hành): khoảng 920–923 MHz ≈ 25 mW (14 dBm),
433 MHz SRD ≈ 10 mW. Khi đổi bất kỳ thông số nào thì **nạp lại cả 2 node**.
Muốn test tầm xa thì tăng SF (10–12), đổi lại tốc độ chậm hơn và airtime dài hơn.

## 4. Build

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1          # build cả H và L
```

`build.ps1` tự tìm ARM GCC trong `tools\` của project này hoặc của project bên cạnh (`..\Test_nhiet_do&do_am\tools`),
sau đó tìm trong PATH. Nếu chưa có thì chạy `setup_toolchain.ps1` một lần. Trên Linux/macOS dùng `make VARIANT=H`.

## 5. Nạp & chạy

Cách nạp giống project nhiệt độ: **STM32CubeProgrammer** qua USB (UART bootloader):
jumper **P2 (BOOT0) → VCC** → RESET → UART, 115200, parity **Even** → nạp `.hex` → **BOOT0 → GND** → RESET.
Hoặc nạp bằng ST-Link qua header **P4** (SWDIO/SWCLK).
⚠️ Thao tác này xóa firmware AT gốc của RAK.

1. Nạp **cùng một file** cho cả 2 mạch.
2. Mở 2 cửa sổ terminal (PuTTY / Tera Term / Arduino Serial Monitor), mỗi cửa sổ một cổng COM, **115200 8N1**.
3. Cấp nguồn cả 2 → sau 1–3 s một node tự làm MASTER, node còn lại thành SLAVE.
4. Muốn tách vai trò: gõ `t` ở node A (chuyên truyền) và `r` ở node B (chuyên nhận).

### Đọc kết quả

| Chỉ số | Tốt | Yếu | Ngưỡng mất (SF9/125k) |
|---|---|---|---|
| RSSI | > −90 dBm | −100 … −120 dBm | ≈ −129 dBm |
| SNR | > +5 dB | 0 … −10 dB | ≈ −12.5 dB |

Khi đặt 2 mạch sát nhau trên bàn, RSSI có thể đạt −20…−40 dBm. Nên đặt cách nhau tối thiểu ~1 m để bộ thu không bị bão hòa.

## 6. Mô phỏng 2 node (không cần mạch thật)

Thư mục [`sim/`](sim) có model SX1276 ảo ([`SX1276Model.cs`](sim/SX1276Model.cs)) cho trình giả lập **Renode**.
Model chạy **chính firmware `.elf`** trên 2 STM32L151 ảo nối với nhau qua sóng ảo. Kịch bản kiểm tra AUTO ping-pong,
chuyển chế độ TX/RX bằng phím, và giả lập mất 1/4 số gói:

```powershell
powershell -ExecutionPolicy Bypass -File sim\run_sim.ps1 -Renode "C:\duong_dan\renode.exe"
```

Model chỉ dùng để kiểm tra logic firmware và trình tự lệnh SPI. Nó **không** thay được việc thử trên mạch thật
(công tắc anten, công suất và tầm xa thật chỉ kiểm tra được trên phần cứng).

## 7. Khắc phục sự cố

| Hiện tượng | Nguyên nhân thường gặp |
|---|---|
| `SX1276: LOI ... RegVersion=0x00/0xFF` | Nạp nhầm bản H/L |
| Toàn `timeout`, bên kia không in gì | 2 node khác thông số (`lora_config.h`) / khác bản H–L / chưa gắn anten |
| Có gói nhưng nhiều `Goi loi CRC` | Hai mạch quá gần nhau (bão hòa) hoặc nhiễu, thử tách xa ≥ 1 m |
| Cả 2 node cùng là MASTER mãi | Một node không phát được: kiểm tra anten, jumper nguồn |
| Không thấy gì trên COM | BOOT0 còn ở VCC, sai jumper P1, sai baud |

## 8. Cấu trúc

```
src/board.h        Sơ đồ chân STM32 <-> SX1276 cho bản H/L (kèm nguồn tham chiếu)
src/lora_config.h  Tần số, SF, BW, CR, công suất, chu kỳ gửi
src/sx1276.c/.h    Driver SX1276 LoRa: SPI1, reset, image cal, errata 2.1/2.3, PA_BOOST,
                   TX/RX polling, RSSI/SNR, tính airtime, số ngẫu nhiên
src/hal.c/.h       Clock HSI 16 MHz, SysTick, GPIO, USART1, IWDG, ID chip, data EEPROM
src/main.c         Giao thức PING/PONG/DATA (gói 16 byte), 5 chế độ, thống kê
sim/               Model SX1276 cho Renode + kịch bản 2 node
```

Định dạng gói (16 byte): `'R' 'K' | loại (1=PING 2=PONG 3=DATA) | src(2) | dst(2) | seq(4) | RSSI(2) | SNR×4(1) | dự trữ(2)`.
PONG mang theo RSSI/SNR mà slave đo được trên gói PING, nhờ vậy master biết chất lượng sóng của cả hai chiều.
