// ===================== CAU HINH UNG DUNG (GATEWAY WIFI) =====================
#pragma once

// ---------- Watchdog ----------
// Moi vong cho deu an watchdog; neu chuong trinh treo that su > 90 s -> ESP32 tu reset.
#define WDT_TIMEOUT_S 90

// ---------- WiFi ----------
// Ten/mat khau lay tu wifi.env (tools/tao-secrets.mjs sinh vao include/secrets.h),
// hoac doi luc chay bang lenh Serial:  wifi set TenWifi|MatKhau  (luu vao flash NVS).
#define CFG_HOSTNAME "kholanh-gw"
#define CFG_WIFI_TIMEOUT_MS (30UL * 1000UL)  // cho vao WiFi moi lan thu

// ---------- Gio (SNTP) ----------
#define CFG_NTP_SERVER "pool.ntp.org"
#define CFG_NTP_SERVER2 "time.google.com"
#define CFG_TIME_WAIT_MS (20UL * 1000UL)  // cho dong bo gio toi da truoc khi gui ban tin dau tien

// ---------- Gateway & du lieu gia lap ----------
// Ma gateway PHAI trung voi ma tren app (Quan ly > Gateway), neu khong app bao "gateway la".
#define CFG_GATEWAY_ID "UG65-0FA31C"
#define CFG_SENSOR_PERIOD_MS (5UL * 60UL * 1000UL)  // moi cam bien gui 5 phut/lan (giong gateway-gia.mjs)

// ---------- MQTT ----------
#define CFG_MQTT_KEEPALIVE_S 60
#define CFG_MQTT_TIMEOUT_MS (20UL * 1000UL)  // ket noi TLS + CONNACK
#define CFG_ACK_TIMEOUT_MS (10UL * 1000UL)   // cho PUBACK cua moi ban tin

// ---------- Chu ky / nguong phuc hoi ----------
#define CFG_STATUS_INTERVAL_MS (5UL * 60UL * 1000UL)
#define CFG_NO_SERVICE_RESTART_MS (3UL * 60UL * 60UL * 1000UL)  // 3 gio; 0 = khong bao gio tu khoi dong lai ESP32
#define CFG_MAX_BACKOFF_MS (5UL * 60UL * 1000UL)
