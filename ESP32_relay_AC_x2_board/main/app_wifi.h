/**
 * @file    app_wifi.h
 * @brief   Wi-Fi: luôn phát AP "ESP32-Relay-xxxxxx" (192.168.4.1) + nối Wi-Fi nhà nếu đã khai.
 */
#ifndef APP_WIFI_H
#define APP_WIFI_H

#include <stdbool.h>
#include <stdint.h>

void app_wifi_start(const char *device_id);

/** true khi đã nối Wi-Fi nhà và có IP. */
bool app_wifi_sta_connected(void);

/** Đọc RSSI của Wi-Fi nhà; false nếu chưa nối. */
bool app_wifi_get_rssi(int32_t *rssi);

#endif /* APP_WIFI_H */
