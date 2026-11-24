/**
 * @file    relay.c
 * @brief   Điều khiển relay – xem relay.h.
 */
#include "relay.h"
#include "gpio_drv.h"

typedef struct
{
    relay_config_t cfg;
    bool           initialised;
    bool           state[RELAY_COUNT];
    bool           has_switched[RELAY_COUNT];
    uint32_t       last_switch_ms[RELAY_COUNT];
    uint32_t       switch_count[RELAY_COUNT];
} relay_ctx_t;

static relay_ctx_t s_ctx;

static bool channel_to_index(uint8_t channel, uint8_t *index)
{
    bool ok = false;
    if ((channel >= 1U) && (channel <= RELAY_COUNT))
    {
        *index = (uint8_t)(channel - 1U);
        ok = true;
    }
    return ok;
}

static gpio_level_t state_to_level(bool on)
{
    gpio_level_t level;
    if (on == s_ctx.cfg.active_high)
    {
        level = GPIO_LEVEL_HIGH;
    }
    else
    {
        level = GPIO_LEVEL_LOW;
    }
    return level;
}

static bool level_to_state(gpio_level_t level)
{
    const bool high = (level == GPIO_LEVEL_HIGH);
    return (high == s_ctx.cfg.active_high);
}

rc_status_t relay_init(const relay_config_t *cfg)
{
    rc_status_t rc = RC_ERR_ARG;

    s_ctx.initialised = false;

    if (cfg != NULL)
    {
        bool pins_ok = (cfg->pin[0] != cfg->pin[1]);
        for (uint8_t i = 0U; i < RELAY_COUNT; i++)
        {
            if (!gpio_drv_pin_is_usable(cfg->pin[i]))
            {
                pins_ok = false;
            }
        }

        if (pins_ok)
        {
            s_ctx.cfg = *cfg;
            rc = RC_OK;
            for (uint8_t i = 0U; i < RELAY_COUNT; i++)
            {
                s_ctx.state[i] = false;
                s_ctx.has_switched[i] = false;
                s_ctx.last_switch_ms[i] = 0UL;
                s_ctx.switch_count[i] = 0UL;
                if (rc == RC_OK)
                {
                    rc = gpio_drv_config_output(cfg->pin[i], state_to_level(false));
                }
            }
            s_ctx.initialised = (rc == RC_OK);
        }
    }
    return rc;
}

rc_status_t relay_set(uint8_t channel, bool on, uint32_t now_ms, bool *changed)
{
    rc_status_t rc = RC_ERR_ARG;
    uint8_t idx = 0U;
    bool did_change = false;

    if (!s_ctx.initialised)
    {
        rc = RC_ERR_STATE;
    }
    else if (channel_to_index(channel, &idx))
    {
        if (s_ctx.state[idx] == on)
        {
            rc = RC_OK; /* Không đổi – không tính là một lần đóng/cắt. */
        }
        else if (s_ctx.has_switched[idx] &&
                 ((uint32_t)(now_ms - s_ctx.last_switch_ms[idx]) < s_ctx.cfg.min_switch_ms))
        {
            rc = RC_ERR_BUSY; /* Phép trừ không dấu xử lý đúng khi bộ đếm ms tràn vòng. */
        }
        else
        {
            rc = gpio_drv_write(s_ctx.cfg.pin[idx], state_to_level(on));
            if (rc == RC_OK)
            {
                s_ctx.state[idx] = on;
                s_ctx.has_switched[idx] = true;
                s_ctx.last_switch_ms[idx] = now_ms;
                s_ctx.switch_count[idx]++;
                did_change = true;
            }
        }
    }
    else
    {
        /* rc giữ RC_ERR_ARG */
    }

    if (changed != NULL)
    {
        *changed = did_change;
    }
    return rc;
}

rc_status_t relay_toggle(uint8_t channel, uint32_t now_ms, bool *changed)
{
    rc_status_t rc = RC_ERR_ARG;
    uint8_t idx = 0U;

    if (!s_ctx.initialised)
    {
        rc = RC_ERR_STATE;
        if (changed != NULL)
        {
            *changed = false;
        }
    }
    else if (channel_to_index(channel, &idx))
    {
        rc = relay_set(channel, !s_ctx.state[idx], now_ms, changed);
    }
    else
    {
        if (changed != NULL)
        {
            *changed = false;
        }
    }
    return rc;
}

rc_status_t relay_get(uint8_t channel, bool *on)
{
    rc_status_t rc = RC_ERR_ARG;
    uint8_t idx = 0U;

    if (!s_ctx.initialised)
    {
        rc = RC_ERR_STATE;
    }
    else if (channel_to_index(channel, &idx) && (on != NULL))
    {
        gpio_level_t level = GPIO_LEVEL_LOW;
        rc = gpio_drv_read_output(s_ctx.cfg.pin[idx], &level);
        if (rc == RC_OK)
        {
            *on = level_to_state(level);
        }
    }
    else
    {
        /* rc giữ RC_ERR_ARG */
    }
    return rc;
}

uint8_t relay_get_mask(void)
{
    uint8_t mask = 0U;

    if (s_ctx.initialised)
    {
        for (uint8_t i = 0U; i < RELAY_COUNT; i++)
        {
            if (s_ctx.state[i])
            {
                mask |= (uint8_t)(1U << i);
            }
        }
    }
    return mask;
}

uint32_t relay_get_switch_count(uint8_t channel)
{
    uint32_t count = 0UL;
    uint8_t idx = 0U;

    if (s_ctx.initialised && channel_to_index(channel, &idx))
    {
        count = s_ctx.switch_count[idx];
    }
    return count;
}

rc_status_t relay_self_check(void)
{
    rc_status_t rc = RC_ERR_STATE;

    if (s_ctx.initialised)
    {
        rc = RC_OK;
        for (uint8_t i = 0U; i < RELAY_COUNT; i++)
        {
            bool hw_on = false;
            const rc_status_t r = relay_get((uint8_t)(i + 1U), &hw_on);
            if ((r != RC_OK) || (hw_on != s_ctx.state[i]))
            {
                rc = RC_ERR_STATE;
            }
        }
    }
    return rc;
}
