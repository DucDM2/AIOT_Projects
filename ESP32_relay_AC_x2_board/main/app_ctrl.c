/**
 * @file    app_ctrl.c
 * @brief   Bộ điều phối relay – xem app_ctrl.h.
 */
#include "app_ctrl.h"
#include "app_config.h"
#include "app_mqtt.h"
#include "app_wifi.h"
#include "gpio_drv.h"
#include "relay.h"
#include "str_buf.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *const TAG = "ctrl";

static SemaphoreHandle_t s_lock = NULL;
static uint32_t s_seq = 0UL;
static char s_device_id[APP_DEVICE_ID_LEN];

static void lock(void)
{
    (void)xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    (void)xSemaphoreGive(s_lock);
}

static void build_device_id(void)
{
    uint8_t mac[6] = {0U, 0U, 0U, 0U, 0U, 0U};
    str_buf_t sb;

    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK)
    {
        ESP_LOGE(TAG, "Khong doc duoc MAC -> device_id = relay-000000");
    }
    if (sb_init(&sb, s_device_id, sizeof(s_device_id)) == RC_OK)
    {
        sb_append_str(&sb, "relay-");
        sb_append_hex8(&sb, mac[3]);
        sb_append_hex8(&sb, mac[4]);
        sb_append_hex8(&sb, mac[5]);
    }
}

void app_ctrl_init(void)
{
    relay_config_t cfg;
    rc_status_t rc;
    const gpio_level_t led_off = APP_LED_ACTIVE_LOW ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW;

    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock != NULL);

    gpio_drv_init();

    cfg.pin[0] = APP_PIN_RELAY1;
    cfg.pin[1] = APP_PIN_RELAY2;
    cfg.active_high = APP_RELAY_ACTIVE_HIGH;
    cfg.min_switch_ms = APP_RELAY_MIN_SWITCH_MS;

    rc = relay_init(&cfg);
    if (rc != RC_OK)
    {
        ESP_LOGE(TAG, "relay_init loi: %s", rc_status_name(rc));
    }

    rc = gpio_drv_config_output(APP_PIN_LED, led_off);
    if (rc != RC_OK)
    {
        ESP_LOGE(TAG, "LED GPIO%u loi: %s", (unsigned)APP_PIN_LED, rc_status_name(rc));
    }

    rc = gpio_drv_config_input(APP_PIN_BUTTON);
    if (rc != RC_OK)
    {
        ESP_LOGE(TAG, "Nut GPIO%u loi: %s", (unsigned)APP_PIN_BUTTON, rc_status_name(rc));
    }

    build_device_id();

    ESP_LOGI(TAG, "Thiet bi %s | relay1=GPIO%u relay2=GPIO%u (%s) | self-check: %s",
             s_device_id, (unsigned)APP_PIN_RELAY1, (unsigned)APP_PIN_RELAY2,
             APP_RELAY_ACTIVE_HIGH ? "active-high" : "active-low",
             rc_status_name(relay_self_check()));
}

const char *app_ctrl_device_id(void)
{
    return s_device_id;
}

uint32_t app_ctrl_now_ms(void)
{
    const int64_t us = esp_timer_get_time();
    return (uint32_t)((uint64_t)us / 1000ULL);
}

rc_status_t app_ctrl_execute(const cmd_t *cmd, status_reason_t reason)
{
    rc_status_t rc = RC_ERR_ARG;
    bool changed = false;
    bool publish = false;

    if (cmd != NULL)
    {
        const uint32_t now = app_ctrl_now_ms();

        lock();
        switch (cmd->kind)
        {
            case CMD_KIND_SET:
                rc = relay_set(cmd->relay, cmd->on, now, &changed);
                break;
            case CMD_KIND_TOGGLE:
                rc = relay_toggle(cmd->relay, now, &changed);
                break;
            case CMD_KIND_QUERY:
                rc = RC_OK;
                publish = true;
                break;
            case CMD_KIND_NONE:
            default:
                rc = RC_ERR_ARG;
                break;
        }
        if (changed)
        {
            s_seq++;
            publish = true;
            if (relay_self_check() != RC_OK)
            {
                ESP_LOGE(TAG, "Thanh ghi GPIO_OUT KHONG khop trang thai relay!");
            }
        }
        unlock();

        ESP_LOGI(TAG, "[%s] relay%u kind=%d -> %s, mask=0x%02x",
                 status_reason_name(reason), (unsigned)cmd->relay, (int)cmd->kind,
                 rc_status_name(rc), (unsigned)relay_get_mask());

        if (publish)
        {
            app_ctrl_publish_status(reason);
        }
    }
    return rc;
}

rc_status_t app_ctrl_build_status(status_reason_t reason, char *out, size_t cap, size_t *out_len)
{
    status_info_t info;
    int32_t rssi = 0;

    lock();
    info.relay_mask = relay_get_mask();
    info.seq = s_seq;
    unlock();

    info.device_id = s_device_id;
    info.fw_version = APP_FW_VERSION;
    info.uptime_s = app_ctrl_now_ms() / 1000UL;
    info.rssi_valid = app_wifi_get_rssi(&rssi);
    info.rssi = rssi;
    info.mqtt_connected = app_mqtt_is_connected();
    info.reason = reason;

    return status_json_build(&info, out, cap, out_len);
}

void app_ctrl_publish_status(status_reason_t reason)
{
    char json[APP_STATUS_JSON_MAX];
    size_t len = 0U;

    if (app_mqtt_is_connected() &&
        (app_ctrl_build_status(reason, json, sizeof(json), &len) == RC_OK))
    {
        app_mqtt_publish_state(json, len);
    }
}

rc_status_t app_ctrl_self_check(void)
{
    rc_status_t rc;

    lock();
    rc = relay_self_check();
    unlock();
    return rc;
}
