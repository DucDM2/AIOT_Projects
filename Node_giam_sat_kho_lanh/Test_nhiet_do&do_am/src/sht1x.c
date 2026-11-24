/*
 * sht1x.c - Driver SHT1x (SHT10/11/15), bit-bang tren chan SCL (=SCK) / SDA (=DATA) cua board.h.
 * Dung chung nguyen van cho Project_node_kho_lanh va Test_nhiet_do&do_am.
 *
 * Giao thuc (datasheet SHT1x muc 3):
 *  - DATA doi muc khi SCK thap, cam bien chot khi SCK len cao. Xung ~25 kHz (cham de chiu duoc pull-up yeu / day dai).
 *  - Transmission Start: DATA xuong khi SCK cao, SCK xung thap, DATA len khi SCK cao.
 *    (Voi thiet bi I2C chung bus, chuoi nay chi la START + STOP -> vo hai.)
 *  - Connection reset: DATA nha cao, >= 9 xung SCK -> giai phong cam bien dang ket giua giao dich
 *    (vd: cam bien do xong va dang KEO DATA THAP cho MCU doc -> do chan DATA thay 0 V).
 *  - CRC: poly 0x31, khoi tao = 4 bit thap thanh ghi status dao bit, tinh tren byte lenh + du lieu,
 *    ket qua dao bit roi so voi byte CRC nhan duoc.
 */
#include "sht1x.h"
#include "board.h"
#include "hal.h"

#define SHT1X_HALF_US       20U       /* nua chu ky SCK: 25 kHz */
#define SHT1X_CMD_STATUS_RD 0x07U
#define SHT1X_CMD_RESET     0x1EU
#define SHT1X_RESET_MS      15U       /* soft reset: >= 11 ms */
#define SHT1X_CRC_POLY      0x31U

/* d1 theo VDD (datasheet bang 8): 3.0 V = -39.6, 3.5 V = -39.7 -> 3.3 V = -39.66 C */
#ifndef SHT1X_D1_X100
#define SHT1X_D1_X100       (-3966)
#endif

static uint8_t s_status;              /* 4 bit thap thanh ghi status (gia tri khoi tao CRC) */

static void sck_high(void)  { gpio_high(I2C_SCL_PORT, I2C_SCL_PIN); }
static void sck_low(void)   { gpio_low(I2C_SCL_PORT, I2C_SCL_PIN); }
static void data_rel(void)  { gpio_high(I2C_SDA_PORT, I2C_SDA_PIN); }   /* open-drain: nha = pull-up keo len */
static void data_low(void)  { gpio_low(I2C_SDA_PORT, I2C_SDA_PIN); }
static bool data_read(void) { return gpio_read(I2C_SDA_PORT, I2C_SDA_PIN); }
static void half(void)      { hal_delay_us(SHT1X_HALF_US); }

static void sck_pulse(void)
{
    sck_high();
    half();
    sck_low();
    half();
}

static uint32_t rev8(uint32_t v)
{
    uint32_t r = 0U;
    for (uint32_t i = 0U; i < 8U; i++) {
        if ((v & ((uint32_t)1U << i)) != 0U) {
            r |= (uint32_t)0x80U >> i;
        }
    }
    return r;
}

uint8_t sht1x_crc(uint8_t init_status, const uint8_t *bytes, uint32_t n)
{
    uint32_t crc = rev8((uint32_t)init_status & 0x0FU);
    for (uint32_t i = 0U; i < n; i++) {
        crc ^= (uint32_t)bytes[i];
        for (uint32_t b = 0U; b < 8U; b++) {
            if ((crc & 0x80U) != 0U) {
                crc = ((crc << 1U) ^ SHT1X_CRC_POLY) & 0xFFU;
            } else {
                crc = (crc << 1U) & 0xFFU;
            }
        }
    }
    return (uint8_t)rev8(crc);
}

/* Connection reset (9 xung SCK voi DATA nha) roi Transmission Start. false = DATA van bi keo thap. */
static bool conn_reset(void)
{
    bool free_bus;
    data_rel();
    sck_low();
    half();
    for (uint32_t i = 0U; i < 9U; i++) {
        sck_pulse();
    }
    free_bus = data_read();
    return free_bus;
}

static void trans_start(void)
{
    data_rel();
    sck_low();
    half();
    sck_high();
    half();
    data_low();
    half();
    sck_low();
    half();
    sck_high();
    half();
    data_rel();
    half();
    sck_low();
    half();
}

/* Ghi 1 byte MSB truoc; true = cam bien ACK (keo DATA thap o xung thu 9). */
static bool write_byte(uint8_t v)
{
    bool ack;
    uint32_t x = (uint32_t)v;
    for (uint32_t i = 0U; i < 8U; i++) {
        if ((x & 0x80U) != 0U) {
            data_rel();
        } else {
            data_low();
        }
        x = (x << 1U) & 0xFFU;
        half();
        sck_pulse();
    }
    data_rel();
    half();
    sck_high();
    half();
    ack = !data_read();
    sck_low();                       /* cam bien nha DATA sau suon xuong xung 9 */
    half();
    return ack;
}

/* Doc 1 byte; ack = true -> MCU keo DATA thap (doc tiep), false -> ket thuc giao dich */
static uint8_t read_byte(bool ack)
{
    uint32_t v = 0U;
    data_rel();
    for (uint32_t i = 0U; i < 8U; i++) {
        sck_high();
        half();
        v = (v << 1U) | (data_read() ? 1U : 0U);
        sck_low();
        half();
    }
    if (ack) {
        data_low();
    }
    half();
    sck_pulse();
    data_rel();
    return (uint8_t)(v & 0xFFU);
}

static sht1x_status_t send_cmd(uint8_t cmd)
{
    sht1x_status_t st = SHT1X_OK;
    trans_start();
    if (!write_byte(cmd)) {
        st = SHT1X_ERR_NO_ACK;
    }
    return st;
}

sht1x_status_t sht1x_init(void)
{
    sht1x_status_t st = SHT1X_OK;

    hal_gpio_open_drain(I2C_SCL_PORT, I2C_SCL_PIN);
    hal_gpio_open_drain(I2C_SDA_PORT, I2C_SDA_PIN);
    hal_delay_us(50U);

    if (!conn_reset()) {
        st = SHT1X_ERR_DATA_LOW;
    } else {
        st = send_cmd(SHT1X_CMD_RESET);
    }
    if (st == SHT1X_OK) {
        hal_delay_ms(SHT1X_RESET_MS);
        s_status = 0U;                                     /* status sau soft reset = mac dinh */
        st = send_cmd(SHT1X_CMD_STATUS_RD);
    }
    if (st == SHT1X_OK) {
        uint8_t b[2];
        uint8_t crc;
        b[0] = SHT1X_CMD_STATUS_RD;
        b[1] = read_byte(true);
        crc = read_byte(false);
        if (sht1x_crc(s_status, b, 2U) != crc) {
            st = SHT1X_ERR_CRC;
        } else {
            s_status = (uint8_t)((uint32_t)b[1] & 0x0FU);
        }
    }
    if (st != SHT1X_OK) {
        (void)conn_reset();                                /* de bus o trang thai nghi cho I2C / lan sau */
    }
    return st;
}

sht1x_status_t sht1x_start(uint8_t cmd)
{
    sht1x_status_t st;
    if (!conn_reset()) {
        st = SHT1X_ERR_DATA_LOW;
    } else {
        st = send_cmd(cmd);
    }
    return st;
}

sht1x_status_t sht1x_fetch(uint8_t cmd, uint32_t timeout_ms, uint16_t *raw)
{
    sht1x_status_t st = SHT1X_ERR_TIMEOUT;
    uint32_t waited = 0U;

    hal_delay_ms(1U);                                      /* de DATA kip len cao sau ACK */
    for (;;) {
        if (!data_read()) {
            st = SHT1X_OK;                                 /* cam bien keo DATA xuong: do xong */
            break;
        }
        if (waited >= timeout_ms) {
            break;
        }
        hal_delay_ms(1U);
        waited++;
    }
    if (st == SHT1X_OK) {
        uint8_t b[3];
        uint8_t crc;
        b[0] = cmd;
        b[1] = read_byte(true);
        b[2] = read_byte(true);
        crc = read_byte(false);
        if (sht1x_crc(s_status, b, 3U) != crc) {
            st = SHT1X_ERR_CRC;
        } else {
            *raw = (uint16_t)(((uint32_t)b[1] << 8U) | (uint32_t)b[2]);
        }
    }
    if (st != SHT1X_OK) {
        (void)conn_reset();
    }
    return st;
}

int16_t sht1x_temp_x100(uint16_t so_t)
{
    /* d2 = 0.01 C/LSB (14 bit) -> T_x100 = d1_x100 + SOt */
    int32_t t = SHT1X_D1_X100 + (int32_t)((uint32_t)so_t & 0x3FFFU);
    return (int16_t)t;
}

uint16_t sht1x_rh_x100(uint16_t so_rh, int16_t t_x100)
{
    /* Tinh theo don vi 1e-4 %RH, so nguyen 32 bit (khong tran: xem chu thich tung buoc) */
    uint32_t s = (uint32_t)so_rh & 0x0FFFU;                    /* 12 bit: 0..4095 */
    uint32_t sq = ((s * s) / 100U) * 15955U;                   /* <= 167690 * 15955 = 2.68e9 < 2^32 */
    int32_t lin = ((int32_t)(s * 367U) - 20468) - (int32_t)(sq / 10000U);
    int32_t comp = (((int32_t)t_x100 - 2500) * (int32_t)(1000U + (8U * s))) / 1000;   /* |.| <= 1.1e4*3.4e4 */
    int32_t rh = lin + comp;                                   /* 1e-4 % */
    if (rh >= 0) {
        rh = (rh + 50) / 100;
    } else {
        rh = (rh - 50) / 100;
    }
    if (rh < 0) {
        rh = 0;
    }
    if (rh > 10000) {
        rh = 10000;
    }
    return (uint16_t)rh;
}
