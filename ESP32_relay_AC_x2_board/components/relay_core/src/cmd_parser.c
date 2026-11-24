/**
 * @file    cmd_parser.c
 * @brief   Bộ phân tích lệnh JSON – xem cmd_parser.h.
 */
#include "cmd_parser.h"
#include "relay.h"

#define CMD_U32_MAX_DIGITS  (9U)   /* ≤ 999 999 999 – không thể tràn uint32_t */

typedef enum
{
    JV_STRING = 0,
    JV_NUMBER,
    JV_TRUE,
    JV_FALSE,
    JV_NULL
} jv_type_t;

typedef struct
{
    jv_type_t type;
    uint32_t  num;
    bool      num_too_big;  /* > CMD_U32_MAX_DIGITS chữ số – hợp lệ cú pháp, sai miền */
    char      str[CMD_MAX_TOKEN_LEN + 1U];
} jv_t;

typedef struct
{
    const char *s;
    size_t      len;
    size_t      pos;
} cursor_t;

typedef struct
{
    bool    has_relay;
    bool    has_state;
    bool    has_query;
    jv_t    relay;
    jv_t    state;
    jv_t    query;
} fields_t;

/* ------------------------------------------------------------------ */
/* Tiện ích chuỗi (không dùng <string.h> để kiểm soát độ dài rõ ràng)  */
/* ------------------------------------------------------------------ */
static bool str_equal(const char *a, const char *b)
{
    size_t i = 0U;
    bool equal = true;
    bool done = false;

    while (!done)
    {
        if (a[i] != b[i])
        {
            equal = false;
            done = true;
        }
        else if (a[i] == '\0')
        {
            done = true;
        }
        else
        {
            i++;
        }
    }
    return equal;
}

/* ------------------------------------------------------------------ */
/* Con trỏ đọc                                                         */
/* ------------------------------------------------------------------ */
static bool cur_at_end(const cursor_t *c)
{
    return c->pos >= c->len;
}

static char cur_peek(const cursor_t *c)
{
    char ch = '\0';
    if (!cur_at_end(c))
    {
        ch = c->s[c->pos];
    }
    return ch;
}

static void cur_skip_ws(cursor_t *c)
{
    bool done = false;
    while ((!done) && (!cur_at_end(c)))
    {
        const char ch = c->s[c->pos];
        if ((ch == ' ') || (ch == '\t') || (ch == '\r') || (ch == '\n'))
        {
            c->pos++;
        }
        else
        {
            done = true;
        }
    }
}

static bool cur_expect(cursor_t *c, char ch)
{
    bool ok = false;
    if ((!cur_at_end(c)) && (c->s[c->pos] == ch))
    {
        c->pos++;
        ok = true;
    }
    return ok;
}

static bool cur_expect_word(cursor_t *c, const char *word)
{
    bool ok = true;
    size_t i = 0U;

    while (ok && (word[i] != '\0'))
    {
        ok = cur_expect(c, word[i]);
        i++;
    }
    return ok;
}

/* ------------------------------------------------------------------ */
/* Đọc token                                                           */
/* ------------------------------------------------------------------ */
static rc_status_t read_string(cursor_t *c, char *dst)
{
    rc_status_t rc = RC_ERR_SYNTAX;

    if (cur_expect(c, '"'))
    {
        size_t n = 0U;
        bool done = false;

        while ((!done) && (!cur_at_end(c)))
        {
            const char ch = c->s[c->pos];
            const uint8_t u = (uint8_t)ch;
            c->pos++;
            if (ch == '"')
            {
                dst[n] = '\0';
                rc = RC_OK;
                done = true;
            }
            else if ((ch == '\\') || (u < 0x20U))
            {
                done = true; /* ký tự thoát / điều khiển không được hỗ trợ */
            }
            else if (n >= CMD_MAX_TOKEN_LEN)
            {
                rc = RC_ERR_RANGE;
                done = true;
            }
            else
            {
                dst[n] = ch;
                n++;
            }
        }
    }
    return rc;
}

/* Đọc số nguyên không dấu. Số quá dài vẫn được đọc hết (để khoá lạ như "ts":1700000000000
 * không làm hỏng lệnh) nhưng đánh dấu too_big; giá trị chỉ tích luỹ tối đa 9 chữ số → không tràn. */
static rc_status_t read_number(cursor_t *c, uint32_t *value, bool *too_big)
{
    rc_status_t rc = RC_ERR_SYNTAX;
    const size_t start = c->pos;
    uint32_t v = 0UL;
    size_t digits = 0U;
    bool done = false;

    while ((!done) && (!cur_at_end(c)))
    {
        const char ch = c->s[c->pos];
        if ((ch >= '0') && (ch <= '9'))
        {
            if (digits < CMD_U32_MAX_DIGITS)
            {
                const uint8_t digit = (uint8_t)((uint8_t)ch - (uint8_t)'0');
                v = (v * 10UL) + (uint32_t)digit;
            }
            digits++;
            c->pos++;
        }
        else
        {
            done = true;
        }
    }

    if (digits > 0U)
    {
        /* Số kiểu JSON không được có số 0 thừa ở đầu, và phải là số nguyên. */
        const char next = cur_peek(c);
        const bool leading_zero = (digits > 1U) && (c->s[start] == '0');
        if ((!leading_zero) && (next != '.') && (next != 'e') && (next != 'E'))
        {
            *too_big = (digits > CMD_U32_MAX_DIGITS);
            *value = (*too_big) ? 0UL : v;
            rc = RC_OK;
        }
    }
    return rc;
}

static rc_status_t read_value(cursor_t *c, jv_t *val)
{
    rc_status_t rc = RC_ERR_SYNTAX;
    const char ch = cur_peek(c);

    val->num = 0UL;
    val->num_too_big = false;
    val->str[0] = '\0';

    if (ch == '"')
    {
        val->type = JV_STRING;
        rc = read_string(c, val->str);
    }
    else if ((ch >= '0') && (ch <= '9'))
    {
        val->type = JV_NUMBER;
        rc = read_number(c, &val->num, &val->num_too_big);
    }
    else if (ch == 't')
    {
        val->type = JV_TRUE;
        if (cur_expect_word(c, "true"))
        {
            rc = RC_OK;
        }
    }
    else if (ch == 'f')
    {
        val->type = JV_FALSE;
        if (cur_expect_word(c, "false"))
        {
            rc = RC_OK;
        }
    }
    else if (ch == 'n')
    {
        val->type = JV_NULL;
        if (cur_expect_word(c, "null"))
        {
            rc = RC_OK;
        }
    }
    else
    {
        /* Số âm, đối tượng/mảng lồng nhau, ký tự lạ → lỗi cú pháp. */
    }
    return rc;
}

/* ------------------------------------------------------------------ */
/* Ghi nhận trường                                                     */
/* ------------------------------------------------------------------ */
static rc_status_t store_field(fields_t *f, const char *key, const jv_t *val)
{
    rc_status_t rc = RC_OK;

    if (str_equal(key, "relay"))
    {
        if (f->has_relay) { rc = RC_ERR_SYNTAX; } else { f->relay = *val; f->has_relay = true; }
    }
    else if (str_equal(key, "state"))
    {
        if (f->has_state) { rc = RC_ERR_SYNTAX; } else { f->state = *val; f->has_state = true; }
    }
    else if (str_equal(key, "query"))
    {
        if (f->has_query) { rc = RC_ERR_SYNTAX; } else { f->query = *val; f->has_query = true; }
    }
    else
    {
        /* Khoá lạ – bỏ qua. */
    }
    return rc;
}

static rc_status_t parse_object(cursor_t *c, fields_t *f)
{
    rc_status_t rc = RC_OK;
    uint8_t keys = 0U;
    bool done = false;
    char key[CMD_MAX_TOKEN_LEN + 1U];
    jv_t val;

    cur_skip_ws(c);
    if (!cur_expect(c, '{'))
    {
        rc = RC_ERR_SYNTAX;
    }
    else
    {
        cur_skip_ws(c);
        if (cur_expect(c, '}'))
        {
            done = true; /* đối tượng rỗng – cú pháp đúng, xử lý ngữ nghĩa sau */
        }
    }

    while ((rc == RC_OK) && (!done))
    {
        cur_skip_ws(c);
        rc = read_string(c, key);
        if (rc == RC_ERR_RANGE)
        {
            rc = RC_ERR_SYNTAX; /* khoá quá dài – chắc chắn không phải khoá hợp lệ */
        }
        if (rc == RC_OK)
        {
            keys++;
            if (keys > CMD_MAX_KEYS)
            {
                rc = RC_ERR_OVERFLOW;
            }
        }
        if (rc == RC_OK)
        {
            cur_skip_ws(c);
            if (!cur_expect(c, ':'))
            {
                rc = RC_ERR_SYNTAX;
            }
        }
        if (rc == RC_OK)
        {
            cur_skip_ws(c);
            rc = read_value(c, &val);
        }
        if (rc == RC_OK)
        {
            rc = store_field(f, key, &val);
        }
        if (rc == RC_OK)
        {
            cur_skip_ws(c);
            if (cur_expect(c, '}'))
            {
                done = true;
            }
            else if (!cur_expect(c, ','))
            {
                rc = RC_ERR_SYNTAX;
            }
            else
            {
                /* còn cặp khoá/giá trị tiếp theo */
            }
        }
    }

    if (rc == RC_OK)
    {
        cur_skip_ws(c);
        if (!cur_at_end(c))
        {
            rc = RC_ERR_SYNTAX; /* rác sau dấu '}' */
        }
    }
    return rc;
}

/* ------------------------------------------------------------------ */
/* Ngữ nghĩa                                                           */
/* ------------------------------------------------------------------ */
static rc_status_t interpret_state(const jv_t *state, cmd_t *out)
{
    rc_status_t rc = RC_OK;

    if (state->type == JV_TRUE)
    {
        out->kind = CMD_KIND_SET;
        out->on = true;
    }
    else if (state->type == JV_FALSE)
    {
        out->kind = CMD_KIND_SET;
        out->on = false;
    }
    else if (state->type != JV_STRING)
    {
        rc = RC_ERR_RANGE;
    }
    else if (str_equal(state->str, "on"))
    {
        out->kind = CMD_KIND_SET;
        out->on = true;
    }
    else if (str_equal(state->str, "off"))
    {
        out->kind = CMD_KIND_SET;
        out->on = false;
    }
    else if (str_equal(state->str, "toggle"))
    {
        out->kind = CMD_KIND_TOGGLE;
        out->on = false;
    }
    else
    {
        rc = RC_ERR_RANGE;
    }
    return rc;
}

static rc_status_t interpret(const fields_t *f, cmd_t *out)
{
    rc_status_t rc = RC_ERR_ARG;

    if (f->has_query)
    {
        if (f->has_relay || f->has_state)
        {
            rc = RC_ERR_ARG; /* không trộn truy vấn với lệnh điều khiển */
        }
        else if ((f->query.type == JV_STRING) && str_equal(f->query.str, "status"))
        {
            out->kind = CMD_KIND_QUERY;
            rc = RC_OK;
        }
        else
        {
            rc = RC_ERR_RANGE;
        }
    }
    else if (f->has_relay && f->has_state)
    {
        if ((f->relay.type != JV_NUMBER) || f->relay.num_too_big || (f->relay.num < 1UL) ||
            (f->relay.num > (uint32_t)RELAY_COUNT))
        {
            rc = RC_ERR_RANGE;
        }
        else
        {
            rc = interpret_state(&f->state, out);
            if (rc == RC_OK)
            {
                out->relay = (uint8_t)f->relay.num;
            }
        }
    }
    else
    {
        /* thiếu trường bắt buộc – rc giữ RC_ERR_ARG */
    }
    return rc;
}

rc_status_t cmd_parse(const char *json, size_t len, cmd_t *out)
{
    rc_status_t rc = RC_ERR_ARG;

    if ((json != NULL) && (out != NULL))
    {
        out->kind = CMD_KIND_NONE;
        out->relay = 0U;
        out->on = false;

        if (len > CMD_MAX_INPUT_LEN)
        {
            rc = RC_ERR_OVERFLOW;
        }
        else
        {
            cursor_t cur;
            fields_t fields;

            cur.s = json;
            cur.len = len;
            cur.pos = 0U;
            fields.has_relay = false;
            fields.has_state = false;
            fields.has_query = false;

            rc = parse_object(&cur, &fields);
            if (rc == RC_OK)
            {
                rc = interpret(&fields, out);
            }
            if (rc != RC_OK)
            {
                out->kind = CMD_KIND_NONE;
                out->relay = 0U;
                out->on = false;
            }
        }
    }
    return rc;
}
