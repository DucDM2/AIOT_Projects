/*
 * board.h - Gan chan cho WisNode-LoRa (RAK811 + CH340).
 *
 * RAK811 co 2 phien ban, KHAC NHAU o chan I2C (chan module 18/19):
 *   RAK811(H)  868/915/AS923 MHz : pin18 = PB8 (SCL), pin19 = PB9 (SDA)
 *              (PB6/PB7 duoc dung ben trong module cho cong tac RF -> KHONG dung)
 *   RAK811(L)  433/470 MHz       : pin18 = PB6 (SCL), pin19 = PB7 (SDA)
 * Chon phien ban luc build: -DRAK811_VARIANT_H (mac dinh) hoac -DRAK811_VARIANT_L.
 *
 * Tren WisNode-LoRa, cac chan nay duoc dua ra header:
 *   P5: 1=GND 2=BOOT1 3=UART3_RX 4=UART3_TX 5=PA1 6=PA0 7=PA2
 *       8=I2C1_SDA 9=I2C1_SCL 10=VCC_3V3            -> noi SHT31
 *   P3: ... 7=PA15 8=UART2_RX 9=PB5 10=VCC_3V3      -> noi DS18B20 (DQ = PB5)
 *   P1: jumper chon UART1 (PA9/PA10) -> CH340 (USB)  -> xem log tren may tinh
 */
#ifndef BOARD_H
#define BOARD_H

#include "stm32l1xx.h"

#if !defined(RAK811_VARIANT_H) && !defined(RAK811_VARIANT_L)
#define RAK811_VARIANT_H
#endif

#if defined(RAK811_VARIANT_H) && defined(RAK811_VARIANT_L)
#error "Chi duoc chon mot trong RAK811_VARIANT_H hoac RAK811_VARIANT_L"
#endif

/* ---- Chon cam bien khong khi (cung 2 chan SCL/SDA ben duoi) ----
 *  SHT_TYPE_AUTO : thu SHT3x -> SHT4x (I2C 0x44/0x45/0x46) -> SHT1x   (mac dinh)
 *  SHT_TYPE_1X   : chi SHT10/11/15 (giao thuc 2 day Sensirion, KHONG phai I2C, khong co dia chi)
 *  SHT_TYPE_3X   : chi SHT30/31/35      SHT_TYPE_4X : chi SHT40/41/45
 * Doi o day hoac luc build: .\build.ps1 -Sht 1X
 * SHT1x: SCK -> chan SCL, DATA -> chan SDA, BAT BUOC dien tro 10k tu DATA len 3V3. */
#define SHT_TYPE_AUTO       0
#define SHT_TYPE_1X         1
#define SHT_TYPE_3X         3
#define SHT_TYPE_4X         4
#ifndef SHT_TYPE
#define SHT_TYPE            SHT_TYPE_AUTO
#endif
#if (SHT_TYPE != SHT_TYPE_AUTO) && (SHT_TYPE != SHT_TYPE_1X) && (SHT_TYPE != SHT_TYPE_3X) && (SHT_TYPE != SHT_TYPE_4X)
#error "SHT_TYPE phai la SHT_TYPE_AUTO, SHT_TYPE_1X, SHT_TYPE_3X hoac SHT_TYPE_4X"
#endif

/* ---- I2C (SHT3x/4x) hoac SCK/DATA (SHT1x) - bit-bang, open-drain ---- */
#if defined(RAK811_VARIANT_H)
#define BOARD_VARIANT_NAME  "RAK811(H) 868/915/AS923"
#define I2C_SCL_PORT        GPIOB
#define I2C_SCL_PIN         8U
#define I2C_SDA_PORT        GPIOB
#define I2C_SDA_PIN         9U
#define I2C_PINS_NAME       "SCL=PB8 SDA=PB9"
#else
#define BOARD_VARIANT_NAME  "RAK811(L) 433/470"
#define I2C_SCL_PORT        GPIOB
#define I2C_SCL_PIN         6U
#define I2C_SDA_PORT        GPIOB
#define I2C_SDA_PIN         7U
#define I2C_PINS_NAME       "SCL=PB6 SDA=PB7"
#endif

/* ---- 1-Wire (DS18B20) - PB5 = chan module 16 = header P3 chan 9 ---- */
#define OW_PORT             GPIOB
#define OW_PIN              5U
#define OW_PIN_NAME         "PB5"

/* ---- UART log - USART1 PA9 (TX) / PA10 (RX) -> CH340 ---- */
#define LOG_BAUDRATE        115200U

/* ---- Bu sai so nhiet do (don vi 0.01 C, CONG vao gia tri do) ----
 * Chinh theo nhiet ke chuan dat SAT canh cam bien: bu = nhiet_ke_chuan - gia_tri_dang_doc.
 * Vi du cam bien doc 30.25 C, nhiet ke chuan 28.00 C -> (-225). Dat 0 de tat bu.
 * Chi bu nhiet do; do am SHT van la do am tuong doi tai cho dat cam bien. Gioi han +/-10 C. */
#ifndef DS18B20_T_OFFSET_X100
#define DS18B20_T_OFFSET_X100   (-225)       /* -2.25 C */
#endif
#ifndef SHT_T_OFFSET_X100
#define SHT_T_OFFSET_X100       (-225)       /* -2.25 C */
#endif
#if (DS18B20_T_OFFSET_X100 > 1000) || (DS18B20_T_OFFSET_X100 < -1000) || (SHT_T_OFFSET_X100 > 1000) || (SHT_T_OFFSET_X100 < -1000)
#error "Bu sai so nhiet do vuot +/-10 C (1000) - kiem tra lai don vi x100"
#endif

/* Chu ky do (ms) */
#define MEASURE_PERIOD_MS   5000U

#endif /* BOARD_H */
