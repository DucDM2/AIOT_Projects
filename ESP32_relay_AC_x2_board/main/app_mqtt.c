/**
 * @file    app_mqtt.c
 * @brief   MQTTS tới EMQX Cloud – xem app_mqtt.h.
 *
 * TLS: kiểm chứng chỉ máy chủ bằng gói CA gốc của ESP-IDF (esp_crt_bundle) – EMQX Cloud dùng
 * chứng chỉ công khai (DigiCert) nên không cần nạp CA riêng.
 */
#include "app_mqtt.h"
#include "app_config.h"
#include "app_ctrl.h"
#include "cmd_parser.h"
#include "status_json.h"
#include "str_buf.h"

#include <string.h>
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "mqtt_client.h"

#define MQTT_KEEPALIVE_S        (30)
#define MQTT_RECONNECT_MS       (5000)
#define MQTT_ACK_MAX            (64U)

static const char *const TAG = "mqtt";

static esp_mqtt_client_handle_t s_client = NULL;
static volatile bool s_connected = false;
static char s_topic_state[APP_TOPIC_MAX];
static char s_topic_cmd[APP_TOPIC_MAX];
static char s_topic_ack[APP_TOPIC_MAX];

static bool build_topic(char *out, size_t cap, const char *device_id, const char *leaf)
{
    str_buf_t sb;
    bool ok = false;

    if (sb_init(&sb, out, cap) == RC_OK)
    {
        sb_append_str(&sb, APP_TOPIC_PREFIX);
        sb_append_char(&sb, '/');
        sb_append_str(&sb, device_id);
        sb_append_char(&sb, '/');
        sb_append_str(&sb, leaf);
        ok = !sb.overflow;
    }
    return ok;
}

/* Topic trong sự kiện MQTT KHÔNG kết thúc '\0' → so từng ký tự có độ dài (thay memcmp, Rule 21.14). */
static bool topic_is(const char *topic, int topic_len, const char *expected)
{
    const size_t n = strlen(expected);
    bool equal = (topic != NULL) && (topic_len >= 0) && ((size_t)topic_len == n);
    size_t i = 0U;

    while (equal && (i < n))
    {
        equal = (topic[i] == expected[i]);
        i++;
    }
    return equal;
}

static void publish_ack(rc_status_t result)
{
    char ack[MQTT_ACK_MAX];
    size_t len = 0U;

    if (status_json_build_ack(result, ack, sizeof(ack), &len) == RC_OK)
    {
        (void)esp_mqtt_client_enqueue(s_client, s_topic_ack, ack, (int)len, 0, 0, false);
    }
}

static void handle_data(const esp_mqtt_event_t *ev)
{
    cmd_t cmd;
    rc_status_t rc;

    if (!topic_is(ev->topic, ev->topic_len, s_topic_cmd))
    {
        /* topic khác – bỏ qua */
    }
    else if ((ev->current_data_offset != 0) || (ev->total_data_len != ev->data_len) ||
             (ev->data_len < 0))
    {
        ESP_LOGW(TAG, "Lenh bi chia manh/qua dai – bo qua");
        publish_ack(RC_ERR_OVERFLOW);
    }
    else
    {
        rc = cmd_parse(ev->data, (size_t)ev->data_len, &cmd);
        if (rc == RC_OK)
        {
            rc = app_ctrl_execute(&cmd, (cmd.kind == CMD_KIND_QUERY) ?
                                  STATUS_REASON_QUERY : STATUS_REASON_MQTT);
        }
        else
        {
            ESP_LOGW(TAG, "Lenh sai: %s", rc_status_name(rc));
        }
        publish_ack(rc);
    }
}

static void on_mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    const esp_mqtt_event_t *ev = (const esp_mqtt_event_t *)data;
    (void)arg;
    (void)base;

    switch ((esp_mqtt_event_id_t)id)
    {
        case MQTT_EVENT_CONNECTED:
            s_connected = true;
            ESP_LOGI(TAG, "Da noi EMQX. Nghe lenh tai: %s", s_topic_cmd);
            (void)esp_mqtt_client_subscribe(s_client, s_topic_cmd, 1);
            app_ctrl_publish_status(STATUS_REASON_CONNECT);
            break;
        case MQTT_EVENT_DISCONNECTED:
            s_connected = false;
            ESP_LOGW(TAG, "Mat ket noi EMQX – se tu noi lai");
            break;
        case MQTT_EVENT_DATA:
            handle_data(ev);
            break;
        case MQTT_EVENT_ERROR:
            if ((ev->error_handle != NULL) &&
                (ev->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED))
            {
                ESP_LOGE(TAG, "EMQX tu choi ket noi (sai user/pass?) code=%d",
                         (int)ev->error_handle->connect_return_code);
            }
            else
            {
                ESP_LOGE(TAG, "Loi MQTT/TLS");
            }
            break;
        default:
            break;
    }
}

void app_mqtt_start(const char *device_id)
{
    /* Bản tin LWT phải sống suốt vòng đời client → lưu trữ tĩnh (khai báo khối, Rule 8.9). */
    static char s_lwt[APP_STATUS_JSON_MAX];
    static size_t s_lwt_len = 0U;
    esp_mqtt_client_config_t cfg;

    if (strlen(APP_MQTT_URI) == 0U)
    {
        ESP_LOGW(TAG, "APP_MQTT_URI rong -> tat MQTT (chi dieu khien qua web).");
    }
    else if (!(build_topic(s_topic_state, sizeof(s_topic_state), device_id, "state") &&
               build_topic(s_topic_cmd, sizeof(s_topic_cmd), device_id, "cmd") &&
               build_topic(s_topic_ack, sizeof(s_topic_ack), device_id, "ack") &&
               (status_json_build_offline(device_id, s_lwt, sizeof(s_lwt), &s_lwt_len) == RC_OK)))
    {
        ESP_LOGE(TAG, "Topic qua dai – kiem tra APP_MQTT_TOPIC_PREFIX");
    }
    else
    {
        (void)memset(&cfg, 0, sizeof(cfg));
        cfg.broker.address.uri = APP_MQTT_URI;
        cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
        cfg.credentials.client_id = device_id;
        cfg.credentials.username = (strlen(APP_MQTT_USER) > 0U) ? APP_MQTT_USER : NULL;
        cfg.credentials.authentication.password = (strlen(APP_MQTT_PASS) > 0U) ? APP_MQTT_PASS : NULL;
        cfg.session.keepalive = MQTT_KEEPALIVE_S;
        cfg.session.last_will.topic = s_topic_state;
        cfg.session.last_will.msg = s_lwt;
        cfg.session.last_will.msg_len = (int)s_lwt_len;
        cfg.session.last_will.qos = 1;
        cfg.session.last_will.retain = 1;
        cfg.network.reconnect_timeout_ms = MQTT_RECONNECT_MS;

        s_client = esp_mqtt_client_init(&cfg);
        if (s_client == NULL)
        {
            ESP_LOGE(TAG, "esp_mqtt_client_init that bai");
        }
        else
        {
            ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_client,
                                                           (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID,
                                                           &on_mqtt_event, NULL));
            ESP_ERROR_CHECK(esp_mqtt_client_start(s_client));
            ESP_LOGI(TAG, "Dang noi %s (client_id=%s)", APP_MQTT_URI, device_id);
        }
    }
}

bool app_mqtt_is_connected(void)
{
    return s_connected;
}

void app_mqtt_publish_state(const char *json, size_t len)
{
    if (s_connected && (s_client != NULL) && (json != NULL))
    {
        (void)esp_mqtt_client_enqueue(s_client, s_topic_state, json, (int)len, 1, 1, true);
    }
}
