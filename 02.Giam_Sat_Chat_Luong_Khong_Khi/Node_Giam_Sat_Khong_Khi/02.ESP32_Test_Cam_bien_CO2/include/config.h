// Cau hinh chan va thong so test. Che do (I2C/UART) chon bang env trong platformio.ini.
#pragma once

#if !defined(ACD10_MODE_UART) && !defined(ACD10_MODE_I2C)
#define ACD10_MODE_I2C 1  // mac dinh I2C (giong cam bien) neu build bang Arduino IDE
#endif
#if defined(ACD10_MODE_UART) && defined(ACD10_MODE_I2C)
#error "Chi chon mot che do: ACD10_MODE_UART hoac ACD10_MODE_I2C"
#endif

// ---- I2C (chan SET pin5 cua ACD10 de ho -> I2C, dia chi 0x2A) ----
// ACD10 pin1 SDA <-> ESP32 GPIO21, ACD10 pin2 SCL <-> ESP32 GPIO22 (qua mach chuyen muc 5V<->3.3V)
#define ACD10_I2C_SDA_PIN 21
#define ACD10_I2C_SCL_PIN 22
#define ACD10_I2C_FREQ_HZ 50000  // datasheet: toi da 100 kHz; 50 kHz cho du phong khi day dai

// ---- UART (chan SET pin5 cua ACD10 noi GND -> UART 1200 baud) ----
// ACD10 pin2 TX  -> ESP32 GPIO16 (RX2)
// ACD10 pin1 RX  <- ESP32 GPIO17 (TX2)
#define ACD10_UART_RX_PIN 16
#define ACD10_UART_TX_PIN 17

// ---- Thong so test ----
#define READ_INTERVAL_MS 2000      // datasheet: chu ky cap nhat du lieu 2 s
#define STATS_EVERY_N_READS 30     // in bang thong ke sau moi N lan doc (~1 phut)
#define FAIL_HINT_AFTER 3          // so lan loi lien tiep truoc khi in goi y kiem tra day
#define SHOW_RAW_DEFAULT false     // in byte tho (hex) moi lan doc; bat/tat bang lenh 'r'
#define CONFIRM_TIMEOUT_MS 15000   // thoi gian cho xac nhan 'y' cho lenh hieu chuan / reset
