/*
 * soft_i2c.h - I2C master bang bit-bang (~100 kHz), ho tro clock stretching.
 * Dung bit-bang de chan I2C chon tu do theo phien ban RAK811 (PB8/PB9 hoac PB6/PB7)
 * va tranh cac loi da biet cua khoi I2C v1 tren STM32L1.
 */
#ifndef SOFT_I2C_H
#define SOFT_I2C_H

#include <stdint.h>
#include <stdbool.h>

void i2c_init(void);

/* Ghi len thiet bi addr7. Tra ve true neu moi byte deu duoc ACK. */
bool i2c_write(uint8_t addr7, const uint8_t *data, uint32_t len);

/* Doc tu thiet bi addr7. Tra ve false neu dia chi bi NACK (vi du: SHT31 chua do xong). */
bool i2c_read(uint8_t addr7, uint8_t *data, uint32_t len);

/* Kiem tra co thiet bi tra loi tai dia chi addr7 khong. */
bool i2c_probe(uint8_t addr7);

#endif /* SOFT_I2C_H */
