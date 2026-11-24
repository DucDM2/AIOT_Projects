/**
 * @file    status_json.c
 * @brief   Dựng bản tin trạng thái JSON – xem status_json.h.
 */
#include "status_json.h"
#include "str_buf.h"

static rc_status_t finish(const str_buf_t *sb, size_t *out_len)
{
    rc_status_t rc = RC_OK;

    if (sb->overflow)
    {
        rc = RC_ERR_OVERFLOW;
    }
    if (out_len != NULL)
    {
        *out_len = sb->len;
    }
    return rc;
}

const char *status_reason_name(status_reason_t reason)
{
    const char *name;

    switch (reason)
    {
        case STATUS_REASON_BOOT:      name = "boot";      break;
        case STATUS_REASON_CONNECT:   name = "connect";   break;
        case STATUS_REASON_WEB:       name = "web";       break;
        case STATUS_REASON_MQTT:      name = "mqtt";      break;
        case STATUS_REASON_BUTTON:    name = "button";    break;
        case STATUS_REASON_HEARTBEAT: name = "heartbeat"; break;
        case STATUS_REASON_QUERY:     name = "query";     break;
        default:                      name = "unknown";   break;
    }
    return name;
}

rc_status_t status_json_build(const status_info_t *info, char *out, size_t cap, size_t *out_len)
{
    rc_status_t rc = RC_ERR_ARG;
    str_buf_t sb;

    if ((info != NULL) && (info->device_id != NULL) && (info->fw_version != NULL) &&
        (sb_init(&sb, out, cap) == RC_OK))
    {
        sb_append_str(&sb, "{\"device\":");
        sb_append_json_str(&sb, info->device_id);
        sb_append_str(&sb, ",\"fw\":");
        sb_append_json_str(&sb, info->fw_version);
        sb_append_str(&sb, ",\"online\":true,\"relay1\":");
        sb_append_bool(&sb, (info->relay_mask & 0x01U) != 0U);
        sb_append_str(&sb, ",\"relay2\":");
        sb_append_bool(&sb, (info->relay_mask & 0x02U) != 0U);
        sb_append_str(&sb, ",\"seq\":");
        sb_append_u32(&sb, info->seq);
        sb_append_str(&sb, ",\"uptime_s\":");
        sb_append_u32(&sb, info->uptime_s);
        sb_append_str(&sb, ",\"rssi\":");
        if (info->rssi_valid)
        {
            sb_append_i32(&sb, info->rssi);
        }
        else
        {
            sb_append_str(&sb, "null");
        }
        sb_append_str(&sb, ",\"mqtt\":");
        sb_append_bool(&sb, info->mqtt_connected);
        sb_append_str(&sb, ",\"reason\":");
        sb_append_json_str(&sb, status_reason_name(info->reason));
        sb_append_char(&sb, '}');
        rc = finish(&sb, out_len);
    }
    return rc;
}

rc_status_t status_json_build_offline(const char *device_id, char *out, size_t cap, size_t *out_len)
{
    rc_status_t rc = RC_ERR_ARG;
    str_buf_t sb;

    if ((device_id != NULL) && (sb_init(&sb, out, cap) == RC_OK))
    {
        sb_append_str(&sb, "{\"device\":");
        sb_append_json_str(&sb, device_id);
        sb_append_str(&sb, ",\"online\":false}");
        rc = finish(&sb, out_len);
    }
    return rc;
}

rc_status_t status_json_build_ack(rc_status_t result, char *out, size_t cap, size_t *out_len)
{
    rc_status_t rc = RC_ERR_ARG;
    str_buf_t sb;

    if (sb_init(&sb, out, cap) == RC_OK)
    {
        sb_append_str(&sb, "{\"ok\":");
        sb_append_bool(&sb, result == RC_OK);
        sb_append_str(&sb, ",\"error\":");
        if (result == RC_OK)
        {
            sb_append_str(&sb, "null");
        }
        else
        {
            sb_append_json_str(&sb, rc_status_name(result));
        }
        sb_append_char(&sb, '}');
        rc = finish(&sb, out_len);
    }
    return rc;
}
