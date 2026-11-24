/*
 * sht31.c - Driver SHT3x-DIS.
 */
#include "sht31.h"
#include "soft_i2c.h"
#include "hal.h"

#define SHT31_ADDR_A            0x44U
#define SHT31_ADDR_B            0x45U

#define CMD_MEAS_HIGH_NOSTRETCH 0x2400U
#define CMD_SOFT_RESET          0x30A2U
#define CMD_CLEAR_STATUS        0x3041U
#define CMD_HEATER_OFF          0x3066U
#define CMD_READ_STATUS         0xF32DU

static uint8_t s_addr;

uint8_t sht31_crc8(const uint8_t *data, uint32_t len)
{
    uint8_t crc = 0xFFU;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint32_t b = 0; b < 8U; b++) {
            crc = (crc & 0x80U) ? (uint8_t)((crc << 1) ^ 0x31U) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static bool send_cmd(uint16_t cmd)
{
    uint8_t buf[2] = { (uint8_t)(cmd >> 8), (uint8_t)cmd };
    return i2c_write(s_addr, buf, 2);
}

sht31_status_t sht31_init(void)
{
    uint8_t st[3];

    s_addr = 0;
    if (i2c_probe(SHT31_ADDR_A))      { s_addr = SHT31_ADDR_A; }
    else if (i2c_probe(SHT31_ADDR_B)) { s_addr = SHT31_ADDR_B; }
    else                              { return SHT31_ERR_NOT_FOUND; }

    if (!send_cmd(CMD_SOFT_RESET)) { s_addr = 0; return SHT31_ERR_NOT_FOUND; }
    hal_delay_ms(2);                       /* t_SR <= 1.5 ms */
    (void)send_cmd(CMD_HEATER_OFF);
    hal_delay_ms(1);
    (void)send_cmd(CMD_CLEAR_STATUS);
    hal_delay_ms(1);

    /* Doc status de chac chan giao tiep dung (co kiem tra CRC) */
    if (!send_cmd(CMD_READ_STATUS) || !i2c_read(s_addr, st, 3)) {
        s_addr = 0;
        return SHT31_ERR_NOT_FOUND;
    }
    if (sht31_crc8(st, 2) != st[2]) {
        return SHT31_ERR_CRC;
    }
    return SHT31_OK;
}

uint8_t sht31_address(void)
{
    return s_addr;
}

sht31_status_t sht31_read(int32_t *temp_c100, int32_t *rh_100)
{
    uint8_t d[6];
    bool got = false;

    if (s_addr == 0U) {
        return SHT31_ERR_NOT_FOUND;
    }
    if (!send_cmd(CMD_MEAS_HIGH_NOSTRETCH)) {
        return SHT31_ERR_NOT_FOUND;
    }

    /* Khong stretching: cam bien NACK khi chua do xong -> thu lai den ~50 ms */
    hal_delay_ms(16);
    for (uint32_t i = 0; i < 10U && !got; i++) {
        got = i2c_read(s_addr, d, 6);
        if (!got) {
            hal_delay_ms(4);
        }
    }
    if (!got) {
        return SHT31_ERR_TIMEOUT;
    }

    if (sht31_crc8(&d[0], 2) != d[2] || sht31_crc8(&d[3], 2) != d[5]) {
        return SHT31_ERR_CRC;
    }

    uint32_t raw_t  = ((uint32_t)d[0] << 8) | d[1];
    uint32_t raw_rh = ((uint32_t)d[3] << 8) | d[4];

    /* 17500 * 65535 = 1.147e9 < 2^31 -> khong tran int32 */
    *temp_c100 = -4500 + (int32_t)((17500U * raw_t + 32767U) / 65535U);
    *rh_100    = (int32_t)((10000U * raw_rh + 32767U) / 65535U);
    return SHT31_OK;
}
