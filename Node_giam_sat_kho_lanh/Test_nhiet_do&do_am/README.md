# Node giám sát kho lạnh — RAK811 (WisNode-LoRa) + DS18B20 + SHT31

Firmware bare-metal (chỉ dùng thanh ghi CMSIS, không cần STM32 HAL) cho module **RAK811**
(STM32L151CBU6 + SX1276) trên board **WisNode-LoRa**. Firmware đọc:

- **DS18B20** (1-Wire) — nhiệt độ, 12-bit (0.0625 °C), sai số ±0.5 °C (−10…+85 °C)
- **SHT31** (I2C, module GY-SHT31-D) — nhiệt độ ±0.2 °C (điển hình) + độ ẩm ±2 %RH

rồi in kết quả qua cổng USB (CH340) mỗi 5 giây, kèm **độ lệch nhiệt độ DS18B20 − SHT31** để so sánh.

```
=== Node giam sat kho lanh - RAK811 / WisNode-LoRa ===
Phien ban : RAK811(H) 868/915/AS923
Reset do  : cap nguon (POR)
Self-test CRC SHT31: OK
Khoi tao cam bien:
  DS18B20 @ PB5: OK, ROM=28FF4C1A6E1801B7
  SHT31   @ SCL=PB8 SDA=PB9: OK, addr=0x44
Chu ky do: 5 s

[0 s] DS18B20: 25.31 C | SHT31: 25.12 C, 60.45 %RH | Lech (DS-SHT): +0.19 C
```

---

## 1. Chọn đúng phiên bản RAK811 (quan trọng)

RAK811 có 2 phiên bản, **chân I2C khác nhau** (theo [datasheet RAK811](https://docs.rakwireless.com/product-categories/wisduo/rak811-module/datasheet)):

| Phiên bản | Băng tần (in trên nhãn module) | I2C (chân 18/19 của module) | File nạp |
|---|---|---|---|
| **RAK811(H)** | 868 / 915 / **AS923** (thường gặp ở VN) | PB8 = SCL, PB9 = SDA | `build/H/rak811_coldroom_H.hex` |
| **RAK811(L)** | 433 / 470 | PB6 = SCL, PB7 = SDA | `build/L/rak811_coldroom_L.hex` |

Trên bản H, PB6/PB7 dùng bên trong module cho công tắc RF — vì vậy firmware bản H **không đụng** tới PB6/PB7.
Nếu nạp nhầm, SHT31 sẽ báo `khong thay tren I2C` (không gây hỏng mạch) — chỉ cần nạp file còn lại.

## 2. Đấu dây

Lấy theo schematic WisNode-LoRa: header **P5** (I2C) và **P3** (GPIO). Cả hai cảm biến dùng **3.3 V**.

| Cảm biến | Chân cảm biến | Nối vào WisNode-LoRa |
|---|---|---|
| SHT31 (GY-SHT31-D) | VIN | P5-10 (VCC_3V3) |
| | GND | P5-1 (GND) |
| | SCL | P5-9 (I2C1_SCL) |
| | SDA | P5-8 (I2C1_SDA) |
| | ADR | để hở / GND → địa chỉ 0x44 (nối 3.3 V → 0x45, firmware tự dò cả hai) |
| DS18B20 | VDD (đỏ) | P3-10 (VCC_3V3) |
| | GND (đen) | GND |
| | DQ (vàng) | P3-9 (**PB5**) + **điện trở 4.7 kΩ từ DQ lên 3.3 V** |

Ghi chú:
- **Bắt buộc có pull-up 4.7 kΩ cho DS18B20** (trừ khi dùng loại module đã có sẵn điện trở). Firmware dùng chế độ cấp nguồn VDD (3 dây), không dùng chế độ ký sinh.
- Module GY-SHT31-D thường có sẵn pull-up 10 kΩ trên SDA/SCL — đủ dùng. Nếu dùng chip SHT31 trần, thêm 4.7 kΩ lên 3.3 V cho SDA và SCL.
- Để so sánh công bằng, đặt đầu dò DS18B20 sát cạnh SHT31 và chờ ~10 phút cho cân bằng nhiệt. Độ lệch < ±0.8 °C (tổng sai số 2 cảm biến) là bình thường; vượt ngưỡng firmware sẽ in `(!)`.

## 3. Build

Yêu cầu: Windows + PowerShell. Không cần cài IDE.

```powershell
powershell -ExecutionPolicy Bypass -File setup_toolchain.ps1   # chỉ lần đầu: tải ARM GCC 15.2 (xPack) vào tools\
powershell -ExecutionPolicy Bypass -File build.ps1             # build cả bản H và L
```

Kết quả: `build/H/*.hex|.bin|.elf` và `build/L/*.hex|.bin|.elf` (~5 KB flash).
Trên Linux/macOS/MSYS2 có thể dùng `make VARIANT=H` hoặc `make VARIANT=L`.

## 4. Nạp firmware

> ⚠️ Nạp firmware này sẽ **xoá firmware AT/LoRaWAN gốc của RAK**. Muốn dùng lại lệnh AT, nạp lại firmware gốc từ trang RAK.

### Cách A — qua USB (UART bootloader, không cần mạch nạp)

1. Jumper **P1** ở vị trí nối UART1 (PA9/PA10) với CH340 (vị trí mặc định để dùng lệnh AT qua USB).
2. Chuyển jumper **P2 (BOOT0)** sang phía **VCC_3V3**, cắm USB, nhấn nút **RESET**.
3. Mở **STM32CubeProgrammer** → chọn **UART**, cổng COM của CH340, baud **115200**, parity **Even** → **Connect**.
4. Chọn file `.hex` đúng phiên bản → **Download**.
5. Chuyển jumper **P2 (BOOT0)** về **GND**, nhấn **RESET**.

### Cách B — qua ST-Link (SWD)

Header **P4**: 1 = VCC_3V3, 2 = SWDIO, 3 = SWCLK, 4 = RST, 5 = GND. Dùng STM32CubeProgrammer (chế độ ST-LINK/SWD)
hoặc: `STM32_Programmer_CLI -c port=SWD -w build/H/rak811_coldroom_H.hex -v -rst`.

## 5. Xem kết quả

Mở cổng COM của CH340 bằng PuTTY / Arduino Serial Monitor / Tera Term: **115200 baud, 8N1**. Nhấn RESET để xem banner khởi động.

## 6. Khắc phục sự cố

| Thông báo | Nguyên nhân thường gặp |
|---|---|
| `DS18B20 ... khong thay cam bien` | Thiếu pull-up 4.7 kΩ, sai chân DQ, chưa cấp 3.3 V |
| `DS18B20 ... DQ bi keo thap` | DQ chập GND hoặc đấu ngược VDD/GND |
| `DS18B20 ... sai CRC` | Dây quá dài / nhiễu → giảm pull-up xuống 2.2–3.3 kΩ |
| `SHT31 ... khong thay tren I2C` | Nạp nhầm bản H/L, đảo SDA/SCL, chưa cấp nguồn |
| Không thấy gì trên COM | BOOT0 còn ở VCC, jumper P1 sai vị trí, sai baud |

Cảm biến rút ra / cắm lại khi đang chạy sẽ được tự khởi tạo lại. Watchdog (IWDG ~28 s) tự reset nếu firmware treo.

## 7. Cấu trúc mã nguồn

```
src/board.h      Gán chân theo phiên bản H/L, chu kỳ đo
src/hal.c/.h     Clock HSI 16 MHz, SysTick, delay µs, GPIO, USART1, IWDG
src/soft_i2c.*   I2C master bit-bang (~100 kHz, hỗ trợ clock stretching, giải phóng bus treo)
src/sht31.*      SHT3x-DIS: lệnh 0x2400, CRC-8 (0x31), T = -45 + 175·S/65535, RH = 100·S/65535
src/onewire.*    1-Wire master theo timing datasheet DS18B20, CRC Dallas
src/ds18b20.*    Read ROM, đặt 12-bit, Convert T (chờ bằng read-slot), Read Scratchpad + CRC
src/main.c       Vòng đo 5 s, in kết quả và độ lệch
drivers/cmsis/   Header CMSIS của ARM (5.9.0) và ST (cmsis_device_l1), startup, system — Apache-2.0
linker/          Linker script STM32L151CBU6(A): 128 KB flash, 16 KB RAM
```

Tài liệu tham khảo: [Sensirion SHT3x-DIS datasheet](https://sensirion.com/media/documents/213E6A3B/63A5A569/Datasheet_SHT3x_DIS.pdf),
`ds18b20.pdf` (Maxim 19-7487), `WisNode-LoRa_Schematic.pdf`.
