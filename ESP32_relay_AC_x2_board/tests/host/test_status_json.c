/* Kiểm thử bản tin JSON trạng thái + bộ đệm chuỗi. */
#include "ut.h"
#include "tests.h"
#include "status_json.h"
#include "str_buf.h"

static status_info_t sample(void)
{
    status_info_t s;
    s.device_id = "relay-a1b2c3";
    s.fw_version = "0.1.0";
    s.relay_mask = 0x02U;
    s.seq = 5UL;
    s.uptime_s = 123UL;
    s.rssi = -55;
    s.rssi_valid = true;
    s.mqtt_connected = true;
    s.reason = STATUS_REASON_MQTT;
    return s;
}

static void test_status_exact_output(void)
{
    char out[256];
    size_t len = 0U;
    status_info_t s = sample();
    const char *expected =
        "{\"device\":\"relay-a1b2c3\",\"fw\":\"0.1.0\",\"online\":true,\"relay1\":false,"
        "\"relay2\":true,\"seq\":5,\"uptime_s\":123,\"rssi\":-55,\"mqtt\":true,\"reason\":\"mqtt\"}";

    UT_EQ_INT(status_json_build(&s, out, sizeof(out), &len), RC_OK);
    UT_EQ_STR(out, expected);
    UT_EQ_INT(len, strlen(expected));

    s.rssi_valid = false;
    s.relay_mask = 0x03U;
    s.mqtt_connected = false;
    s.reason = STATUS_REASON_BOOT;
    UT_EQ_INT(status_json_build(&s, out, sizeof(out), &len), RC_OK);
    UT_EQ_STR(out,
        "{\"device\":\"relay-a1b2c3\",\"fw\":\"0.1.0\",\"online\":true,\"relay1\":true,"
        "\"relay2\":true,\"seq\":5,\"uptime_s\":123,\"rssi\":null,\"mqtt\":false,\"reason\":\"boot\"}");
}

static void test_status_overflow_is_safe(void)
{
    char out[40];
    size_t len = 0U;
    const status_info_t s = sample();

    memset(out, 'Z', sizeof(out));
    UT_EQ_INT(status_json_build(&s, out, 32U, &len), RC_ERR_OVERFLOW);
    UT_EQ_INT(len, 31);                     /* đầy bộ đệm, chừa '\0' */
    UT_EQ_INT(out[31], '\0');
    UT_EQ_INT(out[32], 'Z');                /* không ghi quá giới hạn */
    UT_EQ_INT(status_json_build(&s, out, 0U, &len), RC_ERR_ARG);
    UT_EQ_INT(status_json_build(NULL, out, sizeof(out), &len), RC_ERR_ARG);
}

static void test_offline_and_ack(void)
{
    char out[96];
    size_t len = 0U;
    UT_EQ_INT(status_json_build_offline("relay-a1b2c3", out, sizeof(out), &len), RC_OK);
    UT_EQ_STR(out, "{\"device\":\"relay-a1b2c3\",\"online\":false}");
    UT_EQ_INT(status_json_build_ack(RC_OK, out, sizeof(out), &len), RC_OK);
    UT_EQ_STR(out, "{\"ok\":true,\"error\":null}");
    UT_EQ_INT(status_json_build_ack(RC_ERR_BUSY, out, sizeof(out), &len), RC_OK);
    UT_EQ_STR(out, "{\"ok\":false,\"error\":\"busy\"}");
}

static void test_json_escaping(void)
{
    char out[64];
    str_buf_t sb;
    UT_EQ_INT(sb_init(&sb, out, sizeof(out)), RC_OK);
    sb_append_json_str(&sb, "a\"b\\c\n\x01");
    UT_EQ_STR(out, "\"a\\\"b\\\\c\\u000a\\u0001\"");
}

static void test_numbers(void)
{
    char out[64];
    str_buf_t sb;
    UT_EQ_INT(sb_init(&sb, out, sizeof(out)), RC_OK);
    sb_append_u32(&sb, 0UL);
    sb_append_char(&sb, ' ');
    sb_append_u32(&sb, 4294967295UL);
    sb_append_char(&sb, ' ');
    sb_append_i32(&sb, -2147483647 - 1);
    sb_append_char(&sb, ' ');
    sb_append_i32(&sb, 2147483647);
    sb_append_char(&sb, ' ');
    sb_append_hex8(&sb, 0xA1U);
    UT_EQ_STR(out, "0 4294967295 -2147483648 2147483647 a1");
}

void run_status_json_tests(void)
{
    UT_RUN(test_status_exact_output);
    UT_RUN(test_status_overflow_is_safe);
    UT_RUN(test_offline_and_ack);
    UT_RUN(test_json_escaping);
    UT_RUN(test_numbers);
}
