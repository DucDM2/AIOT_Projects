/* Kiểm thử module relay trên bộ mô phỏng thanh ghi. */
#include "ut.h"
#include "tests.h"
#include "relay.h"
#include "gpio_drv.h"
#include "esp32_regs.h"
#include "reg_io_sim.h"

#define BIT(n)  (1UL << (n))
#define MIN_SW  (300UL)

static relay_config_t board_cfg(bool active_high)
{
    relay_config_t c;
    c.pin[0] = 16U;
    c.pin[1] = 17U;
    c.active_high = active_high;
    c.min_switch_ms = MIN_SW;
    return c;
}

static uint32_t out_reg(void) { return sim_peek(ESP32_GPIO_OUT_REG) & (BIT(16) | BIT(17)); }

static void test_init_all_off_active_high(void)
{
    const relay_config_t c = board_cfg(true);
    sim_poke(ESP32_GPIO_OUT_REG, BIT(16) | BIT(17)); /* rác trước khi init */
    UT_EQ_INT(relay_init(&c), RC_OK);
    UT_EQ_U32(out_reg(), 0UL);
    UT_EQ_U32(sim_peek(ESP32_GPIO_ENABLE_REG), BIT(16) | BIT(17));
    UT_EQ_INT(relay_get_mask(), 0);
    UT_EQ_INT(relay_self_check(), RC_OK);
}

static void test_init_all_off_active_low(void)
{
    const relay_config_t c = board_cfg(false);
    UT_EQ_INT(relay_init(&c), RC_OK);
    UT_EQ_U32(out_reg(), BIT(16) | BIT(17)); /* TẮT = mức cao */
    /* Mức cao phải được chốt TRƯỚC khi bật lái ra, nếu không relay sẽ nháy hút lúc khởi động. */
    UT_CHECK(sim_log_find_write(ESP32_GPIO_OUT_W1TS_REG, BIT(16)) <
             sim_log_find_write(ESP32_GPIO_ENABLE_W1TS_REG, BIT(16)));
    UT_EQ_INT(relay_get_mask(), 0);
}

static void test_init_rejects_bad_config(void)
{
    relay_config_t c = board_cfg(true);
    bool changed = true;

    UT_EQ_INT(relay_init(NULL), RC_ERR_ARG);
    c.pin[1] = 16U;
    UT_EQ_INT(relay_init(&c), RC_ERR_ARG); /* trùng chân */
    c.pin[1] = 6U;
    UT_EQ_INT(relay_init(&c), RC_ERR_ARG); /* chân flash */
    UT_EQ_INT(relay_set(1, true, 0, &changed), RC_ERR_STATE);
    UT_CHECK(!changed);
    UT_EQ_INT(sim_log_count(), 0); /* không chạm phần cứng */
}

static void test_set_and_get_register(void)
{
    const relay_config_t c = board_cfg(true);
    bool changed = false;
    bool on = false;

    UT_EQ_INT(relay_init(&c), RC_OK);
    UT_EQ_INT(relay_set(1, true, 1000, &changed), RC_OK);
    UT_CHECK(changed);
    UT_EQ_U32(out_reg(), BIT(16));
    UT_EQ_INT(relay_get(1, &on), RC_OK);
    UT_CHECK(on);
    UT_EQ_INT(relay_get_mask(), 0x01);

    UT_EQ_INT(relay_set(2, true, 1000, &changed), RC_OK);
    UT_EQ_U32(out_reg(), BIT(16) | BIT(17));
    UT_EQ_INT(relay_get_mask(), 0x03);

    /* Đặt lại cùng trạng thái: không đổi, không tính lần đóng/cắt. */
    UT_EQ_INT(relay_set(1, true, 1001, &changed), RC_OK);
    UT_CHECK(!changed);
    UT_EQ_U32(relay_get_switch_count(1), 1UL);
}

static void test_set_active_low_inverts(void)
{
    const relay_config_t c = board_cfg(false);
    UT_EQ_INT(relay_init(&c), RC_OK);
    UT_EQ_INT(relay_set(1, true, 0, NULL), RC_OK);
    UT_EQ_U32(out_reg(), BIT(17)); /* relay1 bật = GPIO16 mức thấp */
    UT_EQ_INT(relay_get_mask(), 0x01);
    UT_EQ_INT(relay_self_check(), RC_OK);
}

static void test_min_switch_interval(void)
{
    const relay_config_t c = board_cfg(true);
    bool changed = false;

    UT_EQ_INT(relay_init(&c), RC_OK);
    UT_EQ_INT(relay_set(1, true, 0, &changed), RC_OK); /* lần đầu không bao giờ bận */
    UT_EQ_INT(relay_set(1, false, 100, &changed), RC_ERR_BUSY);
    UT_CHECK(!changed);
    UT_EQ_U32(out_reg(), BIT(16)); /* thanh ghi không đổi */
    UT_EQ_INT(relay_set(1, false, 299, &changed), RC_ERR_BUSY);
    UT_EQ_INT(relay_set(2, true, 100, &changed), RC_OK); /* relay khác độc lập */
    UT_EQ_INT(relay_set(1, false, 300, &changed), RC_OK);
    UT_CHECK(changed);
    UT_EQ_U32(out_reg(), BIT(17));
}

static void test_min_switch_interval_timer_wrap(void)
{
    const relay_config_t c = board_cfg(true);
    UT_EQ_INT(relay_init(&c), RC_OK);
    UT_EQ_INT(relay_set(1, true, 0xFFFFFF00UL, NULL), RC_OK);
    UT_EQ_INT(relay_set(1, false, 0x00000010UL, NULL), RC_ERR_BUSY); /* 0x110 = 272 ms */
    UT_EQ_INT(relay_set(1, false, 0x00000064UL, NULL), RC_OK);       /* 0x164 = 356 ms */
}

static void test_toggle(void)
{
    const relay_config_t c = board_cfg(true);
    bool changed = false;

    UT_EQ_INT(relay_init(&c), RC_OK);
    UT_EQ_INT(relay_toggle(2, 0, &changed), RC_OK);
    UT_CHECK(changed);
    UT_EQ_U32(out_reg(), BIT(17));
    UT_EQ_INT(relay_toggle(2, 500, &changed), RC_OK);
    UT_EQ_U32(out_reg(), 0UL);
    UT_EQ_U32(relay_get_switch_count(2), 2UL);
    UT_EQ_INT(relay_toggle(3, 1000, &changed), RC_ERR_ARG);
    UT_CHECK(!changed);
}

static void test_channel_range(void)
{
    const relay_config_t c = board_cfg(true);
    bool on = false;
    UT_EQ_INT(relay_init(&c), RC_OK);
    UT_EQ_INT(relay_set(0, true, 0, NULL), RC_ERR_ARG);
    UT_EQ_INT(relay_set(3, true, 0, NULL), RC_ERR_ARG);
    UT_EQ_INT(relay_get(0, &on), RC_ERR_ARG);
    UT_EQ_INT(relay_get(1, NULL), RC_ERR_ARG);
    UT_EQ_U32(relay_get_switch_count(9), 0UL);
}

static void test_self_check_detects_register_corruption(void)
{
    const relay_config_t c = board_cfg(true);
    bool on = false;

    UT_EQ_INT(relay_init(&c), RC_OK);
    UT_EQ_INT(relay_set(1, true, 0, NULL), RC_OK);
    UT_EQ_INT(relay_self_check(), RC_OK);

    /* Mô phỏng code khác (hoặc nhiễu) xoá bit GPIO16 trong GPIO_OUT. */
    sim_poke(ESP32_GPIO_OUT_REG, 0UL);
    UT_EQ_INT(relay_get(1, &on), RC_OK);
    UT_CHECK(!on);                         /* relay_get đọc thanh ghi thật */
    UT_EQ_INT(relay_self_check(), RC_ERR_STATE);
}

void run_relay_tests(void)
{
    UT_RUN(test_init_all_off_active_high);
    UT_RUN(test_init_all_off_active_low);
    UT_RUN(test_init_rejects_bad_config);
    UT_RUN(test_set_and_get_register);
    UT_RUN(test_set_active_low_inverts);
    UT_RUN(test_min_switch_interval);
    UT_RUN(test_min_switch_interval_timer_wrap);
    UT_RUN(test_toggle);
    UT_RUN(test_channel_range);
    UT_RUN(test_self_check_detects_register_corruption);
}
