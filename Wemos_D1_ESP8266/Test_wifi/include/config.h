// ===================== CAU HINH UNG DUNG (GATEWAY WIFI - WEMOS D1 / ESP8266) =====================
#pragma once

// ---------- Watchdog ----------
// ESP8266 co san watchdog phan mem (~3 s) + phan cung (~8 s): vong lap nao khong nhuong CPU qua lau
// thi chip tu reset. Moi vong cho trong firmware deu goi delay()/yield() nen khong bi reset oan.

// ---------- WiFi ----------
// Ten/mat khau lay tu wifi.env (tools/tao-secrets.mjs sinh vao include/secrets.h),
// hoac doi luc chay bang lenh Serial:  wifi set TenWifi|MatKhau  (luu vao flash/EEPROM).
#define CFG_HOSTNAME "kholanh-gw-8266"
#define CFG_WIFI_TIMEOUT_MS (30UL * 1000UL)  // cho vao WiFi moi lan thu

// ---------- Gio (SNTP) ----------
#define CFG_NTP_SERVER "pool.ntp.org"
#define CFG_NTP_SERVER2 "time.google.com"
#define CFG_TIME_WAIT_MS (20UL * 1000UL)  // cho dong bo gio toi da truoc khi gui ban tin dau tien

// ---------- Gateway & du lieu gia lap ----------
// Ma gateway PHAI trung voi ma tren app (Quan ly > Gateway), neu khong app bao "gateway la".
// KHONG chay cung luc voi gateway ESP32 (4G/WiFi) hay gateway-gia.mjs dung cung ma -> trung du lieu.
#define CFG_GATEWAY_ID "UG65-0FA31C"
#define CFG_SENSOR_PERIOD_MS (5UL * 60UL * 1000UL)  // moi cam bien gui 5 phut/lan (giong gateway-gia.mjs)

// ---------- MQTT ----------
#define CFG_MQTT_KEEPALIVE_S 60
#define CFG_MQTT_TIMEOUT_MS (20UL * 1000UL)  // ket noi TLS + CONNACK
#define CFG_ACK_TIMEOUT_MS (10UL * 1000UL)   // cho PUBACK cua moi ban tin

// ---------- TLS (BearSSL) ----------
// Bo dem TLS: neu broker ho tro MFLN (gioi han kich thuoc goi) thi chi can 1 KB, khong thi phai 16 KB.
#define CFG_TLS_MFLN_SIZE 1024
#define CFG_TLS_MIN_HEAP 24000  // heap lien tuc toi thieu de thu ket noi TLS khong MFLN (16 KB + ngu canh)

// ---------- Chu ky / nguong phuc hoi ----------
#define CFG_STATUS_INTERVAL_MS (5UL * 60UL * 1000UL)
#define CFG_NO_SERVICE_RESTART_MS (3UL * 60UL * 60UL * 1000UL)  // 3 gio; 0 = khong bao gio tu khoi dong lai
#define CFG_MAX_BACKOFF_MS (5UL * 60UL * 1000UL)
