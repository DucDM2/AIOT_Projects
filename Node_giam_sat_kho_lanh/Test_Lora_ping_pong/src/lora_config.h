/*
 * lora_config.h - Thong so song LoRa. HAI NODE PHAI GIONG HET NHAU de nghe duoc nhau.
 *
 * Luu y phap ly (Viet Nam, tham khao - hay kiem tra quy dinh hien hanh):
 *   - 920-923 MHz (AS923-2): thuong gioi han ~25 mW (14 dBm) EIRP
 *   - 433.05-434.79 MHz (SRD): thuong gioi han ~10 mW (10 dBm)
 */
#ifndef LORA_CONFIG_H
#define LORA_CONFIG_H

#if defined(RAK811_VARIANT_L)
#define LORA_FREQ_HZ        433175000UL   /* kenh 433.175 MHz */
#define LORA_TX_POWER_DBM   2             /* TEST TREN BAN: 2 dBm (an toan, du cho vai chuc met). Toi da 10 dBm */
#else
#define LORA_FREQ_HZ        921400000UL   /* kenh AS923-2 921.4 MHz */
#define LORA_TX_POWER_DBM   2             /* TEST TREN BAN: 2 dBm (an toan, du cho vai chuc met). Toi da 14 dBm */
#endif

#define LORA_SF             9             /* Spreading factor 7..12 */
#define LORA_BW_KHZ         125           /* 125 / 250 / 500 */
#define LORA_CR             1             /* 1=4/5, 2=4/6, 3=4/7, 4=4/8 */
#define LORA_PREAMBLE_LEN   8
#define LORA_SYNC_WORD      0x12          /* 0x12 = mang rieng (private); 0x34 = LoRaWAN cong cong */

/* Chu ky gui (ms) trong che do ping / chi-truyen */
/* An toan: khong phat trong 10 s dau sau khi cap nguon (kip rut dien neu quen gan anten) */
#define APP_TX_HOLDOFF_MS   10000U

#define APP_PERIOD_MS       2000U
/* Slave o che do AUTO khong nhan duoc PING trong thoi gian nay -> quay lai lam master */
#define APP_SLAVE_IDLE_MS   10000U

#endif /* LORA_CONFIG_H */
