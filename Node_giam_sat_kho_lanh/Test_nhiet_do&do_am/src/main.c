/*
 * main.c - Node giam sat kho lanh: RAK811 (WisNode-LoRa) doc DS18B20 + SHT1x/SHT3x/SHT4x (SHT_TYPE trong board.h),
 * in ket qua qua USART1 (CH340 -> USB) 115200 8N1, so sanh nhiet do 2 cam bien.
 */
#include "board.h"
#include "hal.h"
#include "soft_i2c.h"
#include "sht31.h"
#include "air.h"
#include "sht1x.h"
#include "onewire.h"
#include "ds18b20.h"

static const char *ds_err_str(ds18b20_status_t s)
{
    switch (s) {
    case DS18B20_ERR_BUS_LOW:     return "LOI: DQ bi keo thap (chap/sai day)";
    case DS18B20_ERR_NO_PRESENCE: return "LOI: khong thay cam bien (kiem tra day, pull-up 4.7k)";
    case DS18B20_ERR_CRC:         return "LOI: sai CRC (nhieu / day dai / thieu pull-up)";
    case DS18B20_ERR_FAMILY:      return "LOI: khong phai DS18B20";
    case DS18B20_ERR_TIMEOUT:     return "LOI: chuyen doi qua lau (dang dung nguon ky sinh?)";
    case DS18B20_ERR_POWERON_85:  return "LOI: doc 85.00C mac dinh (chuyen doi chua chay)";
    default:                      return "OK";
    }
}

static void print_ds_init(ds18b20_status_t st)
{
    uart_puts("  DS18B20 @ " OW_PIN_NAME ": ");
    if (st == DS18B20_OK) {
        const uint8_t *rom = ds18b20_rom();
        uart_puts("OK, ROM=");
        for (int i = 0; i < 8; i++) {
            uart_put_hex8(rom[i]);
        }
        uart_puts("\n");
    } else {
        uart_puts(ds_err_str(st));
        uart_puts("\n");
    }
}

static void print_sht_init(air_status_t st)
{
    uart_puts("  SHT     @ " I2C_PINS_NAME ": ");
    if (st == AIR_OK) {
        uart_puts(air_name());
        if (air_kind() == AIR_SHT1X) {
            uart_puts(" OK (giao thuc 2 day, khong co dia chi)\n");
        } else {
            uart_puts(" OK, addr=0x");
            uart_put_hex8(air_address());
            uart_puts("\n");
        }
    } else {
        uart_puts(air_err_str(st));
        uart_puts("\n");
    }
}

static void uart_put_signed_centi(int32_t v)
{
    if (v >= 0) { uart_putc('+'); }
    uart_put_centi(v);
}

int main(void)
{
    hal_clock_init();
    uart_init(LOG_BAUDRATE);
    const char *rst = hal_reset_cause();
    hal_iwdg_start();

    uart_puts("\n\n=== Node giam sat kho lanh - RAK811 / WisNode-LoRa ===\n");
    uart_puts("Phien ban : " BOARD_VARIANT_NAME "\n");
    uart_puts("Reset do  : ");
    uart_puts(rst);
    uart_puts("\n");

    /* Tu kiem tra CRC SHT3x theo vi du trong datasheet: 0xBEEF -> 0x92 */
    {
        static const uint8_t beef[2] = { 0xBEU, 0xEFU };
        uart_puts("Self-test CRC SHT31: ");
        uart_puts(sht31_crc8(beef, 2) == 0x92U ? "OK\n" : "FAIL\n");
        /* SHT1x: CRC dao bit (theo App Note Sensirion): lenh 0x05 + du lieu 0x05DC, status 0 -> 0x86 */
        static const uint8_t v1[3] = { 0x05U, 0x05U, 0xDCU };
        uart_puts("Self-test CRC SHT1x: ");
        uart_puts(sht1x_crc(0U, v1, 3U) == 0x86U ? "OK\n" : "FAIL\n");
    }

    air_status_t sht_state = air_init();
    ds18b20_status_t ds_state = ds18b20_init();

    uart_puts("Khoi tao cam bien:\n");
    print_ds_init(ds_state);
    print_sht_init(sht_state);
    uart_puts("Bu nhiet do: DS18B20 ");
    uart_put_signed_centi(DS18B20_T_OFFSET_X100);
    uart_puts(" C, SHT ");
    uart_put_signed_centi(SHT_T_OFFSET_X100);
    uart_puts(" C (sua trong board.h)\n");
    uart_puts("Chu ky do: ");
    uart_put_u32(MEASURE_PERIOD_MS / 1000U);
    uart_puts(" s\n\n");

    for (;;) {
        uint32_t t0 = hal_millis();
        int32_t ds_t = 0, sht_t = 0, sht_rh = 0;
        ds18b20_status_t ds_st;
        air_status_t sht_st;

        hal_iwdg_kick();

        /* Cam bien bi rut ra / cam lai: tu khoi tao lai */
        if (ds_state != DS18B20_OK) {
            ds_state = ds18b20_init();
            if (ds_state == DS18B20_OK) { print_ds_init(ds_state); }
        }
        if (sht_state != AIR_OK) {
            sht_state = air_init();
            if (sht_state == AIR_OK) { print_sht_init(sht_state); }
        }

        ds_st  = (ds_state  == DS18B20_OK) ? ds18b20_read(&ds_t)        : ds_state;
        sht_st = (sht_state == AIR_OK)     ? air_read(&sht_t, &sht_rh)   : sht_state;
        /* Bu sai so (board.h) */
        if (ds_st == DS18B20_OK) { ds_t  += DS18B20_T_OFFSET_X100; }
        if (sht_st == AIR_OK)    { sht_t += SHT_T_OFFSET_X100; }

        if (ds_st == DS18B20_ERR_NO_PRESENCE || ds_st == DS18B20_ERR_BUS_LOW) {
            ds_state = ds_st;
        }
        if ((sht_st == AIR_ERR_NOT_FOUND) || (sht_st == AIR_ERR_DATA_LOW) ||
            ((air_kind() == AIR_SHT1X) && (sht_st != AIR_OK))) {   /* SHT1x loi -> khoi tao lai (connection reset) */
            sht_state = sht_st;
        }

        uart_puts("[");
        uart_put_u32(t0 / 1000U);
        uart_puts(" s] DS18B20: ");
        if (ds_st == DS18B20_OK) {
            uart_put_centi(ds_t);
            uart_puts(" C");
        } else {
            uart_puts(ds_err_str(ds_st));
        }

        uart_puts(" | ");
        uart_puts(air_name());
        uart_puts(": ");
        if (sht_st == AIR_OK) {
            uart_put_centi(sht_t);
            uart_puts(" C, ");
            uart_put_centi(sht_rh);
            uart_puts(" %RH");
        } else {
            uart_puts(air_err_str(sht_st));
        }

        if (ds_st == DS18B20_OK && sht_st == AIR_OK) {
            int32_t d = ds_t - sht_t;
            uart_puts(" | Lech (DS-SHT): ");
            if (d >= 0) { uart_putc('+'); }
            uart_put_centi(d);
            uart_puts(" C");
            /* Tong sai so cho phep: DS18B20 +/-0.5 C, SHT3x +/-0.3 C (SHT1x: SHT10 +/-0.5, SHT11 +/-0.4 C) */
            if (d > 80 || d < -80) {
                uart_puts(" (!) vuot sai so ghep +/-0.8C");
            }
        }
        uart_puts("\n");

        /* Cho het chu ky (co kick watchdog) */
        while ((hal_millis() - t0) < MEASURE_PERIOD_MS) {
            hal_iwdg_kick();
            hal_delay_ms(50);
        }
    }
}
