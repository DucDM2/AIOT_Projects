/*
 * ds18b20.c - Driver DS18B20.
 */
#include "ds18b20.h"
#include "onewire.h"
#include "hal.h"

#define CMD_SKIP_ROM        0xCCU
#define CMD_READ_ROM        0x33U
#define CMD_CONVERT_T       0x44U
#define CMD_WRITE_SCRATCH   0x4EU
#define CMD_READ_SCRATCH    0xBEU

#define FAMILY_DS18B20      0x28U
#define CFG_12BIT           0x7FU
#define CONV_TIMEOUT_MS     1000U

static uint8_t s_rom[8];

static ds18b20_status_t reset_checked(void)
{
    if (!ow_bus_idle_high()) {
        return DS18B20_ERR_BUS_LOW;
    }
    if (!ow_reset()) {
        return DS18B20_ERR_NO_PRESENCE;
    }
    return DS18B20_OK;
}

static ds18b20_status_t read_scratchpad(uint8_t sp[9])
{
    ds18b20_status_t st = reset_checked();
    if (st != DS18B20_OK) {
        return st;
    }
    ow_write_byte(CMD_SKIP_ROM);
    ow_write_byte(CMD_READ_SCRATCH);
    for (uint32_t i = 0; i < 9U; i++) {
        sp[i] = ow_read_byte();
    }
    /* Tat ca 0xFF hoac 0x00 -> bus khong co du lieu that */
    if (ow_crc8(sp, 8) != sp[8] || (sp[0] == 0xFFU && sp[1] == 0xFFU && sp[4] == 0xFFU)) {
        return DS18B20_ERR_CRC;
    }
    return DS18B20_OK;
}

ds18b20_status_t ds18b20_init(void)
{
    uint8_t sp[9];
    ds18b20_status_t st;

    ow_init();

    /* Doc ROM code (chi hop le khi tren bus co dung 1 thiet bi) */
    st = reset_checked();
    if (st != DS18B20_OK) {
        return st;
    }
    ow_write_byte(CMD_READ_ROM);
    for (uint32_t i = 0; i < 8U; i++) {
        s_rom[i] = ow_read_byte();
    }
    if (ow_crc8(s_rom, 7) != s_rom[7]) {
        return DS18B20_ERR_CRC;
    }
    if (s_rom[0] != FAMILY_DS18B20) {
        return DS18B20_ERR_FAMILY;
    }

    /* Giu nguyen TH/TL, chi dat do phan giai 12-bit */
    st = read_scratchpad(sp);
    if (st != DS18B20_OK) {
        return st;
    }
    if (sp[4] != CFG_12BIT) {
        st = reset_checked();
        if (st != DS18B20_OK) {
            return st;
        }
        ow_write_byte(CMD_SKIP_ROM);
        ow_write_byte(CMD_WRITE_SCRATCH);
        ow_write_byte(sp[2]);   /* TH */
        ow_write_byte(sp[3]);   /* TL */
        ow_write_byte(CFG_12BIT);
    }
    return DS18B20_OK;
}

const uint8_t *ds18b20_rom(void)
{
    return s_rom;
}

ds18b20_status_t ds18b20_read(int32_t *temp_c100)
{
    uint8_t sp[9];
    bool done = false;
    ds18b20_status_t st = reset_checked();
    if (st != DS18B20_OK) {
        return st;
    }

    ow_write_byte(CMD_SKIP_ROM);
    ow_write_byte(CMD_CONVERT_T);

    /* Cap nguon VDD: DS18B20 tra bit 0 khi dang chuyen doi, 1 khi xong */
    uint32_t start = hal_millis();
    while ((hal_millis() - start) < CONV_TIMEOUT_MS) {
        if (ow_read_bit()) {
            done = true;
            break;
        }
        hal_delay_ms(10);
    }

    st = read_scratchpad(sp);
    if (st != DS18B20_OK) {
        return st;
    }

    int16_t raw = (int16_t)(((uint16_t)sp[1] << 8) | sp[0]);
    if (!done) {
        return (raw == 0x0550) ? DS18B20_ERR_POWERON_85 : DS18B20_ERR_TIMEOUT;
    }

    /* raw / 16 C  ->  x100 = raw * 100 / 16 = raw * 25 / 4 (lam tron) */
    int32_t v = (int32_t)raw * 25;
    *temp_c100 = (v >= 0) ? (v + 2) / 4 : (v - 2) / 4;
    return DS18B20_OK;
}
