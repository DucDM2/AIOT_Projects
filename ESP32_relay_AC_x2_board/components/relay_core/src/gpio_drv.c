/**
 * @file    gpio_drv.c
 * @brief   Driver GPIO mức thanh ghi cho ESP32 – xem gpio_drv.h.
 */
#include "gpio_drv.h"
#include "esp32_regs.h"
#include "reg_io.h"

/* Offset thanh ghi IO_MUX theo số GPIO; 0 = chân không được phép dùng. */
static const uint16_t k_iomux_offset[GPIO_DRV_PIN_COUNT] =
{
    0x044U, /* GPIO0  (BOOT button)   */
    0x000U, /* GPIO1  U0TXD – cấm     */
    0x040U, /* GPIO2                  */
    0x000U, /* GPIO3  U0RXD – cấm     */
    0x048U, /* GPIO4                  */
    0x06CU, /* GPIO5                  */
    0x000U, /* GPIO6  flash – cấm     */
    0x000U, /* GPIO7  flash – cấm     */
    0x000U, /* GPIO8  flash – cấm     */
    0x000U, /* GPIO9  flash – cấm     */
    0x000U, /* GPIO10 flash – cấm     */
    0x000U, /* GPIO11 flash – cấm     */
    0x034U, /* GPIO12 (MTDI)          */
    0x038U, /* GPIO13 (MTCK)          */
    0x030U, /* GPIO14 (MTMS)          */
    0x03CU, /* GPIO15 (MTDO)          */
    0x04CU, /* GPIO16 Relay 1         */
    0x050U, /* GPIO17 Relay 2         */
    0x070U, /* GPIO18                 */
    0x074U, /* GPIO19                 */
    0x000U, /* GPIO20 không có pad    */
    0x07CU, /* GPIO21                 */
    0x080U, /* GPIO22                 */
    0x08CU, /* GPIO23 LED             */
    0x000U, /* GPIO24 không có pad    */
    0x024U, /* GPIO25                 */
    0x028U, /* GPIO26                 */
    0x02CU, /* GPIO27                 */
    0x000U, /* GPIO28 không có pad    */
    0x000U, /* GPIO29 không có pad    */
    0x000U, /* GPIO30 không có pad    */
    0x000U  /* GPIO31 không có pad    */
};

static uint32_t s_output_mask = 0UL;
static uint32_t s_input_mask = 0UL;

static uint32_t pin_bit(uint8_t pin)
{
    /* Gọi sau khi đã kiểm pin < 32 → phép dịch luôn hợp lệ (Rule 12.2). */
    return (uint32_t)1UL << (uint32_t)pin;
}

static uint32_t iomux_addr(uint8_t pin)
{
    return ESP32_IO_MUX_BASE + (uint32_t)k_iomux_offset[pin];
}

static uint32_t pin_reg_addr(uint8_t pin)
{
    return ESP32_GPIO_PIN0_REG + ((uint32_t)pin * ESP32_GPIO_REG_STRIDE);
}

static uint32_t func_out_sel_addr(uint8_t pin)
{
    return ESP32_GPIO_FUNC0_OUT_SEL_CFG_REG + ((uint32_t)pin * ESP32_GPIO_REG_STRIDE);
}

static void latch_level(uint32_t bit, gpio_level_t level)
{
    if (level == GPIO_LEVEL_HIGH)
    {
        reg_io_write(ESP32_GPIO_OUT_W1TS_REG, bit);
    }
    else
    {
        reg_io_write(ESP32_GPIO_OUT_W1TC_REG, bit);
    }
}

static bool level_is_valid(gpio_level_t level)
{
    return (level == GPIO_LEVEL_LOW) || (level == GPIO_LEVEL_HIGH);
}

static gpio_level_t bit_to_level(uint32_t reg_value, uint32_t bit)
{
    gpio_level_t level = GPIO_LEVEL_LOW;
    if ((reg_value & bit) != 0UL)
    {
        level = GPIO_LEVEL_HIGH;
    }
    return level;
}

/* Đặt trường MCU_SEL = GPIO và bit FUN_IE; giữ nguyên sức lái & bit kéo. */
static void iomux_select_gpio(uint8_t pin)
{
    const uint32_t addr = iomux_addr(pin);
    uint32_t v = reg_io_read(addr);
    v &= ~ESP32_IO_MUX_MCU_SEL_MASK;
    v |= (ESP32_IO_MUX_FUNC_GPIO << ESP32_IO_MUX_MCU_SEL_SHIFT);
    v |= ESP32_IO_MUX_FUN_IE_BIT;
    reg_io_write(addr, v);
}

void gpio_drv_init(void)
{
    s_output_mask = 0UL;
    s_input_mask = 0UL;
}

bool gpio_drv_pin_is_usable(uint8_t pin)
{
    bool usable = false;
    if (pin < GPIO_DRV_PIN_COUNT)
    {
        usable = (k_iomux_offset[pin] != 0U);
    }
    return usable;
}

rc_status_t gpio_drv_config_output(uint8_t pin, gpio_level_t initial)
{
    rc_status_t rc = RC_ERR_ARG;

    if (gpio_drv_pin_is_usable(pin) && level_is_valid(initial))
    {
        const uint32_t bit = pin_bit(pin);
        uint32_t v;

        /* 1. Push-pull (xoá PAD_DRIVER). */
        v = reg_io_read(pin_reg_addr(pin));
        v &= ~ESP32_GPIO_PIN_PAD_DRIVER_BIT;
        reg_io_write(pin_reg_addr(pin), v);

        /* 2. Ma trận GPIO: nối tín hiệu "GPIO đơn giản", OE lấy từ GPIO_ENABLE_REG. */
        reg_io_write(func_out_sel_addr(pin), ESP32_GPIO_SIG_GPIO_OUT_IDX);

        /* 3. Chốt mức ban đầu TRƯỚC khi bật lái ra – tránh relay nhảy lúc khởi động. */
        latch_level(bit, initial);

        /* 4. IO_MUX chọn chức năng GPIO, bật bộ đệm vào để đọc lại pad. */
        iomux_select_gpio(pin);

        /* 5. Bật lái ra. */
        reg_io_write(ESP32_GPIO_ENABLE_W1TS_REG, bit);

        s_output_mask |= bit;
        s_input_mask &= ~bit;
        rc = RC_OK;
    }
    return rc;
}

rc_status_t gpio_drv_config_input(uint8_t pin)
{
    rc_status_t rc = RC_ERR_ARG;

    if (gpio_drv_pin_is_usable(pin))
    {
        const uint32_t bit = pin_bit(pin);

        /* Tắt lái ra trước, rồi mới nối pad vào bộ đệm vào. */
        reg_io_write(ESP32_GPIO_ENABLE_W1TC_REG, bit);
        iomux_select_gpio(pin);

        s_input_mask |= bit;
        s_output_mask &= ~bit;
        rc = RC_OK;
    }
    return rc;
}

rc_status_t gpio_drv_write(uint8_t pin, gpio_level_t level)
{
    rc_status_t rc = RC_ERR_ARG;

    if (gpio_drv_pin_is_usable(pin) && level_is_valid(level))
    {
        const uint32_t bit = pin_bit(pin);
        if ((s_output_mask & bit) == 0UL)
        {
            rc = RC_ERR_STATE;
        }
        else
        {
            latch_level(bit, level);
            rc = RC_OK;
        }
    }
    return rc;
}

rc_status_t gpio_drv_read_output(uint8_t pin, gpio_level_t *level)
{
    rc_status_t rc = RC_ERR_ARG;

    if (gpio_drv_pin_is_usable(pin) && (level != NULL))
    {
        const uint32_t bit = pin_bit(pin);
        if ((s_output_mask & bit) == 0UL)
        {
            rc = RC_ERR_STATE;
        }
        else
        {
            *level = bit_to_level(reg_io_read(ESP32_GPIO_OUT_REG), bit);
            rc = RC_OK;
        }
    }
    return rc;
}

rc_status_t gpio_drv_read_input(uint8_t pin, gpio_level_t *level)
{
    rc_status_t rc = RC_ERR_ARG;

    if (gpio_drv_pin_is_usable(pin) && (level != NULL))
    {
        const uint32_t bit = pin_bit(pin);
        if (((s_output_mask | s_input_mask) & bit) == 0UL)
        {
            rc = RC_ERR_STATE;
        }
        else
        {
            *level = bit_to_level(reg_io_read(ESP32_GPIO_IN_REG), bit);
            rc = RC_OK;
        }
    }
    return rc;
}
