/**
 * @file    button.c
 * @brief   Chống dội phím – xem button.h.
 */
#include "button.h"

rc_status_t button_init(button_t *btn, uint32_t debounce_ms, uint32_t long_ms,
                        bool initial_pressed, uint32_t now_ms)
{
    rc_status_t rc = RC_ERR_ARG;

    if ((btn != NULL) && (long_ms > debounce_ms))
    {
        btn->debounce_ms = debounce_ms;
        btn->long_ms = long_ms;
        btn->raw_change_ms = now_ms;
        btn->press_start_ms = now_ms;
        btn->raw_pressed = initial_pressed;
        btn->stable_pressed = initial_pressed;
        /* Đang giữ lúc khởi động → coi như đã báo, không sinh sự kiện khi nhả. */
        btn->long_reported = initial_pressed;
        rc = RC_OK;
    }
    return rc;
}

button_event_t button_update(button_t *btn, bool raw_pressed, uint32_t now_ms)
{
    button_event_t evt = BUTTON_EVT_NONE;

    if (btn != NULL)
    {
        if (raw_pressed != btn->raw_pressed)
        {
            btn->raw_pressed = raw_pressed;
            btn->raw_change_ms = now_ms;
        }

        if ((btn->raw_pressed != btn->stable_pressed) &&
            ((uint32_t)(now_ms - btn->raw_change_ms) >= btn->debounce_ms))
        {
            btn->stable_pressed = btn->raw_pressed;
            if (btn->stable_pressed)
            {
                btn->press_start_ms = btn->raw_change_ms;
                btn->long_reported = false;
            }
            else if (!btn->long_reported)
            {
                evt = BUTTON_EVT_SHORT;
            }
            else
            {
                /* Nhả sau nhấn giữ – đã báo LONG rồi. */
            }
        }

        if (btn->stable_pressed && (!btn->long_reported) &&
            ((uint32_t)(now_ms - btn->press_start_ms) >= btn->long_ms))
        {
            btn->long_reported = true;
            evt = BUTTON_EVT_LONG;
        }
    }
    return evt;
}
