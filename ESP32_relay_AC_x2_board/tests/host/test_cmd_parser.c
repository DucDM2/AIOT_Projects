/* Kiểm thử bộ phân tích lệnh JSON (dùng chung MQTT + HTTP). */
#include "ut.h"
#include "tests.h"
#include "cmd_parser.h"

static rc_status_t parse(const char *s, cmd_t *c)
{
    return cmd_parse(s, strlen(s), c);
}

static void test_valid_commands(void)
{
    cmd_t c;

    UT_EQ_INT(parse("{\"relay\":1,\"state\":\"on\"}", &c), RC_OK);
    UT_EQ_INT(c.kind, CMD_KIND_SET); UT_EQ_INT(c.relay, 1); UT_CHECK(c.on);

    UT_EQ_INT(parse("{\"relay\":2,\"state\":\"off\"}", &c), RC_OK);
    UT_EQ_INT(c.kind, CMD_KIND_SET); UT_EQ_INT(c.relay, 2); UT_CHECK(!c.on);

    UT_EQ_INT(parse(" \r\n{ \"state\" : \"toggle\" ,\t\"relay\" : 2 }\n ", &c), RC_OK);
    UT_EQ_INT(c.kind, CMD_KIND_TOGGLE); UT_EQ_INT(c.relay, 2);

    UT_EQ_INT(parse("{\"relay\":1,\"state\":true}", &c), RC_OK);
    UT_EQ_INT(c.kind, CMD_KIND_SET); UT_CHECK(c.on);
    UT_EQ_INT(parse("{\"relay\":1,\"state\":false}", &c), RC_OK);
    UT_CHECK(!c.on);

    UT_EQ_INT(parse("{\"query\":\"status\"}", &c), RC_OK);
    UT_EQ_INT(c.kind, CMD_KIND_QUERY);
}

static void test_unknown_keys_ignored(void)
{
    cmd_t c;
    /* "ts" kiểu Date.now() của JavaScript (13 chữ số) không được làm hỏng lệnh. */
    UT_EQ_INT(parse("{\"id\":\"abc\",\"relay\":1,\"ts\":1731253200000,\"x\":null,\"y\":false,\"state\":\"on\"}", &c), RC_OK);
    UT_EQ_INT(c.kind, CMD_KIND_SET);
    UT_EQ_INT(c.relay, 1);
}

static void test_not_nul_terminated(void)
{
    cmd_t c;
    const char buf[] = "{\"relay\":2,\"state\":\"on\"}GARBAGE";
    const size_t n = strlen("{\"relay\":2,\"state\":\"on\"}");
    UT_EQ_INT(cmd_parse(buf, n, &c), RC_OK); /* chỉ đọc đúng n byte (dữ liệu MQTT) */
    UT_EQ_INT(c.relay, 2);
    UT_EQ_INT(cmd_parse(buf, n - 1U, &c), RC_ERR_SYNTAX); /* thiếu '}' */
    UT_EQ_INT(cmd_parse(buf, n + 1U, &c), RC_ERR_SYNTAX); /* rác sau '}' */
}

static void test_semantic_errors(void)
{
    cmd_t c;
    UT_EQ_INT(parse("{}", &c), RC_ERR_ARG);
    UT_EQ_INT(parse("{\"relay\":1}", &c), RC_ERR_ARG);
    UT_EQ_INT(parse("{\"state\":\"on\"}", &c), RC_ERR_ARG);
    UT_EQ_INT(parse("{\"relay\":0,\"state\":\"on\"}", &c), RC_ERR_RANGE);
    UT_EQ_INT(parse("{\"relay\":3,\"state\":\"on\"}", &c), RC_ERR_RANGE);
    UT_EQ_INT(parse("{\"relay\":\"1\",\"state\":\"on\"}", &c), RC_ERR_RANGE);
    UT_EQ_INT(parse("{\"relay\":1,\"state\":\"ON\"}", &c), RC_ERR_RANGE);
    UT_EQ_INT(parse("{\"relay\":1,\"state\":1}", &c), RC_ERR_RANGE);
    UT_EQ_INT(parse("{\"relay\":1,\"state\":null}", &c), RC_ERR_RANGE);
    UT_EQ_INT(parse("{\"query\":\"all\"}", &c), RC_ERR_RANGE);
    UT_EQ_INT(parse("{\"query\":\"status\",\"relay\":1,\"state\":\"on\"}", &c), RC_ERR_ARG);
    UT_EQ_INT(parse("{\"relay\":4294967295,\"state\":\"on\"}", &c), RC_ERR_RANGE); /* tràn số */
    UT_EQ_INT(parse("{\"relay\":1,\"state\":\"aaaaaaaaaaaaaaaaaaaa\"}", &c), RC_ERR_RANGE);
    /* Khi lỗi, kết quả luôn được xoá về NONE. */
    UT_EQ_INT(c.kind, CMD_KIND_NONE);
    UT_EQ_INT(c.relay, 0);
}

static void test_syntax_errors(void)
{
    static const char *const bad[] =
    {
        "",
        "   ",
        "[1,2]",
        "{",
        "{\"relay\":1,\"state\":\"on\"",
        "{\"relay\":1,\"state\":\"on\",}",
        "{\"relay\":1,,\"state\":\"on\"}",
        "{\"relay\" 1,\"state\":\"on\"}",
        "{relay:1,\"state\":\"on\"}",
        "{\"relay\":1,\"state\":\"on\"} x",
        "{\"relay\":1,\"state\":\"on\"}{}",
        "{\"relay\":-1,\"state\":\"on\"}",
        "{\"relay\":1.0,\"state\":\"on\"}",
        "{\"relay\":1e0,\"state\":\"on\"}",
        "{\"relay\":01,\"state\":\"on\"}",
        "{\"relay\":1,\"state\":\"o\\n\"}",
        "{\"relay\":1,\"state\":\"on}",
        "{\"relay\":1,\"state\":tru}",
        "{\"relay\":1,\"state\":{\"v\":1}}",
        "{\"relay\":1,\"state\":[\"on\"]}",
        "{\"relay\":1,\"relay\":2,\"state\":\"on\"}",
        "{\"aaaaaaaaaaaaaaaaaaaaaaa\":1}",
        "{\"relay\":1,\"state\":\"on\"\x01}"
    };
    size_t i;
    cmd_t c;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
        const rc_status_t rc = parse(bad[i], &c);
        if (rc != RC_ERR_SYNTAX)
        {
            printf("    case %u: \"%s\" -> %s\n", (unsigned)i, bad[i], rc_status_name(rc));
        }
        UT_EQ_INT(rc, RC_ERR_SYNTAX);
        UT_EQ_INT(c.kind, CMD_KIND_NONE);
    }
}

static void test_limits(void)
{
    char big[CMD_MAX_INPUT_LEN + 2U];
    cmd_t c;

    memset(big, ' ', sizeof(big));
    UT_EQ_INT(cmd_parse(big, CMD_MAX_INPUT_LEN + 1U, &c), RC_ERR_OVERFLOW);
    UT_EQ_INT(cmd_parse(NULL, 3U, &c), RC_ERR_ARG);
    UT_EQ_INT(cmd_parse("{}", 2U, NULL), RC_ERR_ARG);
    UT_EQ_INT(parse("{\"a\":1,\"b\":1,\"c\":1,\"d\":1,\"e\":1,\"f\":1,\"g\":1,\"h\":1,\"i\":1}", &c),
              RC_ERR_OVERFLOW);

    /* Đúng giới hạn 256 byte (đệm khoảng trắng) vẫn hợp lệ. */
    {
        const char *cmd = "{\"relay\":1,\"state\":\"on\"}";
        const size_t n = strlen(cmd);
        memset(big, ' ', sizeof(big));
        memcpy(big, cmd, n);
        UT_EQ_INT(cmd_parse(big, CMD_MAX_INPUT_LEN, &c), RC_OK);
    }
}

void run_cmd_parser_tests(void)
{
    UT_RUN(test_valid_commands);
    UT_RUN(test_unknown_keys_ignored);
    UT_RUN(test_not_nul_terminated);
    UT_RUN(test_semantic_errors);
    UT_RUN(test_syntax_errors);
    UT_RUN(test_limits);
}
