# Node giám sát kho lạnh — RAK811 (WisNode-LoRa)

Các bài thử phần cứng cho **node đo** của giải pháp giám sát kho lạnh, chạy trên module **RAK811**
(STM32L151CBU6 + SX1276) gắn trên board **WisNode-LoRa**. Firmware **bare-metal** — chỉ dùng thanh ghi CMSIS,
không cần STM32 HAL. Mỗi thư mục build độc lập, in kết quả qua cổng USB (CH340) 115200 8N1.

| Project | Thử gì | Có bài test ĐẠT / KHÔNG ĐẠT |
|---|---|---|
| [`Test_nhiet_do&do_am`](Test_nhiet_do&do_am) | DS18B20 (1-Wire) + SHT31 (I2C): nhiệt độ, độ ẩm, so độ lệch hai cảm biến | — |
| [`Test_cua_dong_mo`](Test_cua_dong_mo) | Công tắc từ MC-38: cửa mở/đóng, chống dội, cảnh báo cửa mở lâu | ✔ phím `t` |
| [`Test_do_dien_ap_nguon_pin`](Test_do_dien_ap_nguon_pin) | ADC: đo 3V3 (VREFINT), điện áp pin / USB, nhận diện nguồn, nhật ký sự kiện | ✔ phím `t` |
| [`Test_Lora_ping_pong`](Test_Lora_ping_pong) | LoRa điểm-điểm giữa 2 RAK811: RTT, tỉ lệ thành công, RSSI/SNR hai chiều | — |

## Build chung cho cả 4 project

```powershell
powershell -ExecutionPolicy Bypass -File setup_toolchain.ps1   # cài arm-none-eabi-gcc (một lần)
powershell -ExecutionPolicy Bypass -File build.ps1             # build ra file .hex / .bin
```

Một file firmware dùng chung cho **RAK811(H)** và **RAK811(L)** — xem README từng project để biết chân nối và cách nạp.

> Đây là các bài thử module rời. Node hoàn chỉnh (gộp cảm biến + LoRa + ngủ tiết kiệm pin) thuộc giải pháp
> giám sát kho lạnh của [AIoT Việt](https://aiotviet.com), hiện đang thử nghiệm trên thiết bị thật.
