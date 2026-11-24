// ===================== CAU HINH PHAN CUNG & UNG DUNG =====================
#pragma once

// ---------- Day noi ESP32 <-> module A7680C ----------
// ESP32 GPIO16 (RX2)  <---  TXD module
// ESP32 GPIO17 (TX2)  --->  RXD module
// ESP32 GPIO4         --->  PWRKEY module
// GND chung!  Nguon module: 5V/2A rieng (hoac VBAT 3.8-4.2V/2A), KHONG lay tu chan 3V3 cua ESP32.
#define PIN_MODEM_RX 16
#define PIN_MODEM_TX 17
#define MODEM_BAUD 115200

#define PIN_MODEM_PWRKEY 4
// 0 = noi THANG vao chan PWRKEY cua module: ESP32 keo LOW de "nhan", nha = tha noi (open-drain,
//     module tu keo len). An toan cho ca module 1.8V/VBAT.
// 1 = qua transistor NPN tren board (nhieu board A7680C co san): ESP32 xuat HIGH de "nhan".
#define MODEM_PWRKEY_VIA_TRANSISTOR 0

// Tuy chon (de -1 neu khong noi):
#define PIN_MODEM_RESET -1   // chan RESET module (open-drain, LOW = reset)
#define PIN_MODEM_POWER -1   // MOSFET/relay cat nguon module -> phuc hoi chac chan nhat khi modem treo
#define MODEM_POWER_ON_LEVEL 1

// ---------- Watchdog ----------
// Moi vong cho trong driver deu an watchdog; neu chuong trinh treo that su > 90 s -> ESP32 tu reset.
#define WDT_TIMEOUT_S 90

// ---------- Mang di dong ----------
// De "" de mang tu cap APN (thuong chay voi LTE). Neu khong len mang thu:
//   Viettel "v-internet" | Vinaphone "m3-world" | Mobifone "m-wap" | Vietnamobile "internet"
#define CFG_APN ""
#define CFG_NTP_SERVER "pool.ntp.org"
#define CFG_HTTP_TEST_URL "http://example.com/"

// ---------- Gateway & du lieu gia lap ----------
// Ma gateway PHAI trung voi ma tren app (Quan ly > Gateway), neu khong app bao "gateway la".
#define CFG_GATEWAY_ID "UG65-0FA31C"
#define CFG_SENSOR_PERIOD_MS (5UL * 60UL * 1000UL)  // moi cam bien gui 5 phut/lan (giong gateway-gia.mjs)

// ---------- Chu ky / nguong phuc hoi ----------
#define CFG_STATUS_INTERVAL_MS (5UL * 60UL * 1000UL)
#define CFG_HEALTH_INTERVAL_MS (30UL * 1000UL)
#define CFG_NET_REG_TIMEOUT_MS (3UL * 60UL * 1000UL)
#define CFG_NET_LOST_RECOVER_MS (2UL * 60UL * 1000UL)
#define CFG_NO_SERVICE_RESTART_MS (3UL * 60UL * 60UL * 1000UL)  // 3 gio; 0 = khong bao gio tu khoi dong lai ESP32
#define CFG_MAX_BACKOFF_MS (5UL * 60UL * 1000UL)
#define CFG_MQTT_KEEPALIVE_S 60
