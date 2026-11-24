/*
 * door.c - Cam bien cua MC-38 NC tren PA15.
 */
#include "door.h"
#include "board.h"
#include "hal.h"

static volatile uint32_t s_edges;     /* tong canh (ngat) */
static uint32_t s_edges_at_change;
static uint32_t s_last_bounces;

static door_state_t s_state;
static bool     s_cand;               /* muc tho dang xet */
static uint32_t s_cand_since;
static uint32_t s_state_since;

void EXTI15_10_IRQHandler(void)
{
    if (EXTI->PR & (1UL << DOOR_PIN)) {
        EXTI->PR = (1UL << DOOR_PIN);   /* ghi 1 de xoa */
        s_edges++;
    }
}

static door_state_t level_to_state(bool level)
{
    return (level == (DOOR_CLOSED_LEVEL != 0)) ? DOOR_CLOSED : DOOR_OPEN;
}

bool door_raw_level(void)
{
    return gpio_read(DOOR_PORT, DOOR_PIN);
}

void door_init(void)
{
    hal_gpio_input_pullup(DOOR_PORT, DOOR_PIN);

    /* EXTI15 <- PA15, ngat ca 2 canh (chi de dem nhieu / danh thuc, khong quyet dinh trang thai) */
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    (void)RCC->APB2ENR;
    SYSCFG->EXTICR[3] = (SYSCFG->EXTICR[3] & ~SYSCFG_EXTICR4_EXTI15) | SYSCFG_EXTICR4_EXTI15_PA;
    EXTI->RTSR |= (1UL << DOOR_PIN);
    EXTI->FTSR |= (1UL << DOOR_PIN);
    EXTI->PR    = (1UL << DOOR_PIN);
    EXTI->IMR  |= (1UL << DOOR_PIN);
    NVIC_SetPriority(EXTI15_10_IRQn, 3);
    NVIC_EnableIRQ(EXTI15_10_IRQn);

    hal_delay_ms(5);                     /* cho pull-up nap day dan */
    s_cand = door_raw_level();
    s_cand_since = hal_millis();
    s_state = level_to_state(s_cand);
    s_state_since = s_cand_since;
    s_edges_at_change = s_edges;
}

door_event_t door_poll(void)
{
    uint32_t now = hal_millis();
    bool lvl = door_raw_level();

    if (lvl != s_cand) {
        s_cand = lvl;
        s_cand_since = now;
        return DOOR_EVT_NONE;
    }
    if ((now - s_cand_since) < DOOR_DEBOUNCE_MS) {
        return DOOR_EVT_NONE;
    }

    door_state_t st = level_to_state(lvl);
    if (st == s_state) {
        return DOOR_EVT_NONE;
    }

    uint32_t e = s_edges;
    s_last_bounces = e - s_edges_at_change;
    s_edges_at_change = e;
    s_state = st;
    s_state_since = s_cand_since;          /* thoi diem bat dau on dinh */
    return (st == DOOR_OPEN) ? DOOR_EVT_OPENED : DOOR_EVT_CLOSED;
}

door_state_t door_state(void)      { return s_state; }
uint32_t     door_since_ms(void)   { return s_state_since; }
uint32_t     door_last_bounces(void) { return s_last_bounces; }
uint32_t     door_edge_total(void) { return s_edges; }

const char *door_state_name(door_state_t s)
{
    return (s == DOOR_CLOSED) ? "DONG" : "MO";
}
