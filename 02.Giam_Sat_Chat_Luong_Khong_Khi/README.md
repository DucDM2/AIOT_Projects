# Giám sát chất lượng không khí — bài thử cảm biến với ESP32

Driver và chương trình thử cho 3 cảm biến không khí của hãng **ASAIR (Aosong)**, chạy trên ESP32 (Arduino, PlatformIO).
Driver tách khỏi phần cứng nên có **unit test chạy trên máy tính** với cảm biến giả lập (`pio test -e native`).

| Project | Cảm biến | Đo gì | Giao tiếp |
|---|---|---|---|
| [`Node_Giam_Sat_Khong_Khi/01.ESP32_Test_Cam_bien_APM10`](Node_Giam_Sat_Khong_Khi/01.ESP32_Test_Cam_bien_APM10) | APM10 (laser) | Bụi PM1.0 / PM2.5 / PM10 (µg/m³) | UART hoặc I2C |
| [`Node_Giam_Sat_Khong_Khi/02.ESP32_Test_Cam_bien_CO2`](Node_Giam_Sat_Khong_Khi/02.ESP32_Test_Cam_bien_CO2) | ACD10 (NDIR) | CO₂ 400–5000 ppm, hiệu chuẩn | I2C hoặc UART |
| [`Node_Giam_Sat_Khong_Khi/04.ESP32_Test_Cam_bien_TVOC`](Node_Giam_Sat_Khong_Khi/04.ESP32_Test_Cam_bien_TVOC) | AGS10 (module MakerEdu MKE-S17) | TVOC (khí hữu cơ bay hơi) | I2C |

Mỗi chương trình đọc định kỳ, in kết quả + thống kê ra Serial Monitor (115200), tự gợi ý kiểm tra dây khi đọc lỗi,
và có lệnh gõ trên Serial Monitor (`h` để xem trợ giúp).

> Đây là các bài thử cảm biến rời. Node giám sát không khí hoàn chỉnh thuộc giải pháp của
> [AIoT Việt](https://aiotviet.com) và đang được phát triển.
