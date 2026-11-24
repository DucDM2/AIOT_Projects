/*
 * am2301_port.c - Ghep driver AM2301 voi STM32L151: chan PB5, bo dem SysTick 16 MHz.
 *
 * Cach dieu khien chan dung theo datasheet AM2301 muc 5.4 buoc 2:
 *   "I/O dat output, xuat muc thap >= 1 ms, roi dat muc cao va chuyen ngay sang input".
 *   - Keo xuong : ODR = 0, MODER = output (kieu open-drain -> khong bao gio day 3.3 V vao bus)
 *   - Nha bus   : ODR = 1 (open-drain = hi-Z), roi MODER = input
 * Pull-up noi (~40 kOhm) chi la du phong, VAN CAN dien tro keo len ngoai 4.7-5.1 kOhm.
 */
#include "am2301_port.h"
#include "board.h"
#include "hal.h"

uint32_t am_port_tick_sum;
uint32_t am_port_tick_prev;

void am_port_init(void)
{
    hal_gpio_enable_clock(AM_PORT);
    AM_PORT->BSRR    = (1UL << AM_PIN);                                         /* ODR = 1 */
    AM_PORT->OTYPER |= (1UL << AM_PIN);                                         /* open-drain */
    AM_PORT->OSPEEDR = (AM_PORT->OSPEEDR & ~(3UL << (AM_PIN * 2U))) | (1UL << (AM_PIN * 2U)); /* medium */
    AM_PORT->PUPDR   = (AM_PORT->PUPDR   & ~(3UL << (AM_PIN * 2U))) | (1UL << (AM_PIN * 2U)); /* pull-up noi */
    AM_PORT->MODER  &= ~(3UL << (AM_PIN * 2U));                                 /* input */
    am_port_tick_prev = SysTick->VAL;
}

void am_port_drive_low(void)
{
    AM_PORT->BSRR  = (1UL << (AM_PIN + 16U));                                   /* ODR = 0 */
    AM_PORT->MODER = (AM_PORT->MODER & ~(3UL << (AM_PIN * 2U))) | (1UL << (AM_PIN * 2U)); /* output */
}

void am_port_release(void)
{
    AM_PORT->BSRR   = (1UL << AM_PIN);                                          /* ODR = 1 (hi-Z) */
    AM_PORT->MODER &= ~(3UL << (AM_PIN * 2U));                                  /* input */
}

uint32_t am_port_millis(void)
{
    return hal_millis();
}
