/**
 * @file    rc_types.c
 * @brief   Tên mã lỗi.
 */
#include "rc_types.h"

const char *rc_status_name(rc_status_t status)
{
    const char *name;

    switch (status)
    {
        case RC_OK:           name = "ok";       break;
        case RC_ERR_ARG:      name = "arg";      break;
        case RC_ERR_STATE:    name = "state";    break;
        case RC_ERR_BUSY:     name = "busy";     break;
        case RC_ERR_SYNTAX:   name = "syntax";   break;
        case RC_ERR_RANGE:    name = "range";    break;
        case RC_ERR_OVERFLOW: name = "overflow"; break;
        default:              name = "unknown";  break;
    }
    return name;
}
