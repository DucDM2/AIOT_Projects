/**
 * @file    app_config.h
 * @brief   Cấu hình ứng dụng: gom Kconfig (menuconfig) + app_secrets.h vào một chỗ.
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include "sdkconfig.h"
#include "app_secrets.h"

#define APP_FW_VERSION          "0.1.0"

#define APP_PIN_RELAY1          ((uint8_t)CONFIG_APP_PIN_RELAY1)
#define APP_PIN_RELAY2          ((uint8_t)CONFIG_APP_PIN_RELAY2)
#define APP_PIN_LED             ((uint8_t)CONFIG_APP_PIN_LED)
#define APP_PIN_BUTTON          ((uint8_t)CONFIG_APP_PIN_BUTTON)

#if defined(CONFIG_APP_RELAY_ACTIVE_HIGH)
#define APP_RELAY_ACTIVE_HIGH   (true)
#else
#define APP_RELAY_ACTIVE_HIGH   (false)
#endif

#if defined(CONFIG_APP_LED_ACTIVE_LOW)
#define APP_LED_ACTIVE_LOW      (true)
#else
#define APP_LED_ACTIVE_LOW      (false)
#endif

#define APP_RELAY_MIN_SWITCH_MS ((uint32_t)CONFIG_APP_RELAY_MIN_SWITCH_MS)
#define APP_BUTTON_DEBOUNCE_MS  (30UL)
#define APP_BUTTON_LONG_MS      ((uint32_t)CONFIG_APP_BUTTON_LONG_MS)
#define APP_HEARTBEAT_MS        ((uint32_t)CONFIG_APP_STATUS_HEARTBEAT_S * 1000UL)
#define APP_TOPIC_PREFIX        CONFIG_APP_MQTT_TOPIC_PREFIX

#define APP_DEVICE_ID_LEN       (16U)   /* "relay-a1b2c3" + '\0' */
#define APP_STATUS_JSON_MAX     (256U)
#define APP_TOPIC_MAX           (96U)

#endif /* APP_CONFIG_H */
