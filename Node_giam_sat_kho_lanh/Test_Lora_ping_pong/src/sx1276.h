/*
 * sx1276.h - Driver toi gian SX1276 (che do LoRa) cho RAK811.
 * Tham chieu: Semtech SX1276/77/78/79 datasheet Rev.7 + errata, driver LoRaMac-node cua Semtech/RAK.
 * Hoat dong theo kieu hoi vong (polling) thanh ghi RegIrqFlags -> khong can ngat DIO.
 */
#ifndef SX1276_H
#define SX1276_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t freq_hz;
    int8_t   tx_power_dbm;   /* 2..20 (PA_BOOST) */
    uint8_t  sf;             /* 7..12 */
    uint16_t bw_khz;         /* 125 / 250 / 500 */
    uint8_t  cr;             /* 1..4 -> 4/5..4/8 */
    uint16_t preamble;
    uint8_t  sync_word;
} sx1276_config_t;

typedef struct {
    int16_t rssi_dbm;        /* cuong do goi tin */
    int16_t snr_x4;          /* SNR x4 (don vi 0.25 dB) */
} sx1276_pkt_info_t;

/* Tra ve gia tri thanh ghi RegVersion (0x12 = dung SX1276). */
uint8_t  sx1276_init(const sx1276_config_t *cfg);
bool     sx1276_present(void);
/* Ban module tu nhan dien: 0 = khong ro, 1 = H, 2 = L. mismatch = nap nham file -> radio bi khoa. */
int      sx1276_detected_variant(void);
bool     sx1276_variant_mismatch(void);

/* Phat blocking: tra ve true khi co TxDone trong thoi gian cho phep. */
bool     sx1276_send(const uint8_t *data, uint8_t len);

/* Bat che do thu lien tuc. */
void     sx1276_rx_start(void);

/* Hoi xem co goi moi chua: >0 = do dai goi, 0 = chua co, -1 = goi loi CRC. */
int      sx1276_rx_poll(uint8_t *buf, uint8_t maxlen, sx1276_pkt_info_t *info);

void     sx1276_standby(void);
void     sx1276_sleep(void);

/* Thoi gian phat song (ms) cho 1 goi co do dai len theo cau hinh hien tai. */
uint32_t sx1276_airtime_ms(uint8_t len);

/* So ngau nhien 32-bit lay tu nhieu RSSI bang rong (goi khi radio da init). */
uint32_t sx1276_random(void);

#endif /* SX1276_H */
