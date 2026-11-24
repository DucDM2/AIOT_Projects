/*
 * hal.c - Lop phan cung toi gian, chi dung thanh ghi CMSIS (khong can HAL cua ST).
 */
#include "hal.h"

static volatile uint32_t s_ms;

void SysTick_Handler(void)
{
    s_ms++;
}

void hal_clock_init(void)
{
    /* Bat HSI 16 MHz */
    RCC->CR |= RCC_CR_HSION;
    while ((RCC->CR & RCC_CR_HSIRDY) == 0U) { }

    /* Flash: 16 MHz o dai dien ap 2 (mac dinh sau reset) can 1 wait state.
     * Thu tu bat buoc: ACC64 -> PRFTEN -> LATENCY. */
    FLASH->ACR |= FLASH_ACR_ACC64;
    FLASH->ACR |= FLASH_ACR_PRFTEN;
    FLASH->ACR |= FLASH_ACR_LATENCY;
    while ((FLASH->ACR & FLASH_ACR_LATENCY) == 0U) { }

    /* AHB/APB1/APB2 khong chia; SYSCLK = HSI */
    RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2);
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_HSI;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI) { }

    /* Tat MSI cho do ton dien */
    RCC->CR &= ~RCC_CR_MSION;

    SystemCoreClockUpdate();

    /* SysTick 1 ms (dong thoi la co so cho hal_delay_us) */
    SysTick_Config(CPU_HZ / 1000U);
}

uint32_t hal_millis(void)
{
    return s_ms;
}

void hal_delay_ms(uint32_t ms)
{
    uint32_t start = s_ms;
    while ((s_ms - start) < ms) {
        __WFI();
    }
}

/* Delay micro-giay dua tren bo dem xuong SysTick->VAL (16 tick = 1 us).
 * Van chay dung khi ngat bi tat (dung trong cac slot 1-Wire). */
void hal_delay_us(uint32_t us)
{
    uint32_t reload  = SysTick->LOAD + 1U;
    uint32_t ticks   = us * (CPU_HZ / 1000000U);
    uint32_t elapsed = 0U;
    uint32_t prev    = SysTick->VAL;

    while (elapsed < ticks) {
        uint32_t now = SysTick->VAL;
        elapsed += (prev >= now) ? (prev - now) : (prev + reload - now);
        prev = now;
    }
}

/* ---------------- GPIO ---------------- */

void hal_gpio_enable_clock(GPIO_TypeDef *port)
{
    if (port == GPIOA)      { RCC->AHBENR |= RCC_AHBENR_GPIOAEN; }
    else if (port == GPIOB) { RCC->AHBENR |= RCC_AHBENR_GPIOBEN; }
    else if (port == GPIOC) { RCC->AHBENR |= RCC_AHBENR_GPIOCEN; }
    (void)RCC->AHBENR;   /* doc lai de dam bao clock da bat */
}

void hal_gpio_open_drain(GPIO_TypeDef *port, uint32_t pin)
{
    hal_gpio_enable_clock(port);
    port->BSRR    = (1UL << pin);                                   /* nha duong (muc 1) */
    port->OTYPER |= (1UL << pin);                                   /* open-drain */
    port->OSPEEDR = (port->OSPEEDR & ~(3UL << (pin * 2U))) | (2UL << (pin * 2U)); /* high speed */
    port->PUPDR   = (port->PUPDR   & ~(3UL << (pin * 2U))) | (1UL << (pin * 2U)); /* pull-up noi */
    port->MODER   = (port->MODER   & ~(3UL << (pin * 2U))) | (1UL << (pin * 2U)); /* output */
}

/* ---------------- UART (USART1: PA9 TX, PA10 RX, AF7) ---------------- */

void uart_init(uint32_t baud)
{
    hal_gpio_enable_clock(GPIOA);
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;

    /* PA9, PA10 -> Alternate function 7 */
    GPIOA->MODER   = (GPIOA->MODER & ~((3UL << 18) | (3UL << 20))) | (2UL << 18) | (2UL << 20);
    GPIOA->OSPEEDR |= (2UL << 18) | (2UL << 20);
    GPIOA->PUPDR   = (GPIOA->PUPDR & ~((3UL << 18) | (3UL << 20))) | (1UL << 20); /* pull-up cho RX */
    GPIOA->AFR[1]  = (GPIOA->AFR[1] & ~((0xFUL << 4) | (0xFUL << 8))) | (7UL << 4) | (7UL << 8);

    USART1->CR1 = 0U;
    USART1->CR2 = 0U;
    USART1->CR3 = 0U;
    USART1->BRR = (CPU_HZ + baud / 2U) / baud;   /* 16 MHz / 115200 = 139 (sai so 0.08%) */
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

void uart_putc(char c)
{
    while ((USART1->SR & USART_SR_TXE) == 0U) { }
    USART1->DR = (uint8_t)c;
}

void uart_puts(const char *s)
{
    while (*s != '\0') {
        if (*s == '\n') {
            uart_putc('\r');
        }
        uart_putc(*s++);
    }
}

void uart_put_u32(uint32_t v)
{
    char buf[11];
    int i = 0;
    do {
        buf[i++] = (char)('0' + (v % 10U));
        v /= 10U;
    } while (v != 0U);
    while (i > 0) {
        uart_putc(buf[--i]);
    }
}

void uart_put_hex8(uint8_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    uart_putc(hex[v >> 4]);
    uart_putc(hex[v & 0x0FU]);
}

void uart_put_centi(int32_t v)
{
    uint32_t a;
    if (v < 0) {
        uart_putc('-');
        a = (uint32_t)(-v);
    } else {
        a = (uint32_t)v;
    }
    uart_put_u32(a / 100U);
    uart_putc('.');
    uart_putc((char)('0' + (a / 10U) % 10U));
    uart_putc((char)('0' + a % 10U));
}

/* ---------------- Watchdog ---------------- */

void hal_iwdg_start(void)
{
    IWDG->KR  = 0xCCCCU;     /* khoi dong IWDG (tu bat LSI) */
    IWDG->KR  = 0x5555U;     /* mo khoa PR/RLR */
    IWDG->PR  = 6U;          /* LSI ~37 kHz / 256 */
    IWDG->RLR = 0x0FFFU;     /* 4096 / 144.5 Hz ~ 28 s */
    while (IWDG->SR != 0U) { }
    IWDG->KR  = 0xAAAAU;
}

void hal_iwdg_kick(void)
{
    IWDG->KR = 0xAAAAU;
}

const char *hal_reset_cause(void)
{
    uint32_t csr = RCC->CSR;
    const char *cause = "khac";

    if (csr & RCC_CSR_IWDGRSTF)      { cause = "watchdog (IWDG)"; }
    else if (csr & RCC_CSR_SFTRSTF)  { cause = "phan mem"; }
    else if (csr & RCC_CSR_PORRSTF)  { cause = "cap nguon (POR)"; }
    else if (csr & RCC_CSR_PINRSTF)  { cause = "nut RESET"; }
    else if (csr & RCC_CSR_LPWRRSTF) { cause = "low-power"; }

    RCC->CSR |= RCC_CSR_RMVF;   /* xoa co */
    return cause;
}
