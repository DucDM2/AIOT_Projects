# Test đo điện áp nguồn 3V3 / pin — RAK811 (WisNode-LoRa)

Firmware bare-metal dùng **ADC của STM32L151** để đo nguồn và in kết quả qua USB (CH340), 115200 8N1.

- **Đo 3V3 (VDD)** của module, **không cần nối dây**. Firmware đo kênh VREFINT rồi tính ngược bằng hệ số hiệu chuẩn nhà máy.
  Cách này đúng cho mọi nguồn cấp: USB, adapter hay pin.
- **Đo Vin** (điện áp pin hoặc 5 V USB) qua mạch chia áp 1 MΩ / 1 MΩ vào **PA2**. Mạch này không bắt buộc.
  Từ Vin, firmware **nhận diện nguồn** (5 V USB / PIN / PIN YẾU) và ước tính % pin Li-ion.
- **Đo liên tục** mỗi 250 ms và **ghi nhật ký sự kiện** vào RAM (đổi nguồn, 3V3 thấp / quá áp, 3V3 nhảy đột ngột).
  Bạn có thể rút USB cho chạy bằng pin, cắm lại rồi gõ `e` để xem chuyện gì đã xảy ra.
- **Bài test tự động** (phím `t`): 3V3 trong dải → gợn nguồn → Vin / LDO, kết luận **ĐẠT / KHÔNG ĐẠT**.
- **Một file firmware dùng chung** cho RAK811 bản H và bản L (chỉ dùng ADC nội và PA2, giống nhau ở 2 bản).

## 1. Nguyên lý đo

| Đại lượng | Cách đo | Công thức |
|---|---|---|
| 3V3 (VDD = VDDA) | ADC kênh 17 = VREFINT (~1.22 V cố định bên trong chip) | `VDD = 3000 mV × VREFINT_CAL / VREFINT_raw` |
| Điện áp tại PA2 | ADC kênh 2, quy đổi bằng chính VDD vừa đo | `V_PA2 = raw × VDD / 4095` |
| Vin | Nhân với hệ số mạch chia | `Vin = V_PA2 × (R1 + R2) / R2` |

`VREFINT_CAL` là mã ADC của VREFINT do ST đo lúc sản xuất (VDDA = 3.0 V, 30 °C), lưu ở `0x1FF80078`.
Vì vậy **không cần nguồn chuẩn ngoài**: khi 3V3 thay đổi, `VREFINT_raw` đổi theo và firmware tính ra được VDD.

Mỗi lần đo, ADC đọc xen kẽ VREFINT → PA2 nhiều lần, rồi lấy trung bình và min/max. Từ min/max suy ra **gợn đỉnh-đỉnh** của 3V3.
ADC chỉ bật trong vài ms mỗi lần đo rồi tắt hẳn (giống `Project_node_kho_lanh/node/battery.c`).

## 2. Đấu nối

### 2.1. Chỉ đo 3V3 — không cần nối gì

Cắm USB rồi nạp firmware là đo được. Dòng `Vin` sẽ báo `KHONG RO (chua lap mach chia PA2)`.

### 2.2. Đo thêm Vin (pin / 5 V) — mạch chia áp vào PA2

```
 Vin (+) ──[ R1 1 MΩ ]──┬──[ R2 1 MΩ ]── GND (P5-1)
                        │
                        ├──── PA2 (P5-7)
                        │
                     [100 nF]   ← bắt buộc, đặt sát chân PA2 (ADC lấy mẫu từ tụ này)
                        │
                       GND
```

- Đo được tối đa khoảng **2 × 3V3 ≈ 6.6 V**. Pin Li-ion (4.2 V) hay USB 5 V đều nằm trong dải này.
  Nếu Vin cao hơn, tăng R1 rồi sửa `VIN_R_TOP_K` trong [`src/board.h`](src/board.h).
- Mạch chia chỉ tốn ~2 µA ở 4.2 V. Đây cũng là mạch đo pin của dự án chính (PA2, P5-7).
- Muốn chính xác hơn: đo giá trị thật của R1, R2 bằng đồng hồ rồi ghi vào `VIN_R_TOP_K` / `VIN_R_BOT_K`.

**Nối `Vin` vào đâu?** Tùy bạn muốn biết điều gì:

| Mục đích | Nối Vin vào | Firmware báo |
|---|---|---|
| Biết board đang chạy **USB hay pin** | Chân **5V** của header Arduino (= VCC5V sau diode SS24, vào LM1117) | Có USB: `NGUON 5V` (~4.6–4.8 V). Rút USB: Vin tụt → đổi nguồn |
| Theo dõi **dung lượng pin** | Cực **+** của pin (trước LDO MCP1700) | `PIN ~xx%`, `PIN YEU` khi < 3.45 V |

### 2.3. Sơ đồ nguồn WisNode-LoRa (theo schematic)

```
 USB 5V ──D2 (SS24)──► VCC5V ──► LM1117-3.3 ──► VCC_3V3 ──J2/R25──► RAK811 VCC
                         ▲                         │
 Header Arduino "5V" ────┘          P3-10 / P5-10 ─┘ (3V3 ra header)
```

- **LM1117 cần Vin ≥ 3V3 + ~1.1 V.** Nếu đấu pin Li-ion (3.7–4.2 V) vào chân 5V, 3V3 chỉ còn ~2.6–3.1 V.
  Bài test sẽ báo `LDO SUT AP`. Đúng cách là cấp pin qua LDO dòng tĩnh thấp (MCP1700-3302) vào **3V3** (P3-10), như README dự án chính mục 3.
- **Log UART đi qua CH340, mà CH340 lấy nguồn từ USB.** Khi rút USB, board vẫn chạy bằng pin nhưng không còn log.
  Có hai cách xem:
  1. Cắm USB lại rồi gõ `e` để xem nhật ký. Điều kiện là nguồn pin giữ 3V3 liên tục để MCU không bị reset.
  2. Dùng adapter USB-TTL rời: **chỉ nối TX, RX, GND**, không nối VCC, vào UART1 ở header P1 (rút jumper P1).
- Khi cắm **cả USB lẫn pin** vào 3V3, nguồn nào cao hơn sẽ gánh. Muốn đo đúng nguồn pin thì phải rút USB.

## 3. Bảng chân — không trùng với các test khác

| Chức năng | Chân MCU | Header | Ghi chú |
|---|---|---|---|
| **Đo Vin (chia áp)** | **PA2** (ADC_IN2) | **P5-7** | project này, trùng chân đo pin của `Project_node_kho_lanh` |
| Đo 3V3 | VREFINT (kênh 17, bên trong chip) | — | không dùng chân nào |
| MC-38 cửa | PA15 | P3-7 | `Test_cua_dong_mo` |
| DS18B20 / SHT31 | PB5 / PB6-9 | P3-9 / P5-8,9 | `Test_nhiet_do&do_am` |
| 3V3 / GND để đo đồng hồ | — | P3-10 / P5-1 | |
| Log USB | PA9 / PA10 | CH340 (jumper P1) | |

## 4. Build & nạp

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1      # -> build\rak811_power_test.hex
```

Script tự dùng toolchain ở `..\Test_nhiet_do&do_am\tools` (hoặc chạy `setup_toolchain.ps1`). Bản build hiện tại không có cảnh báo nào.
Cách nạp giống các project trước:
- STM32CubeProgrammer qua UART: BOOT0 → VCC, RESET, 115200 / parity Even, nạp, rồi BOOT0 → GND, RESET.
- Hoặc dùng ST-Link qua header P4.

## 5. Test trên mạch thật

1. Mở terminal 115200 8N1 rồi nhấn RESET. Log khởi động in giá trị `VREFINT_CAL`, mức Brown-out và lần đo đầu tiên.
   Sau đó cứ 5 s firmware in một dòng:
   ```
   [00:00:05.0] 3V3 = 3.300 V (TOT, gon 4 mV) | Vin = 4.749 V -> NGUON 5V (USB/adapter)
   ```
2. Gõ **`t`** để chạy bài test tự động (~1 s). Trong lúc test, giữ nguyên nguồn:
   ```
   Buoc 1/3: 3V3 = 3.300 V (dai dat 3.150 V .. 3.450 V) -> TOT
   Buoc 2/3: gon 3V3 trong 1 s = 4 mV (min 3.298 V, max 3.302 V, cho phep <= 50 mV)
   Buoc 3/3: Vin tren PA2 (P5-7): 4.749 V -> NGUON 5V (USB/adapter)
   ===== KET QUA TEST =====
     Buoc 1 3V3 trong dai : DAT
     Buoc 2 gon song      : DAT
     Buoc 3 Vin / LDO     : DAT
     Doi chieu dong ho: do P3-10 (3V3) -> P5-1 (GND), phai ~ 3.300 V (sai lech <= 2% la binh thuong)
   >>> NGUON HOAT DONG DUNG
   ```
3. **Đối chiếu đồng hồ vạn năng:**
   - Đo P3-10 → P5-1 rồi so với `3V3 = ...`. Lệch ≤ 2 % là đạt (VREFINT có sai số ~±1 % và trôi theo nhiệt).
   - Nếu có mạch chia, đo Vin so với dòng `Vin = ...`. Lệch nhiều thì kiểm tra tụ 100 nF và giá trị R1, R2.
4. **Thử đổi nguồn** (cần mạch chia ở mục 2.2 và nguồn pin qua LDO vào 3V3):
   1. Đang cắm USB thì rút USB, chờ vài giây rồi cắm lại.
   2. Mở lại cổng COM và gõ `e`. Nhật ký sẽ có dòng `DOI NGUON -> ...` hoặc `3V3 THAY DOI DOT NGOT`, kèm thời điểm và điện áp lúc đó.
5. Gõ `i` để xem chi tiết:
   - mã ADC thô của VREFINT và PA2;
   - điện áp tại chân PA2;
   - dư địa LDO (`Vin − 3V3`);
   - 3V3 / Vin nhỏ nhất và lớn nhất từ lúc khởi động. 3V3 nhỏ nhất cho biết nguồn có từng bị sụt không.

Các phím: `t` = test, `i` = chi tiết, `e` = nhật ký sự kiện (32 sự kiện gần nhất), `c` = xóa min/max + nhật ký, `h` = trợ giúp.
Ngưỡng đánh giá nằm trong [`src/board.h`](src/board.h):

| Ngưỡng | Mặc định | Ý nghĩa |
|---|---|---|
| `VDD_OK_MIN_MV` .. `VDD_OK_MAX_MV` | 3150 .. 3450 mV | Dải **TỐT** của 3V3 |
| `VDD_LOW_MV` | 3000 mV | Dưới mức này: **THẤP**, LoRa dễ reset / mất gói khi phát |
| `VDD_ABS_MAX_MV` | 3600 mV | Trên mức này: **QUÁ ÁP** (vượt giới hạn RAK811) |
| `RIPPLE_MAX_MV` | 50 mV | Gợn đỉnh-đỉnh tối đa trong 1 s |
| `VIN_USB_MIN_MV` | 4400 mV | Vin ≥ mức này thì coi là nguồn 5 V |
| `BATT_LOW_MV` | 3450 mV | Ngưỡng **PIN YẾU** (Li-ion ~10 %, giống dự án chính) |
| `LDO_DROPOUT_MV` | 1100 mV | LM1117. Khi 3V3 thấp và `Vin − 3V3` nhỏ hơn mức này thì báo LDO sụt áp |

> % pin chỉ là ước tính theo đường cong điện áp hở mạch của Li-ion ở ~25 °C.
> Pin Li-SOCl2 (3.6 V, đường xả rất phẳng) thì không suy % theo điện áp được.

## 6. Mô phỏng (không cần mạch)

Renode không có model ADC cho STM32L1. Vì vậy [`sim/power_sim.repl`](sim/power_sim.repl) thêm một **ADC1 giả lập** viết bằng Python peripheral.
Kịch bản [`sim/power_test.resc`](sim/power_test.resc) đặt điện áp theo từng giai đoạn, rồi chạy chính file `.elf`:

| Giai đoạn | Điện áp giả lập | Kết quả mô phỏng |
|---|---|---|
| 1. USB | 3V3 = 3.30 V, Vin = 4.75 V | `NGUON 5V`, test **ĐẠT** |
| 2. Rút USB, pin Li-ion qua MCP1700 | Vin = 3.95 V | Sự kiện `DOI NGUON -> PIN`, ~74 % |
| 3. Pin cắm nhầm vào chân 5V (qua LM1117) | 3V3 = 2.85 V | `3V3 -> THAP`, test **KHÔNG ĐẠT** (bước 1 + `LDO SUT AP`) |
| 4. Nguồn nhiễu | gợn 80 mV đỉnh-đỉnh | Bước 2 **KHÔNG ĐẠT** |
| 5. Pin yếu | Vin = 3.40 V | `DOI NGUON -> PIN YEU` |
| 6. Tháo mạch chia | PA2 bỏ hở, đọc ngẫu nhiên | `KHONG RO`, bước 3 **BỎ QUA**, test vẫn ĐẠT |
| 7. Phím `e` | — | Nhật ký đủ 6 sự kiện |

```powershell
powershell -ExecutionPolicy Bypass -File sim\run_sim.ps1 -Renode "C:\...\renode.exe"   # log: sim\out\uart_power.txt
```

Mô phỏng chỉ kiểm chứng **logic** firmware: công thức, phân loại nguồn, sự kiện và bài test.
Độ chính xác thật của ADC, VREFINT và mạch chia phải kiểm tra trên mạch bằng đồng hồ (mục 5).

## 7. Khắc phục sự cố

| Hiện tượng | Nguyên nhân / cách xử lý |
|---|---|
| 3V3 THẤP (< 3.0 V) khi cắm USB | Cổng USB yếu hoặc dây dài; board hỏng (HANDOFF mục 5: một board chỉ còn 2.2 V). Đổi cổng hoặc đổi dây |
| 3V3 THẤP khi chạy pin, kèm `LDO SUT AP` | Pin đang cấp qua LM1117 (chân 5V). Cấp qua LDO sụt áp thấp vào 3V3, hoặc pin đã gần cạn |
| Gợn > 50 mV | Nguồn adapter nhiễu, thiếu tụ lọc, tiếp xúc lỏng. Thêm tụ 10–100 µF gần 3V3 |
| `Vin: KHONG RO` dù đã lắp mạch chia | Đứt dây PA2 hoặc nối nhầm chân (phải là P5-7). Vin < 1.5 V cũng bị coi là không có mạch |
| Vin lệch nhiều so với đồng hồ | Thiếu tụ 100 nF, điện trở sai số lớn. Sửa `VIN_R_TOP_K` / `VIN_R_BOT_K` theo giá trị đo được |
| `CHAM DINH THANG DO` | Vin > ~2 × 3V3. Tăng R1 (vd 2 MΩ) và sửa `VIN_R_TOP_K` |
| `VREFINT_CAL ... O HIEU CHUAN HONG` | Chip không có giá trị hiệu chuẩn hợp lệ. Firmware dùng giá trị điển hình, sai số tăng lên ~±3 % |
| Log mất khi rút USB | Bình thường, vì CH340 lấy nguồn từ USB (mục 2.3). Cắm lại rồi gõ `e` |
| `Brown-out: TAT` | Nên bật BOR mức 3–4 bằng STM32CubeProgrammer (README dự án chính, mục 9) |
| `LOI ADC: ...` (3V3 = 0.000 V) | Log in bước hỏng và thanh ghi `SR` (bit 1 EOC, 6 ADONS, 8 RCNR). Bản đầu tiên bị lỗi này vì bấm `SWSTART` khi `RCNR` = 1, đã sửa (chờ RCNR = 0 sau khi chọn kênh). Còn lỗi thì gửi lại dòng log này |

## 8. Cấu trúc

```
src/board.h    Chân PA2, mạch chia, các ngưỡng đánh giá
src/supply.c/.h ADC1: VREFINT + PA2, trung bình / min / max, phân loại nguồn, % Li-ion
src/main.c     Đo liên tục, nhật ký sự kiện, bài test tự động, lệnh console
src/hal.c/.h   Clock 16 MHz, SysTick, GPIO, USART1, IWDG (dùng chung với các project khác)
sim/           Renode: ADC giả lập (power_sim.repl), kịch bản (power_test.resc), run_sim.ps1
```
