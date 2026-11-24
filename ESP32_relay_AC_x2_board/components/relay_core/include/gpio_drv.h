/**
 * @file    gpio_drv.h
 * @brief   Driver GPIO mức thanh ghi cho ESP32 (chỉ GPIO0..31).
 *
 * Chỉ cho phép các chân an toàn trên module WROOM-32E:
 *  - Cấm GPIO1/3 (UART0 – cổng nạp & log), GPIO6..11 (SPI flash),
 *    và các số không có pad (20, 24, 28..31).
 *  - GPIO32..39 nằm ở thanh ghi OUT1/IN1 – chưa hỗ trợ (bo này không cần).
 *
 * Lưu ý điện trở kéo: với các pad RTC (vd GPIO0) bit FUN_WPU của IO_MUX KHÔNG có tác dụng,
 * nên driver không bật kéo lên nội; bo ESP32_Relay_AC_X2 đã có điện trở kéo lên ngoài cho IO0
 * (bắt buộc vì IO0 là chân strapping).
 */
#ifndef GPIO_DRV_H
#define GPIO_DRV_H

#include "rc_types.h"

#define GPIO_DRV_PIN_COUNT   (32U)

typedef enum
{
    GPIO_LEVEL_LOW = 0,
    GPIO_LEVEL_HIGH = 1
} gpio_level_t;

/** Xoá bảng chân đã cấu hình (không chạm phần cứng). Gọi một lần khi khởi động. */
void gpio_drv_init(void);

/** true nếu @p pin dùng được làm GPIO trên module WROOM-32E. */
bool gpio_drv_pin_is_usable(uint8_t pin);

/**
 * Cấu hình chân ra push-pull, bật cả bộ đệm vào để đọc lại mức thật trên pad.
 * Mức @p initial được chốt vào OUT TRƯỚC khi bật lái ra → không có xung nhiễu khi khởi động.
 */
rc_status_t gpio_drv_config_output(uint8_t pin, gpio_level_t initial);

/** Cấu hình chân vào (tắt lái ra, bật bộ đệm vào). */
rc_status_t gpio_drv_config_input(uint8_t pin);

/** Ghi mức ra qua W1TS/W1TC (nguyên tử). Chân phải là chân ra. */
rc_status_t gpio_drv_write(uint8_t pin, gpio_level_t level);

/** Đọc mức đang chốt trong GPIO_OUT_REG. Chân phải là chân ra. */
rc_status_t gpio_drv_read_output(uint8_t pin, gpio_level_t *level);

/** Đọc mức thực trên pad từ GPIO_IN_REG. Chân phải đã cấu hình (vào hoặc ra). */
rc_status_t gpio_drv_read_input(uint8_t pin, gpio_level_t *level);

#endif /* GPIO_DRV_H */
