/*
 * board.h - Gan chan cho test cam bien cua MC-38 NC tren WisNode-LoRa (RAK811).
 *
 * MC-38 NC (dientutuyetnga.com): "Cam bien se dong khi 2 mieng dat gan nhau"
 *   -> nam cham SAT cong tac (cua DONG)  : tiep diem DONG  -> PA15 bi keo xuong GND -> muc 0
 *   -> nam cham XA  cong tac (cua MO)    : tiep diem HO    -> pull-up keo len 3.3V  -> muc 1
 *   -> dut day / rut connector           : doc la MO (an toan cho bao dong)
 *
 * Chon PA15 (chan module 14, header P3 chan 7) vi:
 *   - KHONG trung voi cam bien nhiet/am: DS18B20 = PB5 (P3-9), SHT31 = PB8/PB9 (ban H) hoac PB6/PB7 (ban L) (P5-8/9)
 *   - KHONG trung chan SX1276 ben trong module o ca ban H lan L (de sau nay ghep voi LoRa)
 *   - Datasheet RAK811 va schematic WisNode ghi giong nhau (PA0/PA1 o chan 22/23 bi ghi nguoc nhau
 *     giua 2 tai lieu nen khong dung)
 *   - Co EXTI15 de bat canh ngay lap tuc
 * Sau reset PA15 la JTDI (co pull-up). Firmware chuyen sang GPIO input; SWD (PA13/PA14) van dung duoc.
 */
#ifndef BOARD_H
#define BOARD_H

#include "stm32l1xx.h"

#define DOOR_PORT           GPIOA
#define DOOR_PIN            15U
#define DOOR_PIN_NAME       "PA15 (P3-7)"

/* Muc logic khi cua DONG (MC-38 NC: tiep diem dong khi co nam cham -> noi GND) */
#define DOOR_CLOSED_LEVEL   0

#define DOOR_DEBOUNCE_MS    50U      /* tin hieu phai on dinh 50 ms moi chap nhan */
#define DOOR_OPEN_ALARM_S   60U      /* canh bao khi cua mo lien tuc qua 60 s (kho lanh) */
#define DOOR_ALARM_REPEAT_S 30U      /* nhac lai canh bao moi 30 s */
#define STATUS_PERIOD_MS    10000U   /* in trang thai dinh ky */

#define LOG_BAUDRATE        115200U

#endif /* BOARD_H */
