/**
 * @file    app_io.c
 * @brief   Task nút nhấn / LED / heartbeat – xem app_io.h.
 */
#include "app_io.h"
#include "app_config.h"
#include "app_ctrl.h"
#include "app_mqtt.h"
#include "app_wifi.h"
#include "button.h"
#include "gpio_drv.h"
#include "led_pattern.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define IO_PERIOD_MS          (10U)
#define IO_SELF_CHECK_MS      (5000UL)
#define IO_TASK_STACK         (4096U)
#define IO_TASK_PRIO          (5U)

static const char *const TAG = "io";

static bool button_pressed(void)
{
    gpio_level_t level = GPIO_LEVEL_HIGH;
    /* IO0 có điện trở kéo lên → nhấn = mức THẤP. */
    (void)gpio_drv_read_input(APP_PIN_BUTTON, &level);
    return level == GPIO_LEVEL_LOW;
}

static led_mode_t current_led_mode(void)
{
    led_mode_t mode;

    if (app_mqtt_is_connected())
    {
        mode = LED_MODE_ON;
    }
    else if (app_wifi_sta_connected())
    {
        mode = LED_MODE_BLINK_SLOW;
    }
    else
    {
        mode = LED_MODE_BLINK_FAST;
    }
    return mode;
}

static void drive_led(uint32_t now_ms)
{
    const bool on = led_pattern_is_on(current_led_mode(), now_ms);
    const bool high = (on != APP_LED_ACTIVE_LOW);
    (void)gpio_drv_write(APP_PIN_LED, high ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);
}

static void handle_button(button_event_t evt)
{
    cmd_t cmd;

    cmd.kind = CMD_KIND_TOGGLE;
    cmd.on = false;
    cmd.relay = 0U;

    if (evt == BUTTON_EVT_SHORT)
    {
        cmd.relay = 1U;
    }
    else if (evt == BUTTON_EVT_LONG)
    {
        cmd.relay = 2U;
    }
    else
    {
        /* không có sự kiện */
    }

    if (cmd.relay != 0U)
    {
        (void)app_ctrl_execute(&cmd, STATUS_REASON_BUTTON);
    }
}

static void io_task(void *arg)
{
    button_t btn;
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t now = app_ctrl_now_ms();
    uint32_t last_heartbeat = now;
    uint32_t last_check = now;
    (void)arg;

    (void)button_init(&btn, APP_BUTTON_DEBOUNCE_MS, APP_BUTTON_LONG_MS, button_pressed(), now);

    for (;;)
    {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(IO_PERIOD_MS));
        now = app_ctrl_now_ms();

        handle_button(button_update(&btn, button_pressed(), now));
        drive_led(now);

        if ((uint32_t)(now - last_heartbeat) >= APP_HEARTBEAT_MS)
        {
            last_heartbeat = now;
            app_ctrl_publish_status(STATUS_REASON_HEARTBEAT);
        }

        if ((uint32_t)(now - last_check) >= IO_SELF_CHECK_MS)
        {
            last_check = now;
            if (app_ctrl_self_check() != RC_OK)
            {
                ESP_LOGE(TAG, "Self-check: thanh ghi GPIO_OUT lech trang thai relay!");
            }
        }
    }
}

void app_io_start(void)
{
    const BaseType_t ok = xTaskCreate(&io_task, "app_io", IO_TASK_STACK, NULL, IO_TASK_PRIO, NULL);
    if (ok != pdPASS)
    {
        ESP_LOGE(TAG, "Khong tao duoc task app_io");
    }
}
