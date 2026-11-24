/*
 * air.c - Chon / tu nhan dien SHT1x, SHT3x, SHT4x (xem air.h, board.h: SHT_TYPE).
 * Thu tu AUTO: SHT3x (I2C, doc status co CRC) -> SHT4x (I2C, doc serial co CRC) -> SHT1x.
 * Chuoi tin hieu cua tung kieu deu vo hai voi kieu kia (SHT1x Transmission Start = I2C START + STOP,
 * connection reset cua SHT1x = 9 xung giai phong bus I2C).
 */
#include "air.h"
#include "board.h"
#include "hal.h"
#include "soft_i2c.h"
#include "sht31.h"
#include "sht1x.h"

#define TRY_3X  ((SHT_TYPE == SHT_TYPE_AUTO) || (SHT_TYPE == SHT_TYPE_3X))
#define TRY_4X  ((SHT_TYPE == SHT_TYPE_AUTO) || (SHT_TYPE == SHT_TYPE_4X))
#define TRY_1X  ((SHT_TYPE == SHT_TYPE_AUTO) || (SHT_TYPE == SHT_TYPE_1X))

static air_kind_t s_kind;
static uint8_t s_addr4;

#if TRY_4X
/* SHT4x: soft reset 0x94, doc serial 0x89 (6 byte, 2 CRC); do 0xFD (<= 8.3 ms) */
static bool sht4_detect(void)
{
    static const uint8_t addrs[3] = { 0x44U, 0x45U, 0x46U };
    uint8_t d[6];
    for (uint32_t i = 0U; i < 3U; i++) {
        uint8_t a = addrs[i];
        uint8_t cmd = 0x94U;
        if (!i2c_probe(a)) {
            continue;
        }
        (void)i2c_write(a, &cmd, 1U);
        hal_delay_ms(2U);
        cmd = 0x89U;
        if (i2c_write(a, &cmd, 1U)) {
            hal_delay_ms(2U);
            if (i2c_read(a, d, 6U) && (sht31_crc8(&d[0], 2U) == d[2]) && (sht31_crc8(&d[3], 2U) == d[5])) {
                s_addr4 = a;
                return true;
            }
        }
    }
    return false;
}

static air_status_t sht4_read(int32_t *t, int32_t *rh)
{
    uint8_t cmd = 0xFDU;
    uint8_t d[6];
    bool got = false;
    if (!i2c_write(s_addr4, &cmd, 1U)) {
        return AIR_ERR_NOT_FOUND;
    }
    hal_delay_ms(10U);
    for (uint32_t i = 0U; (i < 10U) && !got; i++) {
        got = i2c_read(s_addr4, d, 6U);
        if (!got) {
            hal_delay_ms(2U);
        }
    }
    if (!got) {
        return AIR_ERR_TIMEOUT;
    }
    if ((sht31_crc8(&d[0], 2U) != d[2]) || (sht31_crc8(&d[3], 2U) != d[5])) {
        return AIR_ERR_CRC;
    }
    uint32_t st = ((uint32_t)d[0] << 8) | d[1];
    uint32_t sr = ((uint32_t)d[3] << 8) | d[4];
    int32_t r = -600 + (int32_t)((12500U * sr + 32767U) / 65535U);
    *t  = -4500 + (int32_t)((17500U * st + 32767U) / 65535U);
    *rh = (r < 0) ? 0 : ((r > 10000) ? 10000 : r);
    return AIR_OK;
}
#endif

#if TRY_1X
static air_status_t from_1x(sht1x_status_t s)
{
    switch (s) {
    case SHT1X_OK:           return AIR_OK;
    case SHT1X_ERR_DATA_LOW: return AIR_ERR_DATA_LOW;
    case SHT1X_ERR_TIMEOUT:  return AIR_ERR_TIMEOUT;
    case SHT1X_ERR_CRC:      return AIR_ERR_CRC;
    default:                 return AIR_ERR_NOT_FOUND;
    }
}

static air_status_t sht1_read(int32_t *t, int32_t *rh)
{
    uint16_t so_t = 0U, so_rh = 0U;
    sht1x_status_t s = sht1x_start(SHT1X_CMD_TEMP);
    if (s == SHT1X_OK) { s = sht1x_fetch(SHT1X_CMD_TEMP, 400U, &so_t); }      /* 14 bit <= 320 ms */
    if (s == SHT1X_OK) { s = sht1x_start(SHT1X_CMD_HUMI); }
    if (s == SHT1X_OK) { s = sht1x_fetch(SHT1X_CMD_HUMI, 120U, &so_rh); }     /* 12 bit <= 80 ms */
    if (s == SHT1X_OK) {
        int16_t tc = sht1x_temp_x100(so_t);
        *t  = tc;
        *rh = sht1x_rh_x100(so_rh, tc);
    }
    return from_1x(s);
}
#endif

air_status_t air_init(void)
{
    air_status_t st = AIR_ERR_NOT_FOUND;
    s_kind = AIR_NONE;

#if TRY_3X || TRY_4X
    i2c_init();
#endif
#if TRY_3X
    {
        sht31_status_t s3 = sht31_init();
        if (s3 == SHT31_OK) {
            s_kind = AIR_SHT3X;
            return AIR_OK;
        }
        if (s3 == SHT31_ERR_CRC) {
            st = AIR_ERR_CRC;
        }
    }
#endif
#if TRY_4X
    if (sht4_detect()) {
        s_kind = AIR_SHT4X;
        return AIR_OK;
    }
#endif
#if TRY_1X
    {
        sht1x_status_t s1 = sht1x_init();
        if (s1 == SHT1X_OK) {
            s_kind = AIR_SHT1X;
            return AIR_OK;
        }
        /* Bao loi cu the cua SHT1x neu co dau hieu (DATA thap / co ACK nhung sai CRC) */
        if ((s1 != SHT1X_ERR_NO_ACK) || (SHT_TYPE == SHT_TYPE_1X)) {
            st = from_1x(s1);
        }
    }
#endif
    return st;
}

air_kind_t air_kind(void)
{
    return s_kind;
}

const char *air_name(void)
{
    switch (s_kind) {
    case AIR_SHT1X: return "SHT1x";
    case AIR_SHT3X: return "SHT3x";
    case AIR_SHT4X: return "SHT4x";
    default:        return "SHT?";
    }
}

uint8_t air_address(void)
{
    switch (s_kind) {
    case AIR_SHT3X: return sht31_address();
    case AIR_SHT4X: return s_addr4;
    default:        return 0U;
    }
}

air_status_t air_read(int32_t *temp_c100, int32_t *rh_100)
{
    switch (s_kind) {
#if TRY_3X
    case AIR_SHT3X:
        switch (sht31_read(temp_c100, rh_100)) {
        case SHT31_OK:          return AIR_OK;
        case SHT31_ERR_TIMEOUT: return AIR_ERR_TIMEOUT;
        case SHT31_ERR_CRC:     return AIR_ERR_CRC;
        default:                return AIR_ERR_NOT_FOUND;
        }
#endif
#if TRY_4X
    case AIR_SHT4X:
        return sht4_read(temp_c100, rh_100);
#endif
#if TRY_1X
    case AIR_SHT1X:
        return sht1_read(temp_c100, rh_100);
#endif
    default:
        return AIR_ERR_NOT_FOUND;
    }
}

const char *air_err_str(air_status_t s)
{
    switch (s) {
    case AIR_ERR_NOT_FOUND:
#if SHT_TYPE == SHT_TYPE_1X
        return "LOI: SHT1x khong tra loi (kiem tra day SCK/DATA, nguon VDD)";
#elif SHT_TYPE == SHT_TYPE_AUTO
        return "LOI: khong thay SHT1x/SHT3x/SHT4x";
#else
        return "LOI: khong thay tren I2C (0x44/0x45/0x46)";
#endif
    case AIR_ERR_DATA_LOW: return "LOI: DATA bi giu 0V (thieu tro keo len 10k DATA->3V3 / chap / cam bien mat nguon)";
    case AIR_ERR_TIMEOUT:  return "LOI: het thoi gian cho do";
    case AIR_ERR_CRC:      return "LOI: sai CRC (nhieu / day dai / thieu pull-up)";
    default:               return "OK";
    }
}
