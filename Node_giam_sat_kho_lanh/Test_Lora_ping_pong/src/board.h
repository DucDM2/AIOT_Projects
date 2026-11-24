/*
 * board.h - Gan chan BEN TRONG module RAK811: STM32L151 <-> SX1276.
 *
 * Nguon: firmware goc cua RAK "RAK811_LoRaNode" (src/board/RAK811/board.h, LORA_HF_BOARD / LF),
 * doi chieu voi RAK811_BreakBoard va mbed-os target MTB_RAK811 (cho ban H).
 *
 * Chung cho ca 2 ban:
 *   SPI1: SCK=PA5 MISO=PA6 MOSI=PA7 (AF5), NSS=PB0
 *   XTAL_EN=PH1 (cap nguon bo dao dong 32 MHz cua SX1276), DIO1=PB1, DIO3=PH0, DIO4=PC13
 *   RF switch: CTX_PA=PA4
 *
 *                 RESET   DIO0   DIO2   CRX_RX   CBT_HF
 *   RAK811(H)     PB13    PA11   PA3    PB6      PB7       868/915/AS923
 *   RAK811(L)     PA8     PB4    PB9    PB3      PB8       433/470
 *
 * Cong tac RF (RAK): TX -> CTX_PA=1, CBT_HF=0, CRX_RX=0 ; RX -> CTX_PA=0, CBT_HF=0, CRX_RX=1
 * Duong ra cong suat: PA_BOOST (datasheet RAK811: toi da 20 dBm cho ca 2 ban).
 */
#ifndef BOARD_H
#define BOARD_H

#include "stm32l1xx.h"

#if !defined(RAK811_VARIANT_H) && !defined(RAK811_VARIANT_L)
#define RAK811_VARIANT_H
#endif
#if defined(RAK811_VARIANT_H) && defined(RAK811_VARIANT_L)
#error "Chi duoc chon mot trong RAK811_VARIANT_H hoac RAK811_VARIANT_L"
#endif

/* ---- Chung ---- */
#define RADIO_NSS_PORT      GPIOB
#define RADIO_NSS_PIN       0U
#define RADIO_SCK_PORT      GPIOA
#define RADIO_SCK_PIN       5U
#define RADIO_MISO_PORT     GPIOA
#define RADIO_MISO_PIN      6U
#define RADIO_MOSI_PORT     GPIOA
#define RADIO_MOSI_PIN      7U
#define RADIO_XTAL_PORT     GPIOH
#define RADIO_XTAL_PIN      1U
#define RADIO_CTX_PA_PORT   GPIOA
#define RADIO_CTX_PA_PIN    4U

/* Chan RESET cua SX1276 o CA HAI ban - dung de TU NHAN DIEN ban module (chong nap nham H/L) */
#define RADIO_RESET_H_PORT  GPIOB
#define RADIO_RESET_H_PIN   13U
#define RADIO_RESET_L_PORT  GPIOA
#define RADIO_RESET_L_PIN   8U

#if defined(RAK811_VARIANT_H)
#define RADIO_FW_IS_H       1
#define BOARD_VARIANT_NAME  "RAK811(H) 868/915/AS923"
#define RADIO_RESET_PORT    GPIOB
#define RADIO_RESET_PIN     13U
#define RADIO_DIO0_PORT     GPIOA
#define RADIO_DIO0_PIN      11U
#define RADIO_CRX_RX_PORT   GPIOB
#define RADIO_CRX_RX_PIN    6U
#define RADIO_CBT_HF_PORT   GPIOB
#define RADIO_CBT_HF_PIN    7U
#else
#define RADIO_FW_IS_H       0
#define BOARD_VARIANT_NAME  "RAK811(L) 433/470"
#define RADIO_RESET_PORT    GPIOA
#define RADIO_RESET_PIN     8U
#define RADIO_DIO0_PORT     GPIOB
#define RADIO_DIO0_PIN      4U
#define RADIO_CRX_RX_PORT   GPIOB
#define RADIO_CRX_RX_PIN    3U
#define RADIO_CBT_HF_PORT   GPIOB
#define RADIO_CBT_HF_PIN    8U
#endif

/* UART log - USART1 PA9/PA10 -> CH340 (USB) */
#define LOG_BAUDRATE        115200U

#endif /* BOARD_H */
