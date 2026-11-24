/**
 * @file    led_pattern.c
 * @brief   Kiểu nháy LED – xem led_pattern.h.
 */
#include "led_pattern.h"

bool led_pattern_is_on(led_mode_t mode, uint32_t now_ms)
{
    bool on;

    switch (mode)
    {
        case LED_MODE_ON:
            on = true;
            break;
        case LED_MODE_BLINK_FAST:
            on = ((now_ms % LED_FAST_PERIOD_MS) < (LED_FAST_PERIOD_MS / 2UL));
            break;
        case LED_MODE_BLINK_SLOW:
            on = ((now_ms % LED_SLOW_PERIOD_MS) < (LED_SLOW_PERIOD_MS / 2UL));
            break;
        case LED_MODE_OFF:
        default:
            on = false;
            break;
    }
    return on;
}
