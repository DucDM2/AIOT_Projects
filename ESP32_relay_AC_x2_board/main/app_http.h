/**
 * @file    app_http.h
 * @brief   Web server cục bộ (cổng 80):
 *   GET  /             trang điều khiển 2 relay
 *   GET  /api/status   JSON trạng thái
 *   POST /api/relay    thân là lệnh JSON (cmd_parser.h), trả JSON trạng thái
 */
#ifndef APP_HTTP_H
#define APP_HTTP_H

void app_http_start(void);

#endif /* APP_HTTP_H */
