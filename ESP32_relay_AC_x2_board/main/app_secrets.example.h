/**
 * @file    app_secrets.example.h
 * @brief   MẪU thông tin bí mật. Copy thành app_secrets.h rồi điền (app_secrets.h KHÔNG đưa lên git).
 *          Lần build đầu, CMake tự copy file này thành app_secrets.h nếu chưa có.
 */
#ifndef APP_SECRETS_H
#define APP_SECRETS_H

/* ---- Wi-Fi nhà/xưởng (để trống "" = chỉ phát Wi-Fi riêng của thiết bị) ---- */
#define APP_WIFI_SSID   ""
#define APP_WIFI_PASS   ""

/* ---- Wi-Fi riêng của thiết bị: tên "ESP32-Relay-xxxxxx", vào http://192.168.4.1 ----
 * Mật khẩu PHẢI ≥ 8 ký tự, nếu không thiết bị sẽ không phát Wi-Fi riêng. ĐỔI trước khi lắp thật. */
#define APP_AP_PASS     "relay-setup"

/* ---- EMQX Cloud (MQTTS cổng 8883). Để trống URI = tắt MQTT. ----
 * Lấy ở EMQX Cloud → Deployment → Overview; user/pass tạo ở Access Control → Authentication. */
#define APP_MQTT_URI    ""   /* vd "mqtts://xxxxxxxx.ala.asia-southeast1.emqxsl.com:8883" */
#define APP_MQTT_USER   ""
#define APP_MQTT_PASS   ""

/* ---- Khoá bảo vệ API web POST /api/relay (để trống = không kiểm) ----
 * Nếu đặt, trình duyệt phải gửi header  Authorization: Bearer <APP_HTTP_TOKEN>  */
#define APP_HTTP_TOKEN  ""

#endif /* APP_SECRETS_H */
