/*
 * main.c - Test RAK811 (WisNode-LoRa) doc cam bien AM2301 (DHT21) tren PB5,
 * in ket qua qua USART1 (CH340 -> USB) 115200 8N1.
 *
 * Phim lenh tren cong COM:
 *   d : bat/tat in chi tiet thoi gian xung moi lan doc (kiem tra chat luong tin hieu)
 *   s : in bang thong ke ngay
 */
#include "board.h"
#include "hal.h"
#include "am2301.h"

#define N_STATUS 10U

static uint32_t s_cnt[N_STATUS];       /* dem theo ma trang thai */
static uint32_t s_total;
static int32_t  s_tmin, s_tmax;
static uint32_t s_hmin, s_hmax;
static bool     s_has_val;
static uint32_t s_consec_err;
static bool     s_verbose;
static bool     s_warned_twos;

/* Gia tri lon nhat / nho nhat cua do dai xung (de danh gia day noi) */
static uint16_t s_th0_min = 0xFFFFU, s_th0_max, s_th1_min = 0xFFFFU, s_th1_max;
static uint16_t s_resp_min = 0xFFFFU, s_resp_max;

static void put_us(uint32_t v)
{
    uart_put_u32(v);
    uart_puts("us");
}

static void put_range(uint16_t mn, uint16_t mx)
{
    if (mn > mx) {
        uart_puts("--");
        return;
    }
    uart_put_u32(mn);
    uart_puts("..");
    put_us(mx);
}

static void print_raw(const am2301_diag_t *d)
{
    uart_puts(" raw=");
    for (uint32_t i = 0; i < 5U; i++) {
        uart_put_hex8(d->raw[i]);
        if (i < 4U) { uart_putc(' '); }
    }
    uart_puts(" (");
    uart_put_u32(d->bits);
    uart_puts("/40 bit)");
}

static void print_timing(const am2301_diag_t *d)
{
    uart_puts("      Tgo=");  put_us(d->tgo_us);
    uart_puts(" Trel="); put_us(d->trel_us);
    uart_puts(" Treh="); put_us(d->treh_us);
    uart_puts(" Tlow="); put_range(d->tlow_min_us, d->tlow_max_us);
    uart_puts(" TH0=");  put_range(d->th0_min_us, d->th0_max_us);
    uart_puts(" TH1=");  put_range(d->th1_min_us, d->th1_max_us);
    uart_puts("\n      (datasheet: Tgo 20-200, Trel/Treh 75-85, Tlow 48-55, TH0 22-30, TH1 68-75)\n");
}

static void print_stats(void)
{
    uint32_t ok = s_cnt[AM2301_OK];
    uart_puts("---- Thong ke: ");
    uart_put_u32(ok);
    uart_puts("/");
    uart_put_u32(s_total);
    uart_puts(" lan doc OK");
    if (s_total > 0U) {
        uart_puts(" (");
        uart_put_u32((ok * 100U + s_total / 2U) / s_total);
        uart_puts("%)");
    }
    if (s_has_val) {
        uart_puts(" | T ");
        uart_put_deci(s_tmin);
        uart_puts("..");
        uart_put_deci(s_tmax);
        uart_puts(" C | RH ");
        uart_put_deci((int32_t)s_hmin);
        uart_puts("..");
        uart_put_deci((int32_t)s_hmax);
        uart_puts(" %");
    }
    uart_puts("\n     Xung: Trel/Treh=");
    put_range(s_resp_min, s_resp_max);
    uart_puts(" TH0=");
    put_range(s_th0_min, s_th0_max);
    uart_puts(" TH1=");
    put_range(s_th1_min, s_th1_max);
    uart_puts("\n");
    for (uint32_t i = 1U; i < N_STATUS; i++) {
        if (s_cnt[i] != 0U) {
            uart_puts("     ");
            uart_put_u32(s_cnt[i]);
            uart_puts(" x ");
            uart_puts(am2301_status_str((am2301_status_t)i));
            uart_puts("\n");
        }
    }
    uart_puts("----\n");
}

/* Tu kiem tra bo giai ma bang cac vi du trong datasheet muc 5.2 */
static bool self_test_decode(void)
{
    static const uint8_t ex1[5] = { 0x02U, 0x92U, 0x01U, 0x0DU, 0xA2U };  /* 65.8 %RH, 26.9 C */
    static const uint8_t ex2[5] = { 0x02U, 0x92U, 0x01U, 0x0DU, 0xB2U };  /* sai checksum */
    static const uint8_t neg[5] = { 0x02U, 0x92U, 0x80U, 0x65U, 0x79U };  /* -10.1 C */
    static const uint8_t tc[5]  = { 0x02U, 0x92U, 0xFFU, 0x9BU, 0x2EU };  /* -10.1 C dang bu 2 */
    am2301_data_t d;
    bool ok = true;

    ok = ok && (am2301_decode(ex1, &d) == AM2301_OK) && d.rh_x10 == 658U && d.temp_x10 == 269;
    ok = ok && (am2301_decode(ex2, &d) == AM2301_ERR_CHECKSUM);
    ok = ok && (am2301_decode(neg, &d) == AM2301_OK) && d.temp_x10 == -101 && !d.temp_twos_comp;
    ok = ok && (am2301_decode(tc, &d) == AM2301_OK) && d.temp_x10 == -101 && d.temp_twos_comp;
    return ok;
}

static void wait_ms_kick(uint32_t ms)
{
    uint32_t t0 = hal_millis();
    while ((hal_millis() - t0) < ms) {
        hal_iwdg_kick();
        int c = uart_getc();
        if (c == 'd' || c == 'D') {
            s_verbose = !s_verbose;
            uart_puts(s_verbose ? "[chi tiet xung: BAT]\n" : "[chi tiet xung: TAT]\n");
        } else if (c == 's' || c == 'S') {
            print_stats();
        } else {
            /* khong co phim / phim khac: bo qua */
        }
        hal_delay_ms(10);
    }
}

static void update_stats(am2301_status_t st, const am2301_data_t *v, const am2301_diag_t *dg)
{
    s_total++;
    if ((uint32_t)st < N_STATUS) { s_cnt[st]++; }
    if (st != AM2301_OK) {
        return;
    }
    if (!s_has_val) {
        s_tmin = s_tmax = v->temp_x10;
        s_hmin = s_hmax = v->rh_x10;
        s_has_val = true;
    }
    if (v->temp_x10 < s_tmin) { s_tmin = v->temp_x10; }
    if (v->temp_x10 > s_tmax) { s_tmax = v->temp_x10; }
    if (v->rh_x10 < s_hmin)   { s_hmin = v->rh_x10; }
    if (v->rh_x10 > s_hmax)   { s_hmax = v->rh_x10; }

    if (dg->th0_min_us < s_th0_min) { s_th0_min = dg->th0_min_us; }
    if (dg->th0_max_us > s_th0_max && dg->th0_max_us != 0U) { s_th0_max = dg->th0_max_us; }
    if (dg->th1_min_us < s_th1_min) { s_th1_min = dg->th1_min_us; }
    if (dg->th1_max_us > s_th1_max && dg->th1_max_us != 0U) { s_th1_max = dg->th1_max_us; }
    uint16_t r1 = dg->trel_us < dg->treh_us ? dg->trel_us : dg->treh_us;
    uint16_t r2 = dg->trel_us > dg->treh_us ? dg->trel_us : dg->treh_us;
    if (r1 < s_resp_min) { s_resp_min = r1; }
    if (r2 > s_resp_max) { s_resp_max = r2; }
}

int main(void)
{
    hal_clock_init();
    uart_init(LOG_BAUDRATE);
    const char *rst = hal_reset_cause();
    hal_iwdg_start();

    uart_puts("\n\n=== Test RAK811 (WisNode-LoRa) + AM2301 (DHT21) ===\n");
    uart_puts("Firmware dung chung RAK811(H) va RAK811(L)\n");
    uart_puts("Reset do  : ");
    uart_puts(rst);
    uart_puts("\nChan SDA  : " AM_PIN_NAME ", can pull-up 4.7k-5.1k len 3.3V\n");
    uart_puts("Self-test giai ma (vi du datasheet): ");
    uart_puts(self_test_decode() ? "OK\n" : "FAIL\n");
    uart_puts("Chu ky doc: ");
    uart_put_u32(MEASURE_PERIOD_MS / 1000U);
    uart_puts(" s | phim: d = chi tiet xung, s = thong ke\n");

    am2301_init();
    uart_puts("Cho cam bien on dinh sau cap nguon...\n");
    wait_ms_kick(POWERUP_WAIT_MS);

    bool first = true;
    for (;;) {
        uint32_t t0 = hal_millis();
        am2301_data_t v = { 0, 0U, false };

        hal_iwdg_kick();
        am2301_status_t st = am2301_read(&v);
        const am2301_diag_t *dg = am2301_diag();

        uart_puts("[");
        uart_put_u32(t0 / 1000U);
        uart_puts(" s] AM2301: ");
        if (st == AM2301_OK) {
            uart_puts("T=");
            uart_put_deci(v.temp_x10);
            uart_puts(" C, RH=");
            uart_put_deci((int32_t)v.rh_x10);
            uart_puts(" %RH");
            if (first) {
                uart_puts("  (lan dau: gia tri lan do truoc -> bo qua, khong tinh thong ke)");
            }
            if (v.temp_twos_comp && !s_warned_twos) {
                uart_puts("  [cam bien gui T am dang bu 2, da tu xu ly]");
                s_warned_twos = true;
            }
            if (s_consec_err > 0U) {
                uart_puts("  [hoi phuc sau ");
                uart_put_u32(s_consec_err);
                uart_puts(" lan loi]");
            }
            s_consec_err = 0U;
        } else {
            uart_puts(am2301_status_str(st));
            print_raw(dg);
            s_consec_err++;
        }
        uart_puts("\n");
        if (s_verbose || (st != AM2301_OK && st != AM2301_ERR_TOO_SOON && st != AM2301_ERR_BUS_LOW)) {
            print_timing(dg);
        }

        if (!first) {
            update_stats(st, &v, dg);
            if ((s_total % STATS_EVERY_N) == 0U) {
                print_stats();
            }
        }
        first = false;

        /* Cho het chu ky (co kick watchdog + doc phim) */
        uint32_t used = hal_millis() - t0;
        wait_ms_kick((used < MEASURE_PERIOD_MS) ? (MEASURE_PERIOD_MS - used) : 0U);
    }
}
