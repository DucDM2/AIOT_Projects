/*
 * board.h - Gan chan cho WisNode-LoRa (RAK811 + CH340) khi test cam bien AM2301 (DHT21).
 *
 * AM2301 dung bus 1 day rieng cua Aosong (KHONG phai 1-Wire Dallas), noi vao PB5:
 *   PB5 = chan module RAK811 so 16 = header P3 chan 9 tren WisNode-LoRa.
 * PB5 giong nhau o RAK811(H) va RAK811(L) -> 1 file firmware dung chung cho ca 2 ban.
 *
 * Header WisNode-LoRa:
 *   P3: ... 7=PA15 8=UART2_RX 9=PB5 10=VCC_3V3      -> noi AM2301 (SDA vang = PB5)
 *   P1: jumper chon UART1 (PA9/PA10) -> CH340 (USB)  -> xem log tren may tinh
 */
#ifndef BOARD_H
#define BOARD_H

#include "stm32l1xx.h"

/* ---- AM2301 data (SDA, day vang) ---- */
#define AM_PORT             GPIOB
#define AM_PIN              5U
#define AM_PIN_NAME         "PB5 (header P3-9)"

/* ---- UART log - USART1 PA9 (TX) / PA10 (RX) -> CH340 ---- */
#define LOG_BAUDRATE        115200U

/* Chu ky doc (ms). Datasheet: khoang cach 2 lan doc toi thieu 2 s. */
#define MEASURE_PERIOD_MS   3000U

/* Thoi gian cho sau khi cap nguon truoc lan doc dau tien (datasheet: > 2 s) */
#define POWERUP_WAIT_MS     2500U

/* In bang thong ke sau moi N lan doc */
#define STATS_EVERY_N       20U

#endif /* BOARD_H */
