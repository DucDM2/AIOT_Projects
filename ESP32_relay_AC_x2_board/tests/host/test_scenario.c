/*
 * Kịch bản tích hợp: mô phỏng TOÀN BỘ thiết bị ở mức thanh ghi, dùng đúng các module
 * firmware dùng (gpio_drv, relay, button, led_pattern, cmd_parser, status_json) và đúng
 * cấu hình chân của bo ESP32_Relay_AC_X2 – chỉ thay FreeRTOS bằng vòng lặp 10 ms.
 */
#include "ut.h"
#include "tests.h"
#include "button.h"
#include "cmd_parser.h"
#include "esp32_regs.h"
#include "gpio_drv.h"
#include "led_pattern.h"
#include "reg_io_sim.h"
#include "relay.h"
#include "status_json.h"

#define PIN_R1    (16U)
#define PIN_R2    (17U)
#define PIN_LED   (23U)
#define PIN_BTN   (0U)
#define BIT(n)    (1UL << (n))

typedef struct
{
    button_t btn;
    uint32_t now;
    uint32_t seq;
    led_mode_t led_mode;
} device_t;

static device_t s_dev;

static void dev_boot(void)
{
    relay_config_t cfg;
    cfg.pin[0] = PIN_R1;
    cfg.pin[1] = PIN_R2;
    cfg.active_high = true;
    cfg.min_switch_ms = 300UL;

    s_dev.now = 0UL;
    s_dev.seq = 0UL;
    s_dev.led_mode = LED_MODE_BLINK_FAST;

    gpio_drv_init();
    UT_EQ_INT(relay_init(&cfg), RC_OK);
    UT_EQ_INT(gpio_drv_config_output(PIN_LED, GPIO_LEVEL_HIGH), RC_OK); /* LED active-low: tắt */
    UT_EQ_INT(gpio_drv_config_input(PIN_BTN), RC_OK);
    UT_EQ_INT(button_init(&s_dev.btn, 30UL, 1500UL, false, 0UL), RC_OK);
}

/* Giống app_ctrl_execute() nhưng không có mutex/MQTT. */
static rc_status_t dev_execute(const cmd_t *cmd)
{
    rc_status_t rc = RC_ERR_ARG;
    bool changed = false;
    if (cmd->kind == CMD_KIND_SET) { rc = relay_set(cmd->relay, cmd->on, s_dev.now, &changed); }
    else if (cmd->kind == CMD_KIND_TOGGLE) { rc = relay_toggle(cmd->relay, s_dev.now, &changed); }
    else if (cmd->kind == CMD_KIND_QUERY) { rc = RC_OK; }
    else { rc = RC_ERR_ARG; }
    if (changed) { s_dev.seq++; }
    return rc;
}

/* Giống io_task(): đọc nút, xử lý sự kiện, lái LED. */
static void dev_tick_10ms(void)
{
    gpio_level_t lvl = GPIO_LEVEL_HIGH;
    button_event_t e;
    cmd_t cmd;

    s_dev.now += 10UL;
    (void)gpio_drv_read_input(PIN_BTN, &lvl);
    e = button_update(&s_dev.btn, lvl == GPIO_LEVEL_LOW, s_dev.now);
    cmd.kind = CMD_KIND_TOGGLE;
    cmd.on = false;
    cmd.relay = (e == BUTTON_EVT_SHORT) ? 1U : ((e == BUTTON_EVT_LONG) ? 2U : 0U);
    if (cmd.relay != 0U) { (void)dev_execute(&cmd); }

    (void)gpio_drv_write(PIN_LED, led_pattern_is_on(s_dev.led_mode, s_dev.now) ? GPIO_LEVEL_LOW : GPIO_LEVEL_HIGH);
}

static void dev_run_ms(uint32_t ms)
{
    uint32_t t;
    for (t = 0UL; t < ms; t += 10UL) { dev_tick_10ms(); }
}

static rc_status_t dev_mqtt_message(const char *payload)
{
    cmd_t cmd;
    rc_status_t rc = cmd_parse(payload, strlen(payload), &cmd);
    if (rc == RC_OK) { rc = dev_execute(&cmd); }
    return rc;
}

static uint32_t relay_bits(void) { return sim_peek(ESP32_GPIO_OUT_REG) & (BIT(PIN_R1) | BIT(PIN_R2)); }

void scenario_full_device(bool verbose)
{
    char json[256];
    size_t len = 0U;
    status_info_t info;

    if (verbose) { printf("\n  == 1. Khoi dong: cau hinh GPIO bang thanh ghi ==\n"); sim_set_trace(true); }
    dev_boot();
    sim_set_trace(false);
    UT_EQ_U32(relay_bits(), 0UL);
    UT_EQ_U32(sim_peek(ESP32_GPIO_ENABLE_REG), BIT(PIN_R1) | BIT(PIN_R2) | BIT(PIN_LED));
    UT_EQ_U32(sim_peek(ESP32_GPIO_OUT_REG) & BIT(PIN_LED), BIT(PIN_LED));

    if (verbose) { printf("\n  == 2. Nhan ngan nut BOOT (IO0 = 0 trong 120 ms) ==\n"); }
    dev_run_ms(100UL);
    sim_set_external_level(PIN_BTN, false);
    dev_run_ms(120UL);
    sim_set_external_level(PIN_BTN, true);
    sim_log_clear();
    if (verbose) { sim_set_trace(true); }
    dev_run_ms(40UL); /* sự kiện SHORT phát sinh khi nhả + qua chống dội */
    sim_set_trace(false);
    UT_EQ_U32(relay_bits(), BIT(PIN_R1));

    if (verbose) { printf("\n  == 3. MQTT: {\"relay\":2,\"state\":\"on\"} ==\n"); sim_set_trace(true); }
    UT_EQ_INT(dev_mqtt_message("{\"relay\":2,\"state\":\"on\"}"), RC_OK);
    sim_set_trace(false);
    UT_EQ_U32(relay_bits(), BIT(PIN_R1) | BIT(PIN_R2));

    if (verbose) { printf("\n  == 4. Lenh dao relay 2 qua nhanh -> busy, thanh ghi khong doi ==\n"); }
    UT_EQ_INT(dev_mqtt_message("{\"relay\":2,\"state\":\"toggle\"}"), RC_ERR_BUSY);
    UT_EQ_U32(relay_bits(), BIT(PIN_R1) | BIT(PIN_R2));
    UT_EQ_INT(dev_mqtt_message("{\"relay\":2,\"state\":\"bad\"}"), RC_ERR_RANGE);
    UT_EQ_INT(dev_mqtt_message("garbage"), RC_ERR_SYNTAX);

    if (verbose) { printf("\n  == 5. Nhan giu nut 1.6 s -> dao relay 2 ==\n"); }
    dev_run_ms(500UL);
    sim_set_external_level(PIN_BTN, false);
    dev_run_ms(1600UL);
    sim_set_external_level(PIN_BTN, true);
    dev_run_ms(100UL);
    UT_EQ_U32(relay_bits(), BIT(PIN_R1));
    UT_EQ_INT(relay_self_check(), RC_OK);

    if (verbose) { printf("\n  == 6. Ban tin trang thai gui len EMQX ==\n"); }
    info.device_id = "relay-a1b2c3";
    info.fw_version = "0.1.0";
    info.relay_mask = relay_get_mask();
    info.seq = s_dev.seq;
    info.uptime_s = s_dev.now / 1000UL;
    info.rssi = -61;
    info.rssi_valid = true;
    info.mqtt_connected = true;
    info.reason = STATUS_REASON_BUTTON;
    UT_EQ_INT(status_json_build(&info, json, sizeof(json), &len), RC_OK);
    UT_EQ_STR(json,
        "{\"device\":\"relay-a1b2c3\",\"fw\":\"0.1.0\",\"online\":true,\"relay1\":true,"
        "\"relay2\":false,\"seq\":3,\"uptime_s\":2,\"rssi\":-61,\"mqtt\":true,\"reason\":\"button\"}");
    if (verbose) { printf("    %s\n", json); }

    if (verbose) { printf("\n  == 7. LED nhay nhanh 5 Hz trong 1 s ==\n"); }
    sim_log_clear();
    s_dev.now = 0UL; /* căn pha: các tick rơi vào t = 10, 20, ..., 990 ms */
    dev_run_ms(990UL);
    {
        /* LED active-low: bật = W1TC bit23. 5 chu kỳ → 5 lần bật thực sự (bỏ qua ghi lặp). */
        size_t i;
        int on_edges = 0;
        bool led_on = false;
        for (i = 0U; i < sim_log_count(); i++)
        {
            const sim_access_t *a = sim_log_get(i);
            if (a->is_write && ((a->value & BIT(PIN_LED)) != 0UL))
            {
                const bool now_on = (a->addr == ESP32_GPIO_OUT_W1TC_REG);
                if (now_on && !led_on) { on_edges++; }
                led_on = now_on;
            }
        }
        UT_EQ_INT(on_edges, 5);
    }

    UT_EQ_U32(sim_fault_count(), 0UL);
}

static void test_scenario_full_device(void)
{
    scenario_full_device(false);
}

void run_scenario_tests(void)
{
    UT_RUN(test_scenario_full_device);
}
