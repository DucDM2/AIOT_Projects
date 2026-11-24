/**
 * @file    main.c
 * @brief   ESP32_Relay_AC_X2 – firmware thử nghiệm: điều khiển 2 relay qua Wi-Fi (web + MQTTS/EMQX).
 *
 * Thứ tự khởi động:
 *   1. app_ctrl_init  – cấu hình GPIO bằng thanh ghi, relay TẮT ngay (an toàn tải 220 V)
 *   2. NVS            – Wi-Fi cần NVS để lưu hiệu chuẩn RF
 *   3. Wi-Fi          – AP riêng + Wi-Fi nhà
 *   4. Web server     – http://192.168.4.1 hoặc IP trong mạng nhà
 *   5. MQTTS          – EMQX Cloud (nếu đã khai APP_MQTT_URI)
 *   6. Task IO        – nút nhấn, LED, heartbeat, tự kiểm thanh ghi
 */
#include "app_config.h"
#include "app_ctrl.h"
#include "app_http.h"
#include "app_io.h"
#include "app_mqtt.h"
#include "app_wifi.h"

#include "esp_log.h"
#include "nvs_flash.h"

/* Điểm vào do ESP-IDF gọi – khai báo nguyên mẫu (Rule 8.4). */
void app_main(void);

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if ((err == ESP_ERR_NVS_NO_FREE_PAGES) || (err == ESP_ERR_NVS_NEW_VERSION_FOUND))
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    static const char *const TAG = "main";

    ESP_LOGI(TAG, "ESP32_Relay_AC_X2 firmware v%s", APP_FW_VERSION);

    app_ctrl_init();
    init_nvs();
    app_wifi_start(app_ctrl_device_id());
    app_http_start();
    app_mqtt_start(app_ctrl_device_id());
    app_io_start();

    ESP_LOGI(TAG, "San sang. Nut BOOT: nhan ngan = Relay 1, giu %ums = Relay 2",
             (unsigned)APP_BUTTON_LONG_MS);
}
