/**
 * @file    led_pattern.h
 * @brief   Kiểu nháy LED trạng thái (hàm thuần theo thời gian).
 *
 *  LED_MODE_BLINK_FAST : 5 Hz  – chưa có Wi-Fi (chỉ AP cấu hình)
 *  LED_MODE_BLINK_SLOW : 1 Hz  – có Wi-Fi, chưa nối được MQTT
 *  LED_MODE_ON         : sáng  – đã nối MQTT
 */
#ifndef LED_PATTERN_H
#define LED_PATTERN_H

#include "rc_types.h"

typedef enum
{
    LED_MODE_OFF = 0,
    LED_MODE_ON,
    LED_MODE_BLINK_FAST,
    LED_MODE_BLINK_SLOW
} led_mode_t;

#define LED_FAST_PERIOD_MS   (200UL)
#define LED_SLOW_PERIOD_MS   (1000UL)

/** true nếu LED cần sáng tại thời điểm @p now_ms. */
bool led_pattern_is_on(led_mode_t mode, uint32_t now_ms);

#endif /* LED_PATTERN_H */
