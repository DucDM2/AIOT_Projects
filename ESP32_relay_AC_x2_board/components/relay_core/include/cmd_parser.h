/**
 * @file    cmd_parser.h
 * @brief   Bộ phân tích lệnh JSON dùng chung cho MQTT và HTTP.
 *
 * Lệnh hợp lệ (một đối tượng JSON phẳng):
 *   {"relay":1,"state":"on"}      bật relay 1
 *   {"relay":2,"state":"off"}     tắt relay 2
 *   {"relay":1,"state":"toggle"}  đảo relay 1
 *   {"relay":2,"state":true}      true/false tương đương "on"/"off"
 *   {"query":"status"}            yêu cầu gửi lại trạng thái
 *
 * Quy tắc chặt:
 *  - Không đệ quy, không cấp phát động; dữ liệu vào KHÔNG cần kết thúc '\0'.
 *  - Tối đa CMD_MAX_INPUT_LEN byte; khoá/giá trị chuỗi tối đa CMD_MAX_TOKEN_LEN ký tự,
 *    không chấp nhận ký tự thoát '\\' (lệnh không cần).
 *  - Khoá trùng lặp → lỗi cú pháp (tránh mơ hồ).
 *  - Khoá lạ được bỏ qua nếu giá trị là chuỗi/số/true/false/null (tương thích về sau);
 *    đối tượng/mảng lồng nhau → lỗi cú pháp.
 */
#ifndef CMD_PARSER_H
#define CMD_PARSER_H

#include "rc_types.h"

#define CMD_MAX_INPUT_LEN   (256U)
#define CMD_MAX_TOKEN_LEN   (15U)
#define CMD_MAX_KEYS        (8U)

typedef enum
{
    CMD_KIND_NONE = 0,
    CMD_KIND_SET,
    CMD_KIND_TOGGLE,
    CMD_KIND_QUERY
} cmd_kind_t;

typedef struct
{
    cmd_kind_t kind;
    uint8_t    relay;   /**< 1..2, hợp lệ với SET/TOGGLE */
    bool       on;      /**< hợp lệ với SET */
} cmd_t;

/**
 * @return RC_OK, RC_ERR_SYNTAX (JSON hỏng), RC_ERR_RANGE (relay/state sai giá trị),
 *         RC_ERR_ARG (thiếu/thừa trường, con trỏ NULL), RC_ERR_OVERFLOW (quá dài).
 */
rc_status_t cmd_parse(const char *json, size_t len, cmd_t *out);

#endif /* CMD_PARSER_H */
