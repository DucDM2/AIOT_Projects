/**
 * @file    app_io.h
 * @brief   Task chu kỳ 10 ms: đọc nút nhấn, nháy LED, gửi heartbeat, tự kiểm thanh ghi.
 *
 * Nút BOOT (IO0): nhấn ngắn = đảo Relay 1, nhấn giữ ≥ APP_BUTTON_LONG_MS = đảo Relay 2.
 */
#ifndef APP_IO_H
#define APP_IO_H

void app_io_start(void);

#endif /* APP_IO_H */
