/*
 * am2301.h - Driver cam bien nhiet do / do am AM2301 (DHT21), bus 1 day cua Aosong.
 *
 * Theo "AM2301 Product Manual" V1.0 (ASAIR, docs/AM2301_Product_Manual.pdf):
 *   - Khung 40 bit, MSB truoc: RH[15:0] x10, T[15:0] x10, checksum = (b0+b1+b2+b3) & 0xFF
 *   - T am: bit15 = 1, bit14..0 = do lon (dau-do lon). Vi du -10.1 C = 0x8065
 *   - Doc cach nhau >= 2 s; sau khi cap nguon cho > 2 s
 *   - Gia tri doc duoc la KET QUA LAN DO TRUOC (lan doc nay kich hoat lan do moi)
 */
#ifndef AM2301_H
#define AM2301_H

#include <stdint.h>
#include <stdbool.h>

/* Bang 7 datasheet: thoi gian tin hieu (us) */
#define AM_TBE_US            2000U  /* MCU keo thap: 0.8..20 ms (chon 2 ms cho chac) */
#define AM_TGO_MAX_US         200U  /* MCU nha bus -> cam bien keo thap: 20..200 us */
#define AM_TRESP_MIN_US        60U  /* Trel / Treh chuan 75..85 us, chap nhan 60..110 */
#define AM_TRESP_MAX_US       110U
#define AM_TLOW_MIN_US         30U  /* muc thap moi bit: chuan 48..55 us, chap nhan 30..90 */
#define AM_TLOW_MAX_US         90U
#define AM_TH_MIN_US           10U  /* muc cao: "0" = 22..30 us, "1" = 68..75 us */
#define AM_TH_MAX_US          110U
#define AM_TH_THRESHOLD_US     48U  /* > nguong -> bit 1 (giua 30 va 68) */
#define AM_MIN_INTERVAL_MS   2000U

typedef enum {
    AM2301_OK = 0,
    AM2301_ERR_TOO_SOON,     /* doc lai chua du 2 s */
    AM2301_ERR_BUS_LOW,      /* SDA dang o muc 0 truoc khi bat dau (chap GND / thieu pull-up / cam bien treo) */
    AM2301_ERR_NO_RESPONSE,  /* khong co xung tra loi sau khi MCU nha bus */
    AM2301_ERR_RESPONSE,     /* xung tra loi sai do dai */
    AM2301_ERR_BIT_TIMEOUT,  /* mat tin hieu giua chung khung 40 bit */
    AM2301_ERR_BIT_TIMING,   /* xung bit sai do dai (nhieu / day dai / pull-up yeu) */
    AM2301_ERR_CHECKSUM,
    AM2301_ERR_RANGE,        /* checksum dung nhung gia tri ngoai -40..80 C / 0..100 %RH */
    AM2301_ERR_ALL_ZERO      /* ca 4 byte = 0 (checksum van "dung") -> khung rong */
} am2301_status_t;

typedef struct {
    int16_t temp_x10;        /* 0.1 C */
    uint16_t rh_x10;         /* 0.1 %RH */
    bool    temp_twos_comp;  /* true: cam bien (hang nhai) gui T am dang bu 2 thay vi dau-do lon */
} am2301_data_t;

/* Chan doan tin hieu cua lan doc gan nhat (de kiem tra day noi tren mach that) */
typedef struct {
    uint8_t  raw[5];         /* cac byte da nhan (ke ca khi loi) */
    uint8_t  bits;           /* so bit da nhan duoc (0..40) */
    uint16_t tgo_us;         /* MCU nha bus -> cam bien keo thap */
    uint16_t trel_us;        /* xung tra loi muc thap */
    uint16_t treh_us;        /* xung tra loi muc cao */
    uint16_t tlow_min_us, tlow_max_us;
    uint16_t th0_min_us, th0_max_us;   /* muc cao cua bit "0" */
    uint16_t th1_min_us, th1_max_us;   /* muc cao cua bit "1" */
} am2301_diag_t;

void            am2301_init(void);
am2301_status_t am2301_read(am2301_data_t *out);
const am2301_diag_t *am2301_diag(void);

/* Giai ma 5 byte khung (ham thuan tuy, dung cho self-test va test tren PC) */
am2301_status_t am2301_decode(const uint8_t b[5], am2301_data_t *out);

const char *am2301_status_str(am2301_status_t st);

#endif /* AM2301_H */
