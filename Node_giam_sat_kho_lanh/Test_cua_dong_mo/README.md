# Test cảm biến cửa MC-38 NC — RAK811 (WisNode-LoRa)

Firmware bare-metal đọc trạng thái **cửa MỞ / ĐÓNG** bằng công tắc từ **MC-38 NC**
([Điện tử Tuyết Nga](https://dientutuyetnga.com/products/cam-bien-tu-mc-38-nc)) và in sự kiện qua USB (CH340), 115200 8N1.

- Phát hiện MỞ/ĐÓNG có **chống dội 50 ms**, đếm số lần rung tiếp điểm (bounce) bằng ngắt EXTI
- Đo thời gian mở, đếm số lần mở, **cảnh báo khi cửa mở liên tục > 60 s** (nhắc lại mỗi 30 s) — phù hợp kho lạnh
- **Bài test có hướng dẫn** (phím `t`): ĐÓNG → MỞ → ĐÓNG, kết luận **ĐẠT / KHÔNG ĐẠT**
- **Một file firmware dùng chung** cho RAK811 bản H và bản L

## 1. Nguyên lý MC-38 NC

Theo trang sản phẩm: *"Cảm biến sẽ đóng khi 2 miếng đặt gần nhau"*, khoảng cách tác động 18 ± 6 mm, tối đa 100 VDC / 300 mA.

| Thực tế | Tiếp điểm | Chân PA15 | Firmware báo |
|---|---|---|---|
| Nam châm **sát** công tắc (cửa đóng) | **đóng** → nối GND | **0** | `CUA DONG` |
| Nam châm **xa** công tắc (cửa mở) | **hở** → pull-up kéo lên | **1** | `CUA MO` |
| Đứt dây / rút connector | hở | 1 | `CUA MO` → **an toàn**: hỏng dây được báo như cửa mở |

MC-38 chỉ là một công tắc 2 dây, **không phân cực** và không cần cấp nguồn.

## 2. Sơ đồ đấu nối

```
                 WisNode-LoRa (RAK811)
            ┌──────────────────────────────┐
            │  P3-7  (PA15) ●──────────────┼──── dây 1 ┐
            │                              │           ├─ MC-38 NC (phần có dây, gắn KHUNG cửa)
            │  P5-1  (GND)  ●──────────────┼──── dây 2 ┘
            │                              │
            │  (pull-up ~40 kΩ bật sẵn     │     [nam châm] gắn CÁNH cửa, thẳng vạch dấu,
            │   trong chip, không cần      │     khe hở khi đóng < 10 mm
            │   điện trở ngoài khi dây ngắn)│
            └──────────────────────────────┘
```

**Dây dài > 2 m** (thường gặp ở cửa kho lạnh): thêm mạch chống nhiễu ngay cạnh board:

```
 3V3 (P3-10) ──[ 10 kΩ ]──┬──────────────[ 1 kΩ ]──── dây 1 ── MC-38 ── dây 2 ── GND
                          │
                PA15 (P3-7)
                          │
                       [100 nF]
                          │
                         GND
```

Lưu ý lắp đặt: gắn trên vật liệu **không nhiễm từ**, hoặc kê một miếng nhựa 5–10 mm nếu cửa bằng thép/inox
(thép làm yếu từ trường). Đặt hai nửa song song, thẳng vạch dấu.

## 3. Bảng chân khi ghép cùng cảm biến nhiệt độ / độ ẩm — không chân nào dùng chung

| Chức năng | Chân MCU | Header WisNode | Ghi chú |
|---|---|---|---|
| **MC-38 cửa** | **PA15** | **P3-7** + GND | project này |
| DS18B20 DQ | PB5 | P3-9 (+4.7 kΩ lên 3V3) | `Test_nhiet_do&do_am` |
| SHT31 SDA / SCL | PB9 / PB8 (bản H) — PB7 / PB6 (bản L) | P5-8 / P5-9 | `Test_nhiet_do&do_am` |
| 3.3 V | — | P3-10, P5-10 | dùng chung được |
| GND | — | P5-1, P4-5, GND header Arduino | **P3 không có chân GND** |
| Log USB | PA9 / PA10 | qua CH340 (jumper P1) | |
| SX1276 (LoRa, bên trong module) | xem `Test_Lora_ping_pong/src/board.h` | — | PA15 **không** trùng chân radio ở cả bản H và L |

Vì sao chọn PA15: chân này còn trống, có ngắt EXTI15, và **datasheet RAK811 với schematic WisNode ghi giống nhau**
(chân 14 = PA15). Chân PA0/PA1 không dùng vì hai tài liệu ghi ngược nhau (schematic: chân 22 = PA0, 23 = PA1;
datasheet: 22 = PA1, 23 = PA0). Sau reset PA15 là chân JTDI; firmware chuyển nó về GPIO, nạp/gỡ lỗi bằng SWD vẫn bình thường.

## 4. Build & nạp

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1      # -> build\rak811_door_test.hex
```

Script tự dùng toolchain ở `..\Test_nhiet_do&do_am\tools` (hoặc chạy `setup_toolchain.ps1`).
Nạp giống các project trước: STM32CubeProgrammer qua UART (BOOT0 → VCC, RESET, 115200 / parity Even, nạp, BOOT0 → GND, RESET)
hoặc dùng ST-Link qua P4.

## 5. Test chính xác trên mạch thật

1. Mở terminal 115200 8N1, nhấn RESET. Log sẽ in `Trang thai ban dau: CUA ...`.
2. Gõ **`t`** rồi làm theo hướng dẫn trên màn hình (mỗi bước tối đa 20 s, trạng thái phải giữ ổn định 1.5 s):

```
===== BAT DAU TEST CAM BIEN CUA MC-38 NC =====
Buoc 1/3: DONG cua - dat nam cham SAT cong tac (khe < 10 mm), giu yen...
  -> OK: doc duoc CUA DONG (chan = 0)
Buoc 2/3: MO cua - keo nam cham ra XA (> 4 cm), giu yen...
  -> OK: doc duoc CUA MO (chan = 1)
Buoc 3/3: DONG cua lai, giu yen...
  -> OK: doc duoc CUA DONG (chan = 0)
===== KET QUA TEST =====
>>> CAM BIEN HOAT DONG DUNG
```

3. **Tìm khoảng cách tác động thật:** từ từ đưa nam châm ra xa và ghi lại khoảng cách lúc log in `CUA MO`.
   Sau đó đưa lại gần và ghi khoảng cách lúc in `CUA DONG`. Kết quả nên rơi vào khoảng 12–24 mm (spec 18 ± 6 mm).
   Khoảng cách lúc đóng thường nhỏ hơn lúc mở; đó là độ trễ (hysteresis) bình thường của reed switch.
4. **Thử đứt dây:** rút một dây → phải báo `CUA MO`.
5. Để cửa mở hơn 60 s → phải có `!!! CANH BAO`. Đóng lại → `Het canh bao`.

Các phím khác: `i` = trạng thái + thống kê, `c` = xóa thống kê, `h` = trợ giúp. Cứ 10 s firmware tự in trạng thái một lần.
Ngưỡng chống dội, thời gian cảnh báo và chu kỳ in sửa trong [`src/board.h`](src/board.h).

## 6. Mô phỏng (không cần mạch)

[`sim/door_test.resc`](sim/door_test.resc) chạy chính file `.elf` trên STM32L151 ảo (Renode) và tự lật mức PA15 theo kịch bản:
đóng → mở có rung tiếp điểm → xung nhiễu 20 ms (phải bị bỏ qua) → đóng → bài test `t` → mở 65 s (phải cảnh báo) → đóng.

```powershell
renode --console --disable-xwt -e "$fw=@E:/.../build/rak811_door_test.elf; $log=@E:/.../uart_door.txt; include @E:/.../sim/door_test.resc"
```

## 7. Khắc phục sự cố

| Hiện tượng | Nguyên nhân |
|---|---|
| Luôn báo `MO`, chân = 1 | Đứt dây / cắm nhầm chân / khe nam châm quá xa / nam châm lệch vạch |
| Luôn báo `DONG`, chân = 0 | Dây 1 chạm GND, hoặc dùng nhầm loại **NO** |
| Báo ngược (đóng thành mở) | Bạn đang dùng MC-38 loại NO → đổi `DOOR_CLOSED_LEVEL` thành `1` trong `board.h` |
| Nhảy MỞ/ĐÓNG liên tục, "so canh" lớn | Khe sát ngưỡng 18 mm, cửa rung, hoặc dây dài bị nhiễu → chỉnh khe, thêm mạch RC ở mục 2 |
| Cảnh báo ngay khi bật nguồn | Cửa đang mở lúc khởi động (firmware vẫn tính giờ) |

## 8. Cấu trúc

```
src/board.h   Chân PA15, quy ước mức, thời gian chống dội / cảnh báo
src/door.c/.h Đọc PA15 (pull-up), EXTI15 đếm bounce, chống dội theo thời gian ổn định
src/main.c    Sự kiện MỞ/ĐÓNG, thời gian mở, cảnh báo, bài test hướng dẫn, thống kê
src/hal.c/.h  Clock 16 MHz, SysTick, GPIO, USART1, IWDG (dùng chung với các project khác)
sim/          Kịch bản Renode kiểm thử tự động
```
