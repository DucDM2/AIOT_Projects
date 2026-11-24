/*
 * onewire.c - 1-Wire master. Timing (datasheet DS18B20):
 *   Reset: master keo thap >= 480 us, nha ra; DS18B20 cho 15-60 us roi keo thap 60-240 us (presence)
 *   Ghi 1: keo thap 1-15 us roi nha; Ghi 0: keo thap 60-120 us. Moi slot >= 60 us, phuc hoi >= 1 us
 *   Doc  : keo thap >= 1 us, nha ra, lay mau truoc 15 us ke tu canh xuong
 * Ngat duoc tat trong tung slot de SysTick khong lam lech thoi gian.
 */
#include "onewire.h"
#include "board.h"
#include "hal.h"

static inline void dq_low(void)     { gpio_low(OW_PORT, OW_PIN); }
static inline void dq_release(void) { gpio_high(OW_PORT, OW_PIN); }
static inline bool dq_read(void)    { return gpio_read(OW_PORT, OW_PIN); }

void ow_init(void)
{
    hal_gpio_open_drain(OW_PORT, OW_PIN);
    dq_release();
    hal_delay_us(100);
}

bool ow_bus_idle_high(void)
{
    dq_release();
    hal_delay_us(5);
    return dq_read();
}

bool ow_reset(void)
{
    bool presence;
    uint32_t primask;

    if (!ow_bus_idle_high()) {
        return false;
    }

    dq_low();
    hal_delay_us(490);

    primask = __get_PRIMASK();
    __disable_irq();
    dq_release();
    hal_delay_us(70);
    presence = !dq_read();
    __set_PRIMASK(primask);

    hal_delay_us(420);   /* hoan tat cua so presence (>= 480 us tong) */
    return presence;
}

static void ow_write_bit(bool bit)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    dq_low();
    if (bit) {
        hal_delay_us(6);
        dq_release();
        hal_delay_us(64);
    } else {
        hal_delay_us(62);
        dq_release();
        hal_delay_us(8);
    }
    __set_PRIMASK(primask);
}

bool ow_read_bit(void)
{
    bool bit;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    dq_low();
    hal_delay_us(3);
    dq_release();
    hal_delay_us(9);          /* lay mau tai ~12 us */
    bit = dq_read();
    __set_PRIMASK(primask);
    hal_delay_us(55);
    return bit;
}

void ow_write_byte(uint8_t b)
{
    for (uint32_t i = 0; i < 8U; i++) {   /* LSB truoc */
        ow_write_bit((b & 0x01U) != 0U);
        b >>= 1;
    }
}

uint8_t ow_read_byte(void)
{
    uint8_t b = 0;
    for (uint32_t i = 0; i < 8U; i++) {
        b >>= 1;
        if (ow_read_bit()) {
            b |= 0x80U;
        }
    }
    return b;
}

uint8_t ow_crc8(const uint8_t *data, uint32_t len)
{
    uint8_t crc = 0;
    for (uint32_t i = 0; i < len; i++) {
        uint8_t in = data[i];
        for (uint32_t b = 0; b < 8U; b++) {
            uint8_t mix = (uint8_t)((crc ^ in) & 0x01U);
            crc >>= 1;
            if (mix) {
                crc ^= 0x8CU;
            }
            in >>= 1;
        }
    }
    return crc;
}
