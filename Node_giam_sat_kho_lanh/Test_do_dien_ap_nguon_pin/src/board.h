/*
 * board.h - Cau hinh test do dien ap nguon / pin tren WisNode-LoRa (RAK811).
 *
 * 1) Dien ap nguon 3V3 (VDD = VDDA cua MCU): KHONG can noi day.
 *    Do kenh VREFINT (ADC kenh 17) roi tinh nguoc:
 *        VDDA = 3000 mV * VREFINT_CAL / VREFINT_raw
 *    VREFINT_CAL do o nha may (VDDA = 3.0 V, 30 C), luu tai 0x1FF80078.
 *
 * 2) Dien ap dau vao (pin hoac 5V USB) - KHONG bat buoc, can mach chia ap vao PA2:
 *        Vin ──[ R1 1M ]──┬──[ R2 1M ]── GND
 *                         ├── PA2 (P5-7, ADC_IN2)
 *                       [100 nF] (bat buoc, dat sat chan PA2)
 *                         └── GND
 *    Vin = V_PA2 * (R1 + R2) / R2. Voi 1M/1M do toi da ~2 x VDD (~6.6 V).
 *    PA2 trung chan voi project chinh (Project_node_kho_lanh/node/board.h).
 */
#ifndef BOARD_H
#define BOARD_H

#include "stm32l1xx.h"

/* ---- Mach chia ap do Vin ---- */
#define VIN_PORT            GPIOA
#define VIN_PIN             2U
#define VIN_ADC_CH          2U
#define VIN_PIN_NAME        "PA2 (P5-7)"
#define VIN_R_TOP_K         1000U    /* R1 (kOhm) - sua theo gia tri do thuc te bang dong ho */
#define VIN_R_BOT_K         1000U    /* R2 (kOhm) */
#define VIN_MIN_VALID_MV    1500U    /* Vin duoi muc nay -> coi nhu khong lap mach chia */
#define VIN_FLOAT_SPREAD_MV 300U     /* Vin nhay qua muc nay trong 1 loat mau -> PA2 dang bo ho */
#define VIN_PROBE_US        30U      /* xung pull 40k de phat hien PA2 bo ho (xem supply.c) */

/* Phan loai nguon theo Vin */
#define VIN_USB_MIN_MV      4400U    /* >= muc nay: nguon 5V (USB qua diode SS24 ~4.6-4.8 V) */
#define BATT_LOW_MV         3450U    /* Li-ion 3.7 V: ~10 % con lai (giong DEF_BATT_LOW_MV project chinh) */
#define LDO_DROPOUT_MV      1100U    /* LM1117 tren WisNode: 3V3 thap + (Vin - 3V3) < muc nay -> bao LDO sut ap; MCP1700: 200 */

/* ---- Nguong danh gia 3V3 (RAK811 VCC: 2.0..3.6 V; LoRa phat on dinh tu ~3.0 V) ---- */
#define VDD_OK_MIN_MV       3150U
#define VDD_OK_MAX_MV       3450U
#define VDD_LOW_MV          3000U    /* duoi muc nay: de reset / mat goi khi phat LoRa */
#define VDD_ABS_MAX_MV      3600U    /* qua muc nay: vuot gioi han module */
#define RIPPLE_MAX_MV       50U      /* gon dinh-dinh 3V3 cho phep */

/* ---- Chu ky ---- */
#define MONITOR_PERIOD_MS   250U     /* do nhanh de bat su kien doi nguon / sut ap */
#define STATUS_PERIOD_MS    5000U    /* in trang thai dinh ky */
#define VDD_EVENT_STEP_MV   60U      /* VDD nhay qua muc nay giua 2 lan do -> ghi su kien */

#define LOG_BAUDRATE        115200U

#endif /* BOARD_H */
