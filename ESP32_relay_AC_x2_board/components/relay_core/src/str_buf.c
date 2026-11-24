/**
 * @file    str_buf.c
 * @brief   Bộ đệm chuỗi có giới hạn – xem str_buf.h.
 */
#include "str_buf.h"

#define SB_U32_MAX_DIGITS  (10U)

static const char k_hex_digits[16] =
{
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
};

static bool sb_usable(const str_buf_t *sb)
{
    return (sb != NULL) && (sb->buf != NULL) && (!sb->overflow);
}

rc_status_t sb_init(str_buf_t *sb, char *buf, size_t cap)
{
    rc_status_t rc = RC_ERR_ARG;

    if ((sb != NULL) && (buf != NULL) && (cap > 0U))
    {
        sb->buf = buf;
        sb->cap = cap;
        sb->len = 0U;
        sb->overflow = false;
        buf[0] = '\0';
        rc = RC_OK;
    }
    return rc;
}

void sb_append_char(str_buf_t *sb, char c)
{
    if (sb_usable(sb))
    {
        if ((sb->len + 1U) < sb->cap)
        {
            sb->buf[sb->len] = c;
            sb->len++;
            sb->buf[sb->len] = '\0';
        }
        else
        {
            sb->overflow = true;
        }
    }
}

void sb_append_str(str_buf_t *sb, const char *s)
{
    if (s != NULL)
    {
        size_t i = 0U;
        while ((s[i] != '\0') && sb_usable(sb))
        {
            sb_append_char(sb, s[i]);
            i++;
        }
    }
}

void sb_append_u32(str_buf_t *sb, uint32_t value)
{
    char digits[SB_U32_MAX_DIGITS];
    uint32_t v = value;
    uint8_t n = 0U;

    do
    {
        digits[n] = k_hex_digits[v % 10UL]; /* tra bảng – tránh số học trên kiểu char (Rule 10.2) */
        n++;
        v /= 10UL;
    } while ((v != 0UL) && (n < SB_U32_MAX_DIGITS));

    while (n > 0U)
    {
        n--;
        sb_append_char(sb, digits[n]);
    }
}

void sb_append_i32(str_buf_t *sb, int32_t value)
{
    uint32_t magnitude;

    if (value < 0)
    {
        sb_append_char(sb, '-');
        /* -(INT32_MIN) tràn số có dấu → tính trên kiểu không dấu (Rule 10.x an toàn). */
        magnitude = (uint32_t)(~(uint32_t)value) + 1UL;
    }
    else
    {
        magnitude = (uint32_t)value;
    }
    sb_append_u32(sb, magnitude);
}

void sb_append_json_str(str_buf_t *sb, const char *s)
{
    sb_append_char(sb, '"');
    if (s != NULL)
    {
        size_t i = 0U;
        while ((s[i] != '\0') && sb_usable(sb))
        {
            const char c = s[i];
            const uint8_t u = (uint8_t)c;
            if ((c == '"') || (c == '\\'))
            {
                sb_append_char(sb, '\\');
                sb_append_char(sb, c);
            }
            else if (u < 0x20U)
            {
                sb_append_str(sb, "\\u00");
                sb_append_hex8(sb, u);
            }
            else
            {
                sb_append_char(sb, c);
            }
            i++;
        }
    }
    sb_append_char(sb, '"');
}

void sb_append_hex8(str_buf_t *sb, uint8_t value)
{
    sb_append_char(sb, k_hex_digits[(uint8_t)(value >> 4U) & 0x0FU]);
    sb_append_char(sb, k_hex_digits[value & 0x0FU]);
}

void sb_append_bool(str_buf_t *sb, bool value)
{
    if (value)
    {
        sb_append_str(sb, "true");
    }
    else
    {
        sb_append_str(sb, "false");
    }
}
