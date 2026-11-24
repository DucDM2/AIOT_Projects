/**
 * @file    app_http.c
 * @brief   Web server cục bộ – xem app_http.h.
 */
#include "app_http.h"
#include "app_config.h"
#include "app_ctrl.h"
#include "cmd_parser.h"
#include "status_json.h"

#include <string.h>
#include "esp_http_server.h"
#include "esp_log.h"

#define HTTP_AUTH_MAX       (96U)
#define HTTP_RECV_RETRIES   (3U)

static const char *const TAG = "http";

/* Trang web nhúng vào firmware (EMBED_TXTFILES) – ký hiệu do linker tạo.
 * MISRA DEVIATION D-03 (Rule 1.2): nhãn asm là phần mở rộng GCC, bắt buộc để lấy ký hiệu linker. */
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

/* So sánh thời gian hằng – không lộ độ dài khớp qua thời gian phản hồi. */
static bool const_time_equal(const char *a, const char *b)
{
    const size_t la = strlen(a);
    const size_t lb = strlen(b);
    uint8_t diff = (la == lb) ? 0U : 1U;
    size_t i;

    for (i = 0U; i < la; i++)
    {
        const char cb = (i < lb) ? b[i] : '\0';
        diff |= (uint8_t)((uint8_t)a[i] ^ (uint8_t)cb);
    }
    return diff == 0U;
}

static bool is_authorized(httpd_req_t *req)
{
    bool ok = true;

    if (strlen(APP_HTTP_TOKEN) > 0U)
    {
        char hdr[HTTP_AUTH_MAX];
        char expected[HTTP_AUTH_MAX];
        const size_t prefix_len = strlen("Bearer ");

        ok = false;
        if ((strlen(APP_HTTP_TOKEN) + prefix_len) < sizeof(expected))
        {
            (void)memcpy(expected, "Bearer ", prefix_len);
            (void)memcpy(&expected[prefix_len], APP_HTTP_TOKEN, strlen(APP_HTTP_TOKEN) + 1U);
            if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) == ESP_OK)
            {
                ok = const_time_equal(hdr, expected);
            }
        }
    }
    return ok;
}

static esp_err_t send_json(httpd_req_t *req, const char *status, const char *json, size_t len)
{
    (void)httpd_resp_set_status(req, status);
    (void)httpd_resp_set_type(req, "application/json");
    (void)httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, (ssize_t)len);
}

static esp_err_t send_ack(httpd_req_t *req, const char *status, rc_status_t result)
{
    char ack[64];
    size_t len = 0U;

    (void)status_json_build_ack(result, ack, sizeof(ack), &len);
    return send_json(req, status, ack, len);
}

static esp_err_t h_index(httpd_req_t *req)
{
    /* EMBED_TXTFILES thêm '\0' ở cuối → không gửi byte đó. */
    const size_t len = (size_t)(index_html_end - index_html_start) - 1U;

    (void)httpd_resp_set_type(req, "text/html; charset=utf-8");
    (void)httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, index_html_start, (ssize_t)len);
}

static esp_err_t h_status(httpd_req_t *req)
{
    char json[APP_STATUS_JSON_MAX];
    size_t len = 0U;
    esp_err_t err;

    if (app_ctrl_build_status(STATUS_REASON_QUERY, json, sizeof(json), &len) == RC_OK)
    {
        err = send_json(req, "200 OK", json, len);
    }
    else
    {
        err = send_ack(req, "500 Internal Server Error", RC_ERR_OVERFLOW);
    }
    return err;
}

/* Đọc trọn thân yêu cầu vào @p body (đã kiểm content_len ≤ cap). */
static bool recv_body(httpd_req_t *req, char *body, size_t *got)
{
    uint32_t retries = 0U;
    bool ok = true;

    *got = 0U;
    while (ok && (*got < req->content_len))
    {
        const int r = httpd_req_recv(req, &body[*got], req->content_len - *got);
        if (r > 0)
        {
            *got += (size_t)r;
        }
        else if ((r == HTTPD_SOCK_ERR_TIMEOUT) && (retries < HTTP_RECV_RETRIES))
        {
            retries++;
        }
        else
        {
            ok = false;
        }
    }
    return ok;
}

static esp_err_t h_relay(httpd_req_t *req)
{
    char body[CMD_MAX_INPUT_LEN];
    size_t got = 0U;
    cmd_t cmd;
    rc_status_t rc;
    esp_err_t err;

    if (!is_authorized(req))
    {
        err = send_ack(req, "401 Unauthorized", RC_ERR_ARG);
    }
    else if (req->content_len > sizeof(body))
    {
        err = send_ack(req, "413 Payload Too Large", RC_ERR_OVERFLOW);
    }
    else if (!recv_body(req, body, &got))
    {
        err = ESP_FAIL; /* lỗi socket → httpd đóng kết nối */
    }
    else
    {
        rc = cmd_parse(body, got, &cmd);
        if (rc == RC_OK)
        {
            rc = app_ctrl_execute(&cmd, STATUS_REASON_WEB);
        }

        if (rc == RC_OK)
        {
            err = h_status(req);
        }
        else if (rc == RC_ERR_BUSY)
        {
            err = send_ack(req, "409 Conflict", rc);
        }
        else
        {
            err = send_ack(req, "400 Bad Request", rc);
        }
    }
    return err;
}

void app_http_start(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    static const httpd_uri_t uri_index = {
        .uri = "/", .method = HTTP_GET, .handler = h_index, .user_ctx = NULL
    };
    static const httpd_uri_t uri_status = {
        .uri = "/api/status", .method = HTTP_GET, .handler = h_status, .user_ctx = NULL
    };
    static const httpd_uri_t uri_relay = {
        .uri = "/api/relay", .method = HTTP_POST, .handler = h_relay, .user_ctx = NULL
    };

    cfg.stack_size = 6144;
    cfg.lru_purge_enable = true;

    if (httpd_start(&server, &cfg) == ESP_OK)
    {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_index));
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_status));
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_relay));
        ESP_LOGI(TAG, "Web server san sang (cong %u)", (unsigned)cfg.server_port);
    }
    else
    {
        ESP_LOGE(TAG, "Khong khoi dong duoc web server");
    }
}
