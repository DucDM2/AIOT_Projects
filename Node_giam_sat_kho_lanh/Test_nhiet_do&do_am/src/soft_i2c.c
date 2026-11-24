/*
 * soft_i2c.c - I2C master bit-bang tren 2 chan open-drain.
 * Can dien tro keo len (module SHT31 thuong co san 10k; pull-up noi ~40k chi la du phong).
 */
#include "soft_i2c.h"
#include "board.h"
#include "hal.h"

#define I2C_HALF_US        5U      /* ~100 kHz */
#define I2C_STRETCH_US     2000U   /* thoi gian toi da cho slave giu SCL */

static inline void scl_high(void) { gpio_high(I2C_SCL_PORT, I2C_SCL_PIN); }
static inline void scl_low(void)  { gpio_low(I2C_SCL_PORT, I2C_SCL_PIN); }
static inline void sda_high(void) { gpio_high(I2C_SDA_PORT, I2C_SDA_PIN); }
static inline void sda_low(void)  { gpio_low(I2C_SDA_PORT, I2C_SDA_PIN); }
static inline bool sda_read(void) { return gpio_read(I2C_SDA_PORT, I2C_SDA_PIN); }
static inline bool scl_read(void) { return gpio_read(I2C_SCL_PORT, I2C_SCL_PIN); }

static inline void half(void) { hal_delay_us(I2C_HALF_US); }

/* Nha SCL va cho no len cao (slave co the keo dai xung clock). */
static bool scl_release(void)
{
    uint32_t t = 0;
    scl_high();
    while (!scl_read()) {
        if (t++ >= I2C_STRETCH_US) {
            return false;
        }
        hal_delay_us(1);
    }
    return true;
}

static void i2c_start(void)
{
    sda_high();
    (void)scl_release();
    half();
    sda_low();
    half();
    scl_low();
}

static void i2c_stop(void)
{
    sda_low();
    half();
    (void)scl_release();
    half();
    sda_high();
    half();
}

static bool i2c_write_byte(uint8_t b)
{
    for (uint32_t i = 0; i < 8U; i++) {
        if (b & 0x80U) { sda_high(); } else { sda_low(); }
        b <<= 1;
        half();
        if (!scl_release()) { return false; }
        half();
        scl_low();
    }
    /* Doc ACK */
    sda_high();
    half();
    if (!scl_release()) { return false; }
    bool ack = !sda_read();
    half();
    scl_low();
    return ack;
}

static uint8_t i2c_read_byte(bool ack)
{
    uint8_t b = 0;
    sda_high();
    for (uint32_t i = 0; i < 8U; i++) {
        half();
        (void)scl_release();
        b = (uint8_t)((b << 1) | (sda_read() ? 1U : 0U));
        half();
        scl_low();
    }
    if (ack) { sda_low(); } else { sda_high(); }
    half();
    (void)scl_release();
    half();
    scl_low();
    sda_high();
    return b;
}

void i2c_init(void)
{
    hal_gpio_open_drain(I2C_SCL_PORT, I2C_SCL_PIN);
    hal_gpio_open_drain(I2C_SDA_PORT, I2C_SDA_PIN);
    hal_delay_us(50);

    /* Giai phong bus neu slave dang treo SDA (vi du reset giua chung giao dich) */
    for (uint32_t i = 0; i < 9U && !sda_read(); i++) {
        scl_low();
        half();
        (void)scl_release();
        half();
    }
    i2c_stop();
}

bool i2c_write(uint8_t addr7, const uint8_t *data, uint32_t len)
{
    bool ok;
    i2c_start();
    ok = i2c_write_byte((uint8_t)(addr7 << 1));
    for (uint32_t i = 0; ok && i < len; i++) {
        ok = i2c_write_byte(data[i]);
    }
    i2c_stop();
    return ok;
}

bool i2c_read(uint8_t addr7, uint8_t *data, uint32_t len)
{
    bool ok;
    i2c_start();
    ok = i2c_write_byte((uint8_t)((addr7 << 1) | 1U));
    if (ok) {
        for (uint32_t i = 0; i < len; i++) {
            data[i] = i2c_read_byte(i + 1U < len);
        }
    }
    i2c_stop();
    return ok;
}

bool i2c_probe(uint8_t addr7)
{
    bool ok;
    i2c_start();
    ok = i2c_write_byte((uint8_t)(addr7 << 1));
    i2c_stop();
    return ok;
}
