/*
 * ds18b20.h - Driver DS18B20 (1 cam bien tren bus, cap nguon VDD 3 day).
 * Tham chieu: Maxim DS18B20 datasheet 19-7487 Rev 6:
 *   Skip ROM 0xCC, Read ROM 0x33, Convert T 0x44, Write Scratchpad 0x4E, Read Scratchpad 0xBE
 *   Ma ho (family code) 0x28; 12-bit: 0.0625 C/LSB, t_conv <= 750 ms
 *   Gia tri sau cap nguon cua thanh ghi nhiet do la +85 C (0x0550)
 */
#ifndef DS18B20_H
#define DS18B20_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    DS18B20_OK = 0,
    DS18B20_ERR_BUS_LOW,      /* DQ bi keo thap: chap mach hoac sai day */
    DS18B20_ERR_NO_PRESENCE,  /* khong co cam bien (thieu pull-up 4.7k / chua cam) */
    DS18B20_ERR_CRC,          /* sai CRC */
    DS18B20_ERR_FAMILY,       /* ROM khong phai DS18B20 (0x28) */
    DS18B20_ERR_TIMEOUT,      /* chuyen doi khong xong (co the dang dung nguon ky sinh) */
    DS18B20_ERR_POWERON_85,   /* doc ra 85.00 C mac dinh - chuyen doi chua chay */
} ds18b20_status_t;

ds18b20_status_t ds18b20_init(void);                 /* doc ROM, dat 12-bit */
const uint8_t   *ds18b20_rom(void);                  /* 8 byte ROM code */
ds18b20_status_t ds18b20_read(int32_t *temp_c100);   /* nhiet do x100 (C) */

#endif /* DS18B20_H */
