/* Kiểm thử driver GPIO ở mức thanh ghi (với bộ mô phỏng reg_io_sim). */
#include "ut.h"
#include "tests.h"
#include "gpio_drv.h"
#include "esp32_regs.h"
#include "reg_io_sim.h"

#define IOMUX_GPIO0   (ESP32_IO_MUX_BASE + 0x44UL)
#define IOMUX_GPIO16  (ESP32_IO_MUX_BASE + 0x4CUL)
#define IOMUX_GPIO17  (ESP32_IO_MUX_BASE + 0x50UL)
#define IOMUX_GPIO23  (ESP32_IO_MUX_BASE + 0x8CUL)
#define PIN_REG(n)    (ESP32_GPIO_PIN0_REG + 4UL * (n))
#define OUT_SEL(n)    (ESP32_GPIO_FUNC0_OUT_SEL_CFG_REG + 4UL * (n))
#define BIT(n)        (1UL << (n))

static void test_pin_usability(void)
{
    static const uint8_t ok_pins[] = {0, 2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27};
    static const uint8_t bad_pins[] = {1, 3, 6, 7, 8, 9, 10, 11, 20, 24, 28, 29, 30, 31, 32, 39, 255};
    size_t i;
    for (i = 0; i < sizeof(ok_pins); i++) { UT_CHECK(gpio_drv_pin_is_usable(ok_pins[i])); }
    for (i = 0; i < sizeof(bad_pins); i++) { UT_CHECK(!gpio_drv_pin_is_usable(bad_pins[i])); }
}

static void test_config_output_registers(void)
{
    /* "Đầu độc" thanh ghi để chắc driver ghi đúng chứ không nhờ giá trị reset. */
    sim_poke(PIN_REG(16), 0xFFFFFFFFUL);
    sim_poke(OUT_SEL(16), 0x000000AAUL);
    sim_poke(IOMUX_GPIO16, 0x00000D80UL); /* FUN_DRV=3, FUN_WPU, FUN_WPD, MCU_SEL=0 */
    sim_poke(ESP32_GPIO_OUT_REG, BIT(16));

    UT_EQ_INT(gpio_drv_config_output(16, GPIO_LEVEL_LOW), RC_OK);

    UT_EQ_U32(sim_peek(PIN_REG(16)) & ESP32_GPIO_PIN_PAD_DRIVER_BIT, 0UL);          /* push-pull */
    UT_EQ_U32(sim_peek(PIN_REG(16)) | ESP32_GPIO_PIN_PAD_DRIVER_BIT, 0xFFFFFFFFUL); /* bit khác giữ nguyên */
    UT_EQ_U32(sim_peek(OUT_SEL(16)), 0x100UL);                                      /* SIG_GPIO_OUT_IDX */
    UT_EQ_U32((sim_peek(IOMUX_GPIO16) & ESP32_IO_MUX_MCU_SEL_MASK) >> 12, 2UL);    /* Function 3 = GPIO */
    UT_CHECK((sim_peek(IOMUX_GPIO16) & ESP32_IO_MUX_FUN_IE_BIT) != 0UL);
    UT_EQ_U32(sim_peek(IOMUX_GPIO16) & 0x0D80UL, 0x0D80UL);                         /* DRV/kéo giữ nguyên */
    UT_EQ_U32(sim_peek(ESP32_GPIO_ENABLE_REG), BIT(16));
    UT_EQ_U32(sim_peek(ESP32_GPIO_OUT_REG) & BIT(16), 0UL);
    UT_EQ_U32(sim_fault_count(), 0UL);
}

static void test_config_output_glitch_free_order(void)
{
    size_t idx_level;
    size_t idx_enable;

    UT_EQ_INT(gpio_drv_config_output(17, GPIO_LEVEL_HIGH), RC_OK);
    idx_level = sim_log_find_write(ESP32_GPIO_OUT_W1TS_REG, BIT(17));
    idx_enable = sim_log_find_write(ESP32_GPIO_ENABLE_W1TS_REG, BIT(17));
    UT_CHECK(idx_level != SIZE_MAX);
    UT_CHECK(idx_enable != SIZE_MAX);
    UT_CHECK(idx_level < idx_enable); /* chốt mức trước, bật lái ra sau */
    UT_EQ_U32(sim_peek(ESP32_GPIO_OUT_REG), BIT(17));
}

static void test_write_uses_atomic_set_clear(void)
{
    UT_EQ_INT(gpio_drv_config_output(16, GPIO_LEVEL_LOW), RC_OK);
    sim_log_clear();

    UT_EQ_INT(gpio_drv_write(16, GPIO_LEVEL_HIGH), RC_OK);
    UT_EQ_U32(sim_peek(ESP32_GPIO_OUT_REG), BIT(16));
    UT_EQ_INT(gpio_drv_write(16, GPIO_LEVEL_LOW), RC_OK);
    UT_EQ_U32(sim_peek(ESP32_GPIO_OUT_REG), 0UL);

    /* Không bao giờ đọc-sửa-ghi GPIO_OUT (tránh tranh chấp giữa các task/ngắt). */
    UT_EQ_INT(sim_log_count_writes(ESP32_GPIO_OUT_REG), 0);
    UT_EQ_INT(sim_log_count_writes(ESP32_GPIO_OUT_W1TS_REG), 1);
    UT_EQ_INT(sim_log_count_writes(ESP32_GPIO_OUT_W1TC_REG), 1);
}

static void test_other_bits_untouched(void)
{
    sim_poke(ESP32_GPIO_OUT_REG, 0xA5000005UL);
    sim_poke(ESP32_GPIO_ENABLE_REG, 0x50000001UL);
    UT_EQ_INT(gpio_drv_config_output(16, GPIO_LEVEL_LOW), RC_OK);
    UT_EQ_INT(gpio_drv_config_output(17, GPIO_LEVEL_LOW), RC_OK);
    UT_EQ_INT(gpio_drv_write(16, GPIO_LEVEL_HIGH), RC_OK);
    UT_EQ_INT(gpio_drv_write(17, GPIO_LEVEL_HIGH), RC_OK);
    UT_EQ_INT(gpio_drv_write(17, GPIO_LEVEL_LOW), RC_OK);
    UT_EQ_U32(sim_peek(ESP32_GPIO_OUT_REG), 0xA5000005UL | BIT(16));
    UT_EQ_U32(sim_peek(ESP32_GPIO_ENABLE_REG), 0x50000001UL | BIT(16) | BIT(17));
}

static void test_invalid_args_and_state(void)
{
    gpio_level_t lvl = GPIO_LEVEL_LOW;

    UT_EQ_INT(gpio_drv_config_output(6, GPIO_LEVEL_LOW), RC_ERR_ARG);   /* flash */
    UT_EQ_INT(gpio_drv_config_output(1, GPIO_LEVEL_LOW), RC_ERR_ARG);   /* UART0 TX */
    UT_EQ_INT(gpio_drv_config_output(40, GPIO_LEVEL_LOW), RC_ERR_ARG);
    UT_EQ_INT(gpio_drv_config_output(16, (gpio_level_t)2), RC_ERR_ARG);
    UT_EQ_INT(gpio_drv_config_input(9), RC_ERR_ARG);
    UT_EQ_INT(gpio_drv_write(16, GPIO_LEVEL_HIGH), RC_ERR_STATE);       /* chưa cấu hình */
    UT_EQ_INT(gpio_drv_read_output(16, &lvl), RC_ERR_STATE);
    UT_EQ_INT(gpio_drv_read_input(16, &lvl), RC_ERR_STATE);
    UT_EQ_INT(gpio_drv_read_input(16, NULL), RC_ERR_ARG);
    /* Không có lần ghi thanh ghi nào khi tham số sai. */
    UT_EQ_INT(sim_log_count(), 0);
    UT_EQ_U32(sim_fault_count(), 0UL);
}

static void test_config_input_button(void)
{
    gpio_level_t lvl = GPIO_LEVEL_LOW;

    sim_poke(ESP32_GPIO_ENABLE_REG, BIT(0)); /* giả sử ai đó đã bật lái ra IO0 */
    sim_poke(IOMUX_GPIO0, 0x00000800UL);
    UT_EQ_INT(gpio_drv_config_input(0), RC_OK);
    UT_EQ_U32(sim_peek(ESP32_GPIO_ENABLE_REG) & BIT(0), 0UL);
    UT_EQ_U32((sim_peek(IOMUX_GPIO0) & ESP32_IO_MUX_MCU_SEL_MASK) >> 12, 2UL);
    UT_CHECK((sim_peek(IOMUX_GPIO0) & ESP32_IO_MUX_FUN_IE_BIT) != 0UL);

    sim_set_external_level(0, true);
    UT_EQ_INT(gpio_drv_read_input(0, &lvl), RC_OK);
    UT_EQ_INT(lvl, GPIO_LEVEL_HIGH);
    sim_set_external_level(0, false); /* nhấn nút */
    UT_EQ_INT(gpio_drv_read_input(0, &lvl), RC_OK);
    UT_EQ_INT(lvl, GPIO_LEVEL_LOW);

    /* Chân vào không được ghi. */
    UT_EQ_INT(gpio_drv_write(0, GPIO_LEVEL_HIGH), RC_ERR_STATE);
}

static void test_output_pad_readback_detects_short(void)
{
    gpio_level_t lvl = GPIO_LEVEL_LOW;

    UT_EQ_INT(gpio_drv_config_output(16, GPIO_LEVEL_LOW), RC_OK);
    UT_EQ_INT(gpio_drv_write(16, GPIO_LEVEL_HIGH), RC_OK);
    UT_EQ_INT(gpio_drv_read_input(16, &lvl), RC_OK);
    UT_EQ_INT(lvl, GPIO_LEVEL_HIGH); /* pad khớp OUT */

    sim_force_pad(16, true, false);  /* chập xuống GND */
    UT_EQ_INT(gpio_drv_read_output(16, &lvl), RC_OK);
    UT_EQ_INT(lvl, GPIO_LEVEL_HIGH); /* OUT vẫn 1 */
    UT_EQ_INT(gpio_drv_read_input(16, &lvl), RC_OK);
    UT_EQ_INT(lvl, GPIO_LEVEL_LOW);  /* nhưng pad = 0 → phát hiện được */
}

static void test_reconfigure_output_to_input(void)
{
    UT_EQ_INT(gpio_drv_config_output(23, GPIO_LEVEL_HIGH), RC_OK);
    UT_EQ_U32(sim_peek(ESP32_GPIO_ENABLE_REG) & BIT(23), BIT(23));
    UT_EQ_INT(gpio_drv_config_input(23), RC_OK);
    UT_EQ_U32(sim_peek(ESP32_GPIO_ENABLE_REG) & BIT(23), 0UL);
    UT_EQ_INT(gpio_drv_write(23, GPIO_LEVEL_LOW), RC_ERR_STATE);
    UT_EQ_U32((sim_peek(IOMUX_GPIO23) & ESP32_IO_MUX_MCU_SEL_MASK) >> 12, 2UL);
}

void run_gpio_drv_tests(void)
{
    UT_RUN(test_pin_usability);
    UT_RUN(test_config_output_registers);
    UT_RUN(test_config_output_glitch_free_order);
    UT_RUN(test_write_uses_atomic_set_clear);
    UT_RUN(test_other_bits_untouched);
    UT_RUN(test_invalid_args_and_state);
    UT_RUN(test_config_input_button);
    UT_RUN(test_output_pad_readback_detects_short);
    UT_RUN(test_reconfigure_output_to_input);
}
