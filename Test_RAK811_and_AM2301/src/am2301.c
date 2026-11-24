/*
 * am2301.c - Driver AM2301 (DHT21). Do do dai tung xung bang bo dem tick (khong lay mau
 * tai thoi diem co dinh), nen ngat SysTick chen vao chi lam lech vai us -> khong can tat ngat.
 *
 * Trinh tu 1 lan doc (datasheet muc 5.3, 5.4, hinh 8-10):
 *   MCU keo thap Tbe (2 ms) -> nha bus -> cam bien keo thap Trel (80 us) -> cao Treh (80 us)
 *   -> 40 bit: thap 50 us + cao 26 us ("0") hoac 70 us ("1") -> cam bien keo thap Ten (50 us) roi nha.
 */
#include "am2301.h"
#include "am2301_port.h"

#define TPU               AM_TICKS_PER_US
#define AMBIG_LO_US       38U   /* muc cao trong (38, 58) us: khong ro la bit 0 hay 1 -> loi */
#define AMBIG_HI_US       58U

static am2301_diag_t s_diag;
static uint32_t      s_last_ms;
static bool          s_has_last;

/* Cho den khi SDA KHAC muc 'level'. Tra ve true + so us da cho; false neu het thoi gian. */
static bool wait_while(bool level, uint32_t timeout_us, uint32_t *elapsed_us)
{
    uint32_t t0  = am_port_ticks();
    uint32_t lim = timeout_us * TPU;
    for (;;) {
        bool     v  = am_port_read();
        uint32_t dt = am_port_ticks() - t0;
        if (v != level) {
            *elapsed_us = dt / TPU;
            return true;
        }
        if (dt > lim) {
            *elapsed_us = dt / TPU;
            return false;
        }
    }
}

static void busy_wait_us(uint32_t us)
{
    uint32_t t0 = am_port_ticks();
    while ((am_port_ticks() - t0) < (us * TPU)) { }
}

static uint16_t u16_clip(uint32_t v)
{
    return (v > 0xFFFFU) ? 0xFFFFU : (uint16_t)v;
}

static void diag_minmax(uint16_t *mn, uint16_t *mx, uint32_t v)
{
    uint16_t x = u16_clip(v);
    if (x < *mn) { *mn = x; }
    if (x > *mx) { *mx = x; }
}

void am2301_init(void)
{
    am_port_init();
    s_has_last = false;
}

const am2301_diag_t *am2301_diag(void)
{
    return &s_diag;
}

am2301_status_t am2301_decode(const uint8_t b[5], am2301_data_t *out)
{
    uint8_t sum = (uint8_t)(b[0] + b[1] + b[2] + b[3]);
    if (sum != b[4]) {
        return AM2301_ERR_CHECKSUM;
    }
    if ((b[0] | b[1] | b[2] | b[3]) == 0U) {
        return AM2301_ERR_ALL_ZERO;
    }

    uint16_t rh   = (uint16_t)(((uint16_t)b[0] << 8) | b[1]);
    uint16_t traw = (uint16_t)(((uint16_t)b[2] << 8) | b[3]);
    int32_t  t;
    bool     twos = false;

    if ((traw & 0x8000U) != 0U) {
        uint16_t mag = traw & 0x7FFFU;
        if (mag <= 400U) {
            t = -(int32_t)mag;                    /* dau-do lon theo datasheet */
        } else {
            t = (int32_t)traw - 65536;            /* bu 2 (mot so ban nhai DHT21) */
            twos = true;
        }
    } else {
        t = (int32_t)traw;
    }

    if (t < -400 || t > 800 || rh > 1000U) {
        return AM2301_ERR_RANGE;
    }
    out->temp_x10       = (int16_t)t;
    out->rh_x10         = rh;
    out->temp_twos_comp = twos;
    return AM2301_OK;
}

am2301_status_t am2301_read(am2301_data_t *out)
{
    uint32_t us;
    uint32_t now = am_port_millis();

    if (s_has_last && (now - s_last_ms) < AM_MIN_INTERVAL_MS) {
        return AM2301_ERR_TOO_SOON;
    }

    for (uint32_t i = 0; i < 5U; i++) { s_diag.raw[i] = 0U; }
    s_diag.bits = 0U;
    s_diag.tgo_us = 0U; s_diag.trel_us = 0U; s_diag.treh_us = 0U;
    s_diag.tlow_min_us = 0xFFFFU; s_diag.tlow_max_us = 0U;
    s_diag.th0_min_us  = 0xFFFFU; s_diag.th0_max_us  = 0U;
    s_diag.th1_min_us  = 0xFFFFU; s_diag.th1_max_us  = 0U;

    /* Bus phai dang o muc 1 (dien tro keo len) */
    am_port_release();
    if (wait_while(true, 20U, &us)) {
        return AM2301_ERR_BUS_LOW;
    }

    /* Tinh moc 2 s ke tu lan gui tin hieu bat dau (du co thanh cong hay khong,
     * cam bien da bi danh thuc va bat dau 1 lan do moi) */
    s_last_ms  = now;
    s_has_last = true;

    /* Tin hieu bat dau */
    am_port_drive_low();
    busy_wait_us(AM_TBE_US);
    am_port_release();

    /* Tgo: cam bien keo thap trong 20..200 us */
    if (!wait_while(true, AM_TGO_MAX_US + 20U, &us)) {
        return AM2301_ERR_NO_RESPONSE;
    }
    s_diag.tgo_us = u16_clip(us);

    /* Xung tra loi: thap Trel, cao Treh */
    if (!wait_while(false, AM_TRESP_MAX_US, &us)) {
        s_diag.trel_us = u16_clip(us);
        return AM2301_ERR_RESPONSE;
    }
    s_diag.trel_us = u16_clip(us);
    if (us < AM_TRESP_MIN_US) {
        return AM2301_ERR_RESPONSE;
    }
    if (!wait_while(true, AM_TRESP_MAX_US, &us)) {
        s_diag.treh_us = u16_clip(us);
        return AM2301_ERR_RESPONSE;
    }
    s_diag.treh_us = u16_clip(us);
    if (us < AM_TRESP_MIN_US) {
        return AM2301_ERR_RESPONSE;
    }

    /* 40 bit du lieu, MSB truoc */
    for (uint32_t i = 0; i < 40U; i++) {
        uint32_t tl, th;
        if (!wait_while(false, AM_TLOW_MAX_US, &tl)) {
            return AM2301_ERR_BIT_TIMEOUT;
        }
        if (!wait_while(true, AM_TH_MAX_US, &th)) {
            return AM2301_ERR_BIT_TIMEOUT;
        }
        diag_minmax(&s_diag.tlow_min_us, &s_diag.tlow_max_us, tl);
        if (tl < AM_TLOW_MIN_US || th < AM_TH_MIN_US || (th > AMBIG_LO_US && th < AMBIG_HI_US)) {
            return AM2301_ERR_BIT_TIMING;
        }
        if (th > AM_TH_THRESHOLD_US) {
            s_diag.raw[i >> 3] |= (uint8_t)(0x80U >> (i & 7U));
            diag_minmax(&s_diag.th1_min_us, &s_diag.th1_max_us, th);
        } else {
            diag_minmax(&s_diag.th0_min_us, &s_diag.th0_max_us, th);
        }
        s_diag.bits = (uint8_t)(i + 1U);
    }

    /* Ten: cam bien giu thap ~50 us roi nha bus (khong bat buoc phai thay) */
    (void)wait_while(false, 100U, &us);

    return am2301_decode(s_diag.raw, out);
}

const char *am2301_status_str(am2301_status_t st)
{
    switch (st) {
    case AM2301_OK:              return "OK";
    case AM2301_ERR_TOO_SOON:    return "LOI: doc lai chua du 2 s";
    case AM2301_ERR_BUS_LOW:     return "LOI: SDA bi keo thap (chap GND / dau nguoc day / cam bien treo)";
    case AM2301_ERR_NO_RESPONSE: return "LOI: khong co tra loi (kiem tra day, nguon, pull-up 4.7k-5.1k)";
    case AM2301_ERR_RESPONSE:    return "LOI: xung tra loi sai do dai";
    case AM2301_ERR_BIT_TIMEOUT: return "LOI: mat tin hieu giua khung du lieu";
    case AM2301_ERR_BIT_TIMING:  return "LOI: xung bit sai do dai (nhieu / day dai / pull-up yeu)";
    case AM2301_ERR_CHECKSUM:    return "LOI: sai checksum";
    case AM2301_ERR_RANGE:       return "LOI: gia tri ngoai dai -40..80 C / 0..100 %RH";
    case AM2301_ERR_ALL_ZERO:    return "LOI: khung du lieu toan 0";
    default:                     return "LOI: khong ro";
    }
}
