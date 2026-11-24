/*
 * hal.h - Clock, delay, GPIO, UART, watchdog toi gian cho STM32L151 (RAK811).
 */
#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32l1xx.h"

/* Clock: HSI 16 MHz (khong phu thuoc thach anh ngoai). */
#define CPU_HZ 16000000U

void     hal_clock_init(void);
uint32_t hal_millis(void);
void     hal_delay_ms(uint32_t ms);
void     hal_delay_us(uint32_t us);

/* GPIO */
void hal_gpio_enable_clock(GPIO_TypeDef *port);
void hal_gpio_open_drain(GPIO_TypeDef *port, uint32_t pin);   /* output OD + pull-up noi */
void hal_gpio_output(GPIO_TypeDef *port, uint32_t pin, bool level); /* output push-pull */
void hal_gpio_input(GPIO_TypeDef *port, uint32_t pin);       /* input, khong pull */
void hal_gpio_input_pullup(GPIO_TypeDef *port, uint32_t pin); /* input, pull-up noi ~40k */
void hal_gpio_af(GPIO_TypeDef *port, uint32_t pin, uint32_t af); /* alternate function, high speed */

static inline void gpio_high(GPIO_TypeDef *port, uint32_t pin) { port->BSRR = (1UL << pin); }
static inline void gpio_low(GPIO_TypeDef *port, uint32_t pin)  { port->BSRR = (1UL << (pin + 16U)); }
static inline bool gpio_read(GPIO_TypeDef *port, uint32_t pin) { return (port->IDR & (1UL << pin)) != 0U; }

/* UART log (USART1, PA9/PA10) */
void uart_init(uint32_t baud);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_put_u32(uint32_t v);
void uart_put_hex8(uint8_t v);
void uart_put_centi(int32_t v);   /* in so co 2 chu so thap phan: 2531 -> "25.31" */
void uart_put_i32(int32_t v);
int  uart_getc(void);             /* khong chan: tra ve -1 neu chua co ky tu */

/* ID duy nhat 96-bit cua chip, rut gon con 16 bit lam ID node */
uint16_t hal_node_id(void);

/* Data EEPROM noi (4 KB @ 0x08080000): doc/ghi 1 word, giu duoc khi mat nguon */
uint32_t hal_eeprom_read(uint32_t offset);
bool     hal_eeprom_write(uint32_t offset, uint32_t value);

/* Watchdog doc lap (~28 s) */
void hal_iwdg_start(void);
void hal_iwdg_kick(void);

/* Nguyen nhan reset (doc 1 lan luc khoi dong) */
const char *hal_reset_cause(void);

#endif /* HAL_H */
