/**
 * @file    app_ctrl.h
 * @brief   Bộ điều phối: mọi lệnh relay (web, MQTT, nút nhấn) đi qua đây, có khoá mutex.
 */
#ifndef APP_CTRL_H
#define APP_CTRL_H

#include "rc_types.h"
#include "cmd_parser.h"
#include "status_json.h"

/** Khởi tạo GPIO + relay (TẮT hết), LED, nút nhấn. Gọi đầu tiên trong app_main. */
void app_ctrl_init(void);

/** Mã thiết bị dạng "relay-a1b2c3" (3 byte cuối MAC Wi-Fi). */
const char *app_ctrl_device_id(void);

/** Thời gian từ lúc khởi động (ms, tràn vòng sau ~49 ngày – các phép so sánh đã xử lý). */
uint32_t app_ctrl_now_ms(void);

/** Thực thi lệnh; nếu trạng thái đổi (hoặc là truy vấn) thì gửi trạng thái lên MQTT. */
rc_status_t app_ctrl_execute(const cmd_t *cmd, status_reason_t reason);

/** Dựng JSON trạng thái hiện tại. */
rc_status_t app_ctrl_build_status(status_reason_t reason, char *out, size_t cap, size_t *out_len);

/** Dựng và gửi trạng thái lên MQTT (bỏ qua nếu chưa kết nối). */
void app_ctrl_publish_status(status_reason_t reason);

/** So khớp thanh ghi GPIO_OUT với trạng thái relay; RC_ERR_STATE nếu lệch. */
rc_status_t app_ctrl_self_check(void);

#endif /* APP_CTRL_H */
