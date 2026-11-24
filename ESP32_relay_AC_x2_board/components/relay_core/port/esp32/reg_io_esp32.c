/**
 * @file    reg_io_esp32.c
 * @brief   Truy cập thanh ghi thật trên ESP32 qua con trỏ volatile.
 *
 * MISRA C:2012 – DEVIATION D-01 (Rule 11.4, advisory): chuyển số nguyên → con trỏ.
 *   Lý do: đây là cách duy nhất truy cập thanh ghi memory-mapped trong C chuẩn.
 *   Kiểm soát: chỉ xuất hiện trong file này; địa chỉ chỉ đến từ esp32_regs.h;
 *   con trỏ 32-bit trên Xtensa LX6 nên không mất thông tin; địa chỉ luôn căn 4 byte.
 *   Xem docs/MISRA-DEVIATIONS.md.
 */
#include "reg_io.h"

uint32_t reg_io_read(uint32_t addr)
{
    /* cppcheck-suppress misra-c2012-11.4 ; DEVIATION D-01 */
    const volatile uint32_t *const reg = (const volatile uint32_t *)addr;
    return *reg;
}

void reg_io_write(uint32_t addr, uint32_t value)
{
    /* cppcheck-suppress misra-c2012-11.4 ; DEVIATION D-01 */
    volatile uint32_t *const reg = (volatile uint32_t *)addr;
    *reg = value;
}
