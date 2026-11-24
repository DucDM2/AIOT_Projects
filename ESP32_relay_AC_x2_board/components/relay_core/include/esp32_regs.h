/**
 * @file    esp32_regs.h
 * @brief   Địa chỉ & trường bit thanh ghi GPIO / IO_MUX của ESP32 (bản gốc, không phải S2/S3/C3).
 *
 * Nguồn: ESP32 Technical Reference Manual, chương "IO_MUX and GPIO Matrix"
 *        (đối chiếu với ESP-IDF soc/esp32/include/soc/gpio_reg.h và io_mux_reg.h).
 *
 * MISRA: chỉ dùng macro hằng (object-like), không dùng macro hàm (Dir 4.9).
 */
#ifndef ESP32_REGS_H
#define ESP32_REGS_H

/* ---------------- Khối GPIO (DR_REG_GPIO_BASE) ---------------- */
#define ESP32_GPIO_BASE                    (0x3FF44000UL)
#define ESP32_GPIO_OUT_REG                 (0x3FF44004UL) /* mức ra GPIO0..31            */
#define ESP32_GPIO_OUT_W1TS_REG            (0x3FF44008UL) /* ghi 1 = đặt bit OUT          */
#define ESP32_GPIO_OUT_W1TC_REG            (0x3FF4400CUL) /* ghi 1 = xoá bit OUT          */
#define ESP32_GPIO_ENABLE_REG              (0x3FF44020UL) /* cho phép lái ra GPIO0..31    */
#define ESP32_GPIO_ENABLE_W1TS_REG         (0x3FF44024UL)
#define ESP32_GPIO_ENABLE_W1TC_REG         (0x3FF44028UL)
#define ESP32_GPIO_IN_REG                  (0x3FF4403CUL) /* mức vào thực tế trên chân    */
#define ESP32_GPIO_PIN0_REG                (0x3FF44088UL) /* GPIO_PINn_REG = PIN0 + 4*n   */
#define ESP32_GPIO_FUNC0_OUT_SEL_CFG_REG   (0x3FF44530UL) /* FUNCn_OUT_SEL = FUNC0 + 4*n  */
#define ESP32_GPIO_REG_STRIDE              (4UL)
#define ESP32_GPIO_BLOCK_SIZE              (0x0700UL)     /* vùng mô phỏng                */

/* GPIO_PINn_REG */
#define ESP32_GPIO_PIN_PAD_DRIVER_BIT      (0x00000004UL) /* 1 = open-drain, 0 = push-pull */

/* GPIO_FUNCn_OUT_SEL_CFG_REG */
#define ESP32_GPIO_SIG_GPIO_OUT_IDX        (0x00000100UL) /* 256: tín hiệu "GPIO đơn giản",
                                                             OEN_SEL = 0 → OE lấy từ ENABLE_REG */

/* ---------------- Khối IO_MUX (DR_REG_IO_MUX_BASE) ---------------- */
#define ESP32_IO_MUX_BASE                  (0x3FF49000UL)
#define ESP32_IO_MUX_BLOCK_SIZE            (0x0100UL)

#define ESP32_IO_MUX_MCU_SEL_SHIFT         (12UL)
#define ESP32_IO_MUX_MCU_SEL_MASK          (0x00007000UL) /* bit 14..12                   */
#define ESP32_IO_MUX_FUNC_GPIO             (2UL)          /* Function 3 = GPIO            */
#define ESP32_IO_MUX_FUN_DRV_MASK          (0x00000C00UL) /* bit 11..10 – sức lái         */
#define ESP32_IO_MUX_FUN_IE_BIT            (0x00000200UL) /* bit 9 – bật bộ đệm vào       */
#define ESP32_IO_MUX_FUN_WPU_BIT           (0x00000100UL) /* bit 8 – kéo lên (pad số)     */
#define ESP32_IO_MUX_FUN_WPD_BIT           (0x00000080UL) /* bit 7 – kéo xuống (pad số)   */

#endif /* ESP32_REGS_H */
