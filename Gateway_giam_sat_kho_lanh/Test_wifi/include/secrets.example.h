// Sao chep thanh secrets.h (hoac chay: node tools/tao-secrets.mjs de tu lay tu cau-noi/.env cua web app + wifi.env)
// secrets.h KHONG duoc dua len git.
#pragma once

#define SECRET_WIFI_SSID "TenWifi"
#define SECRET_WIFI_PASS "MatKhauWifi"
#define SECRET_WIFI_SSID2 ""  // mang du phong (tuy chon)
#define SECRET_WIFI_PASS2 ""

#define SECRET_MQTT_HOST "xxxxxxxx.ala.asia-southeast1.emqxsl.com"
#define SECRET_MQTT_PORT 8883
#define SECRET_MQTT_TLS 1
#define SECRET_MQTT_USER "ten_dang_nhap_mqtt"
#define SECRET_MQTT_PASS "mat_khau_mqtt"

// (Tuy chon) Chung chi CA cua broker de xac thuc server (chong gia mao).
// EMQX Cloud > Deployment > Overview > tai "CA Certificate", dan noi dung PEM vao day.
// De "" = ma hoa TLS nhung KHONG xac thuc server.
#define SECRET_MQTT_CA_PEM ""
