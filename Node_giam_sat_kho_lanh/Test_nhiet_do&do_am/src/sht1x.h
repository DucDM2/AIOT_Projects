/*
 * sht1x.h - Sensirion SHT1x (SHT10 / SHT11 / SHT15) qua giao thuc 2 day RIENG cua Sensirion.
 *
 * SHT1x KHONG phai I2C: khong co dia chi, co "Transmission Start" rieng, CRC tinh nguoc bit.
 * Dung chung 2 chan voi I2C: SCK = chan SCL, DATA = chan SDA (xem board.h).
 * DATA BAT BUOC co dien tro keo len 10k len 3V3 (pull-up noi ~40k cua MCU chi du dung tam).
 *
 * Tham chieu: Sensirion "Datasheet SHT1x" v5 (2011) + Application Note "CRC Checksum Calculation".
 *   Lenh: 0x03 do nhiet (14 bit, <= 320 ms) | 0x05 do am (12 bit, <= 80 ms)
 *         0x07 doc status | 0x06 ghi status | 0x1E soft reset (cho >= 11 ms)
 *   Do xong: cam bien KEO DATA XUONG THAP -> MCU doc MSB, LSB, CRC.
 *   T  = d1 + d2*SOt          (d1 = -39.66 @ VDD 3.3 V, d2 = 0.01)
 *   RHlin  = c1 + c2*SOrh + c3*SOrh^2  (c1 = -2.0468, c2 = 0.0367, c3 = -1.5955e-6)
 *   RHtrue = (T - 25)*(t1 + t2*SOrh) + RHlin  (t1 = 0.01, t2 = 0.00008)
 */
#ifndef SHT1X_H
#define SHT1X_H

#include <stdint.h>
#include <stdbool.h>

#define SHT1X_CMD_TEMP     0x03U
#define SHT1X_CMD_HUMI     0x05U

typedef enum {
    SHT1X_OK = 0,
    SHT1X_ERR_DATA_LOW,     /* DATA bi giu muc thap ca sau connection reset: thieu pull-up / chap / cam bien mat nguon */
    SHT1X_ERR_NO_ACK,       /* khong co cam bien tra loi */
    SHT1X_ERR_TIMEOUT,      /* do qua lau, cam bien khong keo DATA xuong */
    SHT1X_ERR_CRC           /* sai CRC */
} sht1x_status_t;

/* Connection reset + soft reset + doc status (co CRC). SHT1X_OK = co SHT1x tren 2 chan. */
sht1x_status_t sht1x_init(void);

/* Phat lenh do (SHT1X_CMD_TEMP / SHT1X_CMD_HUMI), khong cho. */
sht1x_status_t sht1x_start(uint8_t cmd);

/* Cho cam bien do xong (toi da timeout_ms) roi doc 16 bit + kiem tra CRC. cmd = lenh da phat. */
sht1x_status_t sht1x_fetch(uint8_t cmd, uint32_t timeout_ms, uint16_t *raw);

/* Doi gia tri tho -> nhiet do x100 (C) va do am x100 (%RH, da bu nhiet, kep 0..10000). */
int16_t  sht1x_temp_x100(uint16_t so_t);
uint16_t sht1x_rh_x100(uint16_t so_rh, int16_t t_x100);

/* CRC theo application note (dung cho tu kiem tra). */
uint8_t sht1x_crc(uint8_t init_status, const uint8_t *bytes, uint32_t n);

#endif /* SHT1X_H */
