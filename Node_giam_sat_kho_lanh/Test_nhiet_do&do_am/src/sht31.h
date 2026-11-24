/*
 * sht31.h - Driver Sensirion SHT3x-DIS (SHT30/31/35) qua I2C.
 * Tham chieu: Sensirion "Datasheet SHT3x-DIS" (v6/v7):
 *   - Dia chi: 0x44 (ADDR noi GND, mac dinh tren module GY-SHT31-D) hoac 0x45 (ADDR noi VDD)
 *   - Do don (single shot), do lap lai cao, KHONG clock stretching: lenh 0x2400, t_meas <= 15.5 ms
 *   - Du lieu: T_MSB T_LSB CRC  RH_MSB RH_LSB CRC ; CRC-8 poly 0x31, init 0xFF
 *   - T[C] = -45 + 175 * S_T / 65535 ; RH[%] = 100 * S_RH / 65535
 */
#ifndef SHT31_H
#define SHT31_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    SHT31_OK = 0,
    SHT31_ERR_NOT_FOUND,   /* khong co thiet bi tra loi tren I2C */
    SHT31_ERR_TIMEOUT,     /* do khong xong trong thoi gian cho phep */
    SHT31_ERR_CRC,         /* sai CRC du lieu */
} sht31_status_t;

/* Tim SHT31 o 0x44 roi 0x45, soft-reset va doc thanh ghi trang thai. */
sht31_status_t sht31_init(void);

/* Dia chi dang dung (0 neu chua tim thay). */
uint8_t sht31_address(void);

/* Do 1 lan. temp_c100: nhiet do x100 (C), rh_100: do am x100 (%RH). */
sht31_status_t sht31_read(int32_t *temp_c100, int32_t *rh_100);

/* Dung cho tu kiem tra: CRC-8 theo datasheet (0xBEEF -> 0x92). */
uint8_t sht31_crc8(const uint8_t *data, uint32_t len);

#endif /* SHT31_H */
