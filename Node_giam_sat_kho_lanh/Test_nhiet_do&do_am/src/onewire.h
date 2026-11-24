/*
 * onewire.h - 1-Wire master bit-bang (theo timing trong datasheet DS18B20, Maxim 19-7487).
 * Can dien tro keo len 4.7k tu DQ len 3.3V.
 */
#ifndef ONEWIRE_H
#define ONEWIRE_H

#include <stdint.h>
#include <stdbool.h>

void    ow_init(void);
bool    ow_bus_idle_high(void);   /* false: DQ bi keo xuong (chap mach / thieu pull-up) */
bool    ow_reset(void);           /* true: co xung presence */
void    ow_write_byte(uint8_t b);
uint8_t ow_read_byte(void);
bool    ow_read_bit(void);
uint8_t ow_crc8(const uint8_t *data, uint32_t len);  /* Dallas/Maxim CRC: X^8+X^5+X^4+1 */

#endif /* ONEWIRE_H */
