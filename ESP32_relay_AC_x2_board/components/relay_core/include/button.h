/**
 * @file    button.h
 * @brief   Chống dội phím + nhận nhấn ngắn / nhấn giữ (máy trạng thái thuần, không chạm phần cứng).
 *
 * Gọi button_update() định kỳ (vd 10 ms) với mức đã đọc từ chân.
 *  - Nhấn ngắn: sự kiện BUTTON_EVT_SHORT khi NHẢ phím (nếu chưa đạt thời gian giữ).
 *  - Nhấn giữ:  sự kiện BUTTON_EVT_LONG ngay khi giữ đủ long_ms (chỉ một lần mỗi lần nhấn).
 */
#ifndef BUTTON_H
#define BUTTON_H

#include "rc_types.h"

typedef enum
{
    BUTTON_EVT_NONE = 0,
    BUTTON_EVT_SHORT,
    BUTTON_EVT_LONG
} button_event_t;

typedef struct
{
    uint32_t debounce_ms;
    uint32_t long_ms;
    uint32_t raw_change_ms;
    uint32_t press_start_ms;
    bool     raw_pressed;
    bool     stable_pressed;
    bool     long_reported;
} button_t;

/**
 * @param initial_pressed  mức hiện tại lúc khởi tạo – nếu phím đang bị giữ lúc khởi động
 *                         thì lần nhấn đó bị bỏ qua (không sinh sự kiện).
 */
rc_status_t button_init(button_t *btn, uint32_t debounce_ms, uint32_t long_ms,
                        bool initial_pressed, uint32_t now_ms);

button_event_t button_update(button_t *btn, bool raw_pressed, uint32_t now_ms);

#endif /* BUTTON_H */
