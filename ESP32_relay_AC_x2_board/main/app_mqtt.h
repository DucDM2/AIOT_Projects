/**
 * @file    app_mqtt.h
 * @brief   MQTTS tới EMQX Cloud.
 *
 * Topic (prefix mặc định "relay2", đổi trong menuconfig):
 *   <prefix>/<device_id>/state  ← thiết bị gửi trạng thái JSON (retain, QoS1); LWT {"online":false}
 *   <prefix>/<device_id>/cmd    → thiết bị nhận lệnh JSON (QoS1), xem cmd_parser.h
 *   <prefix>/<device_id>/ack    ← phản hồi từng lệnh {"ok":..,"error":..} (QoS0)
 */
#ifndef APP_MQTT_H
#define APP_MQTT_H

#include <stdbool.h>
#include <stddef.h>

/** Khởi động client (không làm gì nếu APP_MQTT_URI rỗng). Tự nối lại khi mất mạng. */
void app_mqtt_start(const char *device_id);

bool app_mqtt_is_connected(void);

/** Gửi trạng thái (retain, QoS1) – không chặn (đưa vào hàng đợi của esp-mqtt). */
void app_mqtt_publish_state(const char *json, size_t len);

#endif /* APP_MQTT_H */
