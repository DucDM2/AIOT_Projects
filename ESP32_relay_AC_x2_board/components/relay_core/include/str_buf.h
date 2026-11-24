/**
 * @file    str_buf.h
 * @brief   Bộ đệm chuỗi có giới hạn – thay cho snprintf/strcat (MISRA Rule 21.6, Dir 4.11).
 *
 * Không bao giờ ghi quá @c cap byte; luôn kết thúc bằng '\0'.
 * Khi hết chỗ, cờ @c overflow bật và mọi lệnh append sau đó bị bỏ qua.
 */
#ifndef STR_BUF_H
#define STR_BUF_H

#include "rc_types.h"

typedef struct
{
    char  *buf;
    size_t cap;       /**< Kích thước bộ đệm, tính cả '\0'. */
    size_t len;       /**< Số ký tự đã ghi (không tính '\0'). */
    bool   overflow;
} str_buf_t;

rc_status_t sb_init(str_buf_t *sb, char *buf, size_t cap);
void sb_append_char(str_buf_t *sb, char c);
void sb_append_str(str_buf_t *sb, const char *s);
void sb_append_u32(str_buf_t *sb, uint32_t value);
void sb_append_i32(str_buf_t *sb, int32_t value);
/** Ghi chuỗi JSON có ngoặc kép và thoát ký tự (" \\ và ký tự điều khiển). */
void sb_append_json_str(str_buf_t *sb, const char *s);
void sb_append_bool(str_buf_t *sb, bool value);
/** Ghi 1 byte dạng 2 chữ số hex thường, vd 0xA1 → "a1". */
void sb_append_hex8(str_buf_t *sb, uint8_t value);

#endif /* STR_BUF_H */
