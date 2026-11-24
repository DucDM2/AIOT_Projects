# MISRA C:2012 – phạm vi, cách kiểm tra, lệch chuẩn

## Phạm vi

| Phần | Thư mục | Mức tuân thủ |
|---|---|---|
| Lõi điều khiển (GPIO thanh ghi, relay, nút, LED, parser lệnh, JSON) | `components/relay_core/` | **Bắt buộc** – 0 vi phạm chưa chấp thuận |
| Glue ESP-IDF (Wi-Fi, HTTP, MQTT, FreeRTOS) | `main/` | **Mục tiêu** – đã sửa mọi vi phạm cppcheck tìm được; còn lệch chuẩn do API ESP-IDF (bảng dưới) |
| Bộ mô phỏng & unit test | `tests/host/` | Không áp dụng (code kiểm thử, chạy trên PC) |

Lõi được thiết kế để kiểm chứng được: mọi truy cập phần cứng đi qua đúng 2 hàm `reg_io_read/reg_io_write`,
nên cùng mã nguồn chạy được trên PC với bộ mô phỏng thanh ghi.

## Cách kiểm tra

```bash
bash tools/misra_check.sh
```

Script chạy `cppcheck --dump` rồi gọi `misra.py` trực tiếp (bản cppcheck 2.21 cài qua WinLibs chạy
`--addon=misra` mà **không báo gì** vì không tìm thấy Python – đã kiểm bằng file vi phạm cố ý).
Báo cáo thô: `reports/misra.txt`, `reports/misra_main.txt`, `reports/cppcheck.txt`.

Kết quả hiện tại:

- `relay_core`: **0 vi phạm chưa chấp thuận**, 0 cảnh báo cppcheck (warning/style/portability/performance).
  Còn 2 dòng Rule 11.4 trong `reg_io_esp32.c` = lệch chuẩn **D-01**.
- `main/`: 0 vi phạm sau khi lọc Rule 17.3 và `misra-config` (dương tính giả vì phân tích không nạp header ESP-IDF).

Bổ trợ cho MISRA chương 10 (essential type): lõi biên dịch với
`-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wcast-qual -Wundef -Wswitch-default -Werror`
trên GCC 16 (PC) và GCC xtensa (ESP32) – không cảnh báo nào.

> **Giới hạn công cụ:** addon MISRA của cppcheck kiểm được phần lớn nhưng **không phải tất cả** 175 luật/chỉ thị
> (đặc biệt các luật "undecidable" và các Directive về quy trình). Để có chứng nhận chính thức cần công cụ
> thương mại (PC-lint Plus, Polyspace, Helix QAC, Parasoft) và review thủ công.

## Các quy tắc đã áp dụng có chủ đích

| Luật | Cách làm trong code |
|---|---|
| Dir 4.6 | Chỉ dùng `uint8_t/uint32_t/int32_t/size_t/bool` |
| Dir 4.9 | Không macro hàm trong lõi; địa chỉ thanh ghi là macro hằng, tính địa chỉ bằng hàm `static` |
| Dir 4.11 / 21.6 | Không `printf/snprintf` trong lõi → `str_buf.c` (bộ đệm có giới hạn, cờ tràn) |
| 21.3 | Không `malloc/free` – mọi bộ nhớ tĩnh / trên stack |
| 17.2 | Không đệ quy – parser JSON là máy trạng thái lặp |
| 15.5 | Một điểm thoát cho mỗi hàm (cả trong `main/`) |
| 15.6 / 15.7 / 16.4 | Luôn có `{}`; mọi `if…else if` kết thúc bằng `else`; mọi `switch` có `default` |
| 10.x | Hằng có hậu tố `U/UL`; không số học trên `char` (tra bảng thay cho `'0' + n`) |
| 12.2 | Phép dịch `1UL << pin` chỉ thực hiện sau khi kiểm `pin < 32` |
| 8.9 | Biến chỉ dùng trong một hàm được khai báo `static` cục bộ |
| 21.14 | Không dùng `memcmp` cho chuỗi – so từng ký tự có độ dài |
| Tràn thời gian | Mọi so sánh thời gian dùng phép trừ không dấu `(uint32_t)(now - t0)` → đúng khi bộ đếm ms tràn vòng (có test) |

## Lệch chuẩn đã chấp thuận (Deviation Record)

| ID | Luật | Vị trí | Lý do | Biện pháp kiểm soát |
|---|---|---|---|---|
| D-01 | 11.4 (advisory) – chuyển số nguyên ↔ con trỏ | `components/relay_core/port/esp32/reg_io_esp32.c` | Cách duy nhất truy cập thanh ghi memory-mapped | Chỉ ở 1 file, 2 dòng; địa chỉ chỉ từ `esp32_regs.h`; con trỏ `volatile`; được kiểm gián tiếp bởi unit test với bộ mô phỏng (sai địa chỉ → fault) |
| D-02 | 17.1 / 21.6 – `<stdarg.h>`, stdio | `main/*.c` (`ESP_LOGx`) | Hệ thống log của ESP-IDF là variadic printf | Chỉ dùng chuỗi định dạng hằng; không dùng trong lõi |
| D-03 | 1.2 – phần mở rộng ngôn ngữ | `main/app_http.c` (`asm("_binary_index_html_start")`) | Ký hiệu do linker tạo cho file nhúng EMBED_TXTFILES | Chỉ khai báo `extern const`, không có mã asm thực thi |
| D-04 | 11.5 – `void*` → con trỏ đối tượng | `main/app_wifi.c`, `main/app_mqtt.c` (dữ liệu sự kiện) | API sự kiện ESP-IDF truyền `void *event_data` | Kiểu đích xác định bởi `event_base` + `event_id` theo tài liệu ESP-IDF |
| D-05 | 21.1x, 4.x – macro/API bên thứ ba | ESP-IDF, FreeRTOS, mbedTLS | Thư viện nền tảng không tuân thủ MISRA | Ngoài phạm vi; cô lập trong `main/`, lõi không phụ thuộc |
