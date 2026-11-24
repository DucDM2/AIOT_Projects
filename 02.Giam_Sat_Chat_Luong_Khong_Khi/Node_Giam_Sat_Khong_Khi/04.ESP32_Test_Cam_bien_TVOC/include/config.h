// Cau hinh chan va thong so test cam bien TVOC AGS10 (module MakerEdu MKE-S17).
#pragma once

// ---- Ket noi (module MKE-S17: GND, 5V, SDA, SCL) ----
// MKE-S17 GND <-> ESP32 GND
// MKE-S17 5V  <-  ESP32 5V/VIN (module co mach nguon rieng cho cam bien 3.0 V; cam bien tran KHONG duoc cap > 3 V)
// MKE-S17 SDA <-> ESP32 GPIO21
// MKE-S17 SCL <-> ESP32 GPIO22
// Truoc khi noi: do dien ap SDA/SCL cua module khi chua noi ESP32. Neu > 3.6 V can mach chuyen muc logic.
#define AGS10_I2C_SDA_PIN 21
#define AGS10_I2C_SCL_PIN 22
#define AGS10_I2C_FREQ_HZ 10000   // datasheet: <= 15 kHz. Chon 10 kHz de co du du phong
#define AGS10_I2C_TIMEOUT_MS 100  // 6 byte o 10 kHz ~ 6 ms, cong them thoi gian keo gian clock

// ---- Thong so test ----
#define READ_INTERVAL_MS 3000      // datasheet: chu ky lay mau >= 2 s, doc nhanh hon 1.5 s se hong du lieu
#define PREHEAT_GRACE_MS 60000     // qua 120 s lam nong + khoang nay ma van NOT_READY -> canh bao
#define STATS_EVERY_N_READS 20     // in bang thong ke sau moi N lan doc
#define FAIL_HINT_AFTER 5          // so lan loi lien tiep truoc khi in goi y kiem tra day
#define CONFIRM_TIMEOUT_MS 10000   // thoi gian cho xac nhan 'y' cho lenh hieu chuan
#define SHOW_RAW_DEFAULT false     // in byte tho (hex) moi lan doc; bat/tat bang phim 'r'
#define SCAN_I2C_ON_BOOT true      // quet bus I2C khi khoi dong
