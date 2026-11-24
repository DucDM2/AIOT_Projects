/**
 * @file    status_json.h
 * @brief   Dựng bản tin trạng thái JSON gửi lên EMQX / trả cho web.
 *
 * Mẫu (một dòng):
 * {"device":"relay-a1b2c3","fw":"0.1.0","online":true,"relay1":false,"relay2":true,
 *  "seq":5,"uptime_s":123,"rssi":-55,"mqtt":true,"reason":"mqtt"}
 *
 * Bản tin "di chúc" (LWT) khi mất kết nối:
 * {"device":"relay-a1b2c3","online":false}
 */
#ifndef STATUS_JSON_H
#define STATUS_JSON_H

#include "rc_types.h"

typedef enum
{
    STATUS_REASON_BOOT = 0,
    STATUS_REASON_CONNECT,
    STATUS_REASON_WEB,
    STATUS_REASON_MQTT,
    STATUS_REASON_BUTTON,
    STATUS_REASON_HEARTBEAT,
    STATUS_REASON_QUERY
} status_reason_t;

typedef struct
{
    const char     *device_id;
    const char     *fw_version;
    uint8_t         relay_mask;   /**< bit0 = relay1, bit1 = relay2 */
    uint32_t        seq;          /**< tăng mỗi lần trạng thái relay đổi */
    uint32_t        uptime_s;
    int32_t         rssi;
    bool            rssi_valid;
    bool            mqtt_connected;
    status_reason_t reason;
} status_info_t;

const char *status_reason_name(status_reason_t reason);

/** @return RC_ERR_OVERFLOW nếu @p cap không đủ (out vẫn kết thúc '\0'). */
rc_status_t status_json_build(const status_info_t *info, char *out, size_t cap, size_t *out_len);

rc_status_t status_json_build_offline(const char *device_id, char *out, size_t cap, size_t *out_len);

/** {"ok":true|false,"error":"<rc>"} – phản hồi lệnh. */
rc_status_t status_json_build_ack(rc_status_t result, char *out, size_t cap, size_t *out_len);

#endif /* STATUS_JSON_H */
