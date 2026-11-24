// Cau hinh chan va thong so test. Che do (UART/I2C) chon bang env trong platformio.ini.
#pragma once

#if !defined(APM10_MODE_UART) && !defined(APM10_MODE_I2C)
#define APM10_MODE_UART 1  // mac dinh UART neu build bang Arduino IDE
#endif
#if defined(APM10_MODE_UART) && defined(APM10_MODE_I2C)
#error "Chi chon mot che do: APM10_MODE_UART hoac APM10_MODE_I2C"
#endif

// ---- UART (chan SET cua APM10 de ho hoac noi 3.3V) ----
// APM10 pin5 TX  -> ESP32 GPIO16 (RX2)
// APM10 pin4 RX  <- ESP32 GPIO17 (TX2)
#define APM10_UART_RX_PIN 16
#define APM10_UART_TX_PIN 17

// ---- I2C (chan SET cua APM10 noi GND) ----
// APM10 pin4 SDA <-> ESP32 GPIO21, APM10 pin5 SCL <-> ESP32 GPIO22, dien tro keo len 4.7k ve 3.3V
#define APM10_I2C_SDA_PIN 21
#define APM10_I2C_SCL_PIN 22
#define APM10_I2C_FREQ_HZ 100000

// ---- Thong so test ----
#define READ_INTERVAL_MS 1000      // datasheet: chu ky cap nhat du lieu 1 s
#define WARMUP_MS 30000            // thoi gian quat/laser on dinh, so doc truoc do chi tham khao
#define STATS_EVERY_N_READS 30     // in bang thong ke sau moi N lan doc
#define FAIL_HINT_AFTER 5          // so lan loi lien tiep truoc khi in goi y kiem tra day
#define SHOW_RAW_DEFAULT false     // in byte tho (hex) moi lan doc; bat/tat bang phim 'r'
