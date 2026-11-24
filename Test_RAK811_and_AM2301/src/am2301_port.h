/*
 * am2301_port.h - Lop ghep noi phan cung cua driver AM2301.
 *
 * Driver (am2301.c) chi goi cac ham duoi day, nho vay CUNG MOT ma driver chay duoc:
 *   - tren RAK811 that / Renode: am2301_port.c (GPIO PB5 + bo dem SysTick)
 *   - tren PC: test/test_am2301.c (mo hinh cam bien theo thoi gian ao)
 */
#ifndef AM2301_PORT_H
#define AM2301_PORT_H

#include <stdint.h>
#include <stdbool.h>

/* So tick cua am_port_ticks() trong 1 micro-giay (16 MHz -> 16) */
#define AM_TICKS_PER_US 16U

void     am_port_init(void);        /* cau hinh chan, nha bus */
void     am_port_drive_low(void);   /* MCU keo SDA xuong 0 (output) */
void     am_port_release(void);     /* MCU nha SDA (input, dien tro keo len dua len 1) */
uint32_t am_port_millis(void);

#if defined(AM2301_HOST_TEST)
bool     am_port_read(void);        /* muc logic hien tai cua SDA */
uint32_t am_port_ticks(void);       /* bo dem tick tang deu 32 bit */
#else
/* Tren STM32: 2 ham nam trong vong do xung -> inline de vong lap nhanh (~1.5 us thay vi ~4 us) */
#include "board.h"

extern uint32_t am_port_tick_sum;   /* tong tick da dem */
extern uint32_t am_port_tick_prev;  /* SysTick->VAL lan doc truoc */

__attribute__((always_inline)) static inline bool am_port_read(void)
{
    return (AM_PORT->IDR & (1UL << AM_PIN)) != 0U;
}

/* SysTick dem xuong tu LOAD ve 0 moi 1 ms. Cong don do chenh giua 2 lan doc,
 * dung ca khi ngat SysTick chen vao giua. Phai goi it nhat 1 lan moi 1 ms. */
__attribute__((always_inline)) static inline uint32_t am_port_ticks(void)
{
    uint32_t now  = SysTick->VAL;
    uint32_t prev = am_port_tick_prev;
    am_port_tick_sum += (prev >= now) ? (prev - now) : (prev + SysTick->LOAD + 1U - now);
    am_port_tick_prev = now;
    return am_port_tick_sum;
}
#endif

#endif /* AM2301_PORT_H */
