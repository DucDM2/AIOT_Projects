/*
 * air.h - Cam bien nhiet do / do am khong khi: SHT1x, SHT3x hoac SHT4x tren cung 2 chan SCL/SDA.
 * Loai cam bien chon bang SHT_TYPE trong board.h (mac dinh AUTO = tu nhan dien).
 *   SHT3x / SHT4x: I2C, nhan dien qua dia chi 0x44/0x45/0x46.
 *   SHT1x        : giao thuc 2 day rieng cua Sensirion, KHONG co dia chi -> nhan dien bang
 *                  connection reset + soft reset + doc thanh ghi status co CRC.
 */
#ifndef AIR_H
#define AIR_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    AIR_NONE = 0,
    AIR_SHT1X,
    AIR_SHT3X,
    AIR_SHT4X
} air_kind_t;

typedef enum {
    AIR_OK = 0,
    AIR_ERR_NOT_FOUND,     /* khong co cam bien tra loi */
    AIR_ERR_DATA_LOW,      /* SHT1x: DATA bi giu 0 V (thieu pull-up 10k / chap / cam bien mat nguon) */
    AIR_ERR_TIMEOUT,
    AIR_ERR_CRC
} air_status_t;

air_status_t air_init(void);
air_kind_t   air_kind(void);
const char  *air_name(void);         /* "SHT1x" / "SHT3x" / "SHT4x" / "SHT?" */
uint8_t      air_address(void);      /* dia chi I2C (0 voi SHT1x) */
air_status_t air_read(int32_t *temp_c100, int32_t *rh_100);
const char  *air_err_str(air_status_t s);

#endif /* AIR_H */
