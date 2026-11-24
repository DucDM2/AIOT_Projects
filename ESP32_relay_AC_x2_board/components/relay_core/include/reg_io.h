/**
 * @file    reg_io.h
 * @brief   Lớp truy cập thanh ghi 32-bit (memory-mapped I/O).
 *
 * Đây là "đường may" duy nhất giữa phần mềm và phần cứng:
 *  - Trên ESP32: port/esp32/reg_io_esp32.c đọc/ghi thật qua con trỏ volatile.
 *  - Trên máy tính: tests/host/sim/reg_io_sim.c mô phỏng khối thanh ghi GPIO/IO_MUX
 *    (kể cả hành vi W1TS/W1TC) để kiểm thử driver ở mức thanh ghi.
 *
 * Mọi driver trong relay_core CHỈ được chạm phần cứng qua hai hàm này.
 */
#ifndef REG_IO_H
#define REG_IO_H

#include <stdint.h>

/** Đọc thanh ghi 32-bit tại địa chỉ @p addr. */
uint32_t reg_io_read(uint32_t addr);

/** Ghi giá trị @p value vào thanh ghi 32-bit tại địa chỉ @p addr. */
void reg_io_write(uint32_t addr, uint32_t value);

#endif /* REG_IO_H */
