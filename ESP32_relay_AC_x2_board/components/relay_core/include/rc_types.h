/**
 * @file    rc_types.h
 * @brief   Kiểu dữ liệu và mã trả về dùng chung cho lõi relay_core.
 *
 * MISRA C:2012 – chỉ dùng kiểu có độ rộng cố định (Dir 4.6).
 */
#ifndef RC_TYPES_H
#define RC_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/** Mã trả về của mọi hàm trong relay_core. */
typedef enum
{
    RC_OK = 0,          /**< Thành công. */
    RC_ERR_ARG,         /**< Tham số sai (con trỏ NULL, chân không hợp lệ...). */
    RC_ERR_STATE,       /**< Sai trạng thái (chưa khởi tạo, chân chưa cấu hình...). */
    RC_ERR_BUSY,        /**< Relay vừa đóng/cắt, chưa hết thời gian bảo vệ. */
    RC_ERR_SYNTAX,      /**< Lệnh JSON sai cú pháp. */
    RC_ERR_RANGE,       /**< Giá trị ngoài miền cho phép. */
    RC_ERR_OVERFLOW     /**< Bộ đệm không đủ chỗ. */
} rc_status_t;

/** Trả về tên ngắn (ASCII) của mã lỗi – dùng trong log và bản tin ack. */
const char *rc_status_name(rc_status_t status);

#endif /* RC_TYPES_H */
