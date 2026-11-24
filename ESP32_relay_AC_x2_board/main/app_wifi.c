/**
 * @file    app_wifi.c
 * @brief   Wi-Fi AP + STA – xem app_wifi.h.
 *
 * Khi Wi-Fi nhà không tìm thấy, ESP32 phải quét kênh làm AP bị rớt kết nối
 * → 5 lần thử nhanh đầu tiên, sau đó thử lại mỗi 30 s để AP vẫn dùng được.
 */
#include "app_wifi.h"
#include "app_config.h"
#include "str_buf.h"

#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#define WIFI_FAST_RETRIES      (5U)
#define WIFI_SLOW_RETRY_US     (30000000ULL)   /* 30 s */
#define WIFI_AP_CHANNEL        (1U)
#define WIFI_AP_MAX_CLIENTS    (4U)
#define WIFI_MIN_PASS_LEN      (8U)

static const char *const TAG = "wifi";

static volatile bool s_sta_connected = false;
static bool s_sta_enabled = false;
static esp_timer_handle_t s_retry_timer = NULL;

static void copy_str(uint8_t *dst, size_t cap, const char *src)
{
    /* Sao chép có giới hạn, luôn kết thúc '\0' (wifi_config_t dùng mảng uint8_t). */
    size_t i = 0U;
    while (((i + 1U) < cap) && (src[i] != '\0'))
    {
        dst[i] = (uint8_t)src[i];
        i++;
    }
    dst[i] = 0U;
}

static void retry_timer_cb(void *arg)
{
    (void)arg;
    if (s_sta_enabled && (!s_sta_connected))
    {
        (void)esp_wifi_connect();
    }
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    static uint32_t s_retry = 0U; /* chỉ dùng trong hàm này (Rule 8.9) */
    (void)arg;

    if (base == WIFI_EVENT)
    {
        switch (id)
        {
            case WIFI_EVENT_STA_START:
                if (s_sta_enabled)
                {
                    (void)esp_wifi_connect();
                }
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
            {
                const wifi_event_sta_disconnected_t *ev = (const wifi_event_sta_disconnected_t *)data;
                s_sta_connected = false;
                s_retry++;
                ESP_LOGW(TAG, "Mat ket noi Wi-Fi nha (reason=%u, lan %u)",
                         (unsigned)ev->reason, (unsigned)s_retry);
                if (s_retry <= WIFI_FAST_RETRIES)
                {
                    (void)esp_wifi_connect();
                }
                else
                {
                    (void)esp_timer_start_once(s_retry_timer, WIFI_SLOW_RETRY_US);
                }
                break;
            }
            case WIFI_EVENT_AP_STACONNECTED:
                ESP_LOGI(TAG, "Co may noi vao AP");
                break;
            default:
                break;
        }
    }
    else if ((base == IP_EVENT) && (id == IP_EVENT_STA_GOT_IP))
    {
        const ip_event_got_ip_t *ev = (const ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Da noi Wi-Fi nha, IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        s_retry = 0U;
        s_sta_connected = true;
    }
    else
    {
        /* sự kiện khác – bỏ qua */
    }
}

void app_wifi_start(const char *device_id)
{
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t ap_cfg;
    wifi_config_t sta_cfg;
    const esp_timer_create_args_t timer_args = {
        .callback = &retry_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "wifi_retry",
        .skip_unhandled_events = true
    };
    const bool ap_enabled = (strlen(APP_AP_PASS) >= WIFI_MIN_PASS_LEN);
    char ssid[32];
    str_buf_t sb;
    wifi_mode_t mode;

    s_sta_enabled = (strlen(APP_WIFI_SSID) > 0U);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    (void)esp_netif_create_default_wifi_sta();
    (void)esp_netif_create_default_wifi_ap();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_retry_timer));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_wifi_event, NULL));

    /* ---- AP riêng của thiết bị ---- */
    (void)memset(&ap_cfg, 0, sizeof(ap_cfg));
    (void)sb_init(&sb, ssid, sizeof(ssid));
    sb_append_str(&sb, "ESP32-Relay-");
    sb_append_str(&sb, &device_id[6]); /* bỏ tiền tố "relay-" */
    copy_str(ap_cfg.ap.ssid, sizeof(ap_cfg.ap.ssid), ssid);
    ap_cfg.ap.ssid_len = (uint8_t)sb.len;
    copy_str(ap_cfg.ap.password, sizeof(ap_cfg.ap.password), APP_AP_PASS);
    ap_cfg.ap.channel = WIFI_AP_CHANNEL;
    ap_cfg.ap.max_connection = WIFI_AP_MAX_CLIENTS;
    ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_cfg.ap.pmf_cfg.required = false;

    /* ---- Wi-Fi nhà ---- */
    (void)memset(&sta_cfg, 0, sizeof(sta_cfg));
    copy_str(sta_cfg.sta.ssid, sizeof(sta_cfg.sta.ssid), APP_WIFI_SSID);
    copy_str(sta_cfg.sta.password, sizeof(sta_cfg.sta.password), APP_WIFI_PASS);
    sta_cfg.sta.threshold.authmode = (strlen(APP_WIFI_PASS) > 0U) ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    sta_cfg.sta.pmf_cfg.capable = true;
    sta_cfg.sta.pmf_cfg.required = false;

    if (ap_enabled && s_sta_enabled)
    {
        mode = WIFI_MODE_APSTA;
    }
    else if (s_sta_enabled)
    {
        mode = WIFI_MODE_STA;
    }
    else
    {
        mode = WIFI_MODE_AP;
    }

    if (!ap_enabled)
    {
        ESP_LOGE(TAG, "APP_AP_PASS < 8 ky tu -> KHONG phat AP rieng.");
    }

    if ((!ap_enabled) && (!s_sta_enabled))
    {
        /* Không để ESP32 phát AP mặc định không mật khẩu. */
        ESP_LOGE(TAG, "Khong co cau hinh Wi-Fi nao -> tat Wi-Fi.");
    }
    else
    {
        ESP_ERROR_CHECK(esp_wifi_set_mode(mode));
        if (ap_enabled)
        {
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
            ESP_LOGI(TAG, "AP: \"%s\" -> http://192.168.4.1", ssid);
        }
        if (s_sta_enabled)
        {
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
            ESP_LOGI(TAG, "Dang noi Wi-Fi nha \"%s\"...", APP_WIFI_SSID);
        }
        ESP_ERROR_CHECK(esp_wifi_start());
    }
}

bool app_wifi_sta_connected(void)
{
    return s_sta_connected;
}

bool app_wifi_get_rssi(int32_t *rssi)
{
    bool ok = false;
    wifi_ap_record_t info;

    if (s_sta_connected && (rssi != NULL) && (esp_wifi_sta_get_ap_info(&info) == ESP_OK))
    {
        *rssi = (int32_t)info.rssi;
        ok = true;
    }
    return ok;
}
