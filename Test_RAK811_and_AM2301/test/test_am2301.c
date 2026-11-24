/*
 * test_am2301.c - Kiem thu driver AM2301 tren PC (khong can mach).
 *
 * Bien dich CUNG file src/am2301.c voi lop port gia lap: thoi gian ao 16 tick/us, moi lan
 * driver doc chan / doc bo dem ton mot it thoi gian (giong CPU that), thinh thoang co
 * ngat SysTick chen vao. Mo hinh cam bien tao dang song dung Bang 7 datasheet
 * (hoac co tinh sai de thu loi), va kiem tra ca phia MCU (xung bat dau, tranh chap bus).
 *
 * Chay: gcc -std=gnu11 -O1 -Wall -Wextra -DAM2301_HOST_TEST -Isrc test/test_am2301.c src/am2301.c -o test_am2301 && ./test_am2301
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "am2301.h"
#include "am2301_port.h"

/* ------------------------------------------------------------------ thoi gian ao */
static uint64_t vt;                 /* tick ao (16 / us) */
static uint64_t next_isr;           /* thoi diem ngat SysTick ke tiep */
static uint32_t poll_cost = 6;      /* tick ton moi lan goi port (~0.4 us) */
static uint32_t isr_cost  = 40;     /* ngat SysTick ~2.5 us */
static uint32_t rng_state = 1;

static uint32_t rnd(void)
{
    rng_state = rng_state * 1103515245U + 12345U;
    return (rng_state >> 8) & 0xFFFFFFU;
}
static uint32_t rnd_range(uint32_t lo, uint32_t hi) { return lo + rnd() % (hi - lo + 1U); }

static void advance(uint32_t t)
{
    vt += t + (rnd() & 1U);
    if (vt >= next_isr) {           /* ngat SysTick moi 1 ms (sau buoc nhay thoi gian: tinh lai moc) */
        vt += isr_cost;
        next_isr = (vt - next_isr > 16000U) ? vt + 16000U : next_isr + 16000U;
    }
}

/* ------------------------------------------------------------------ mo hinh cam bien */
typedef struct {
    bool     present;
    bool     stuck_low;
    int      truncate_bits;         /* >=0: ngung gui sau N bit (bus o muc 1) */
    bool     random_timing;         /* moi xung lay ngau nhien trong [min, max] datasheet */
    uint32_t tgo, trel, treh, tlow, th0, th1, ten;   /* us, dung khi random_timing = false */
} sensor_cfg_t;

#define MAX_SEG 100
static sensor_cfg_t cfg;
static uint8_t      frame[5];
static uint64_t     seg_t[MAX_SEG]; /* moc thoi gian cam bien doi muc keo */
static bool         seg_low[MAX_SEG];
static int          nseg;
static bool         host_low;
static uint64_t     host_fall;
static uint64_t     last_start_len; /* do dai xung bat dau cua MCU (tick) */
static int          n_starts;
static int          contention;     /* so lan MCU keo thap khi cam bien dang gui */
static uint64_t     frame_end;

static uint32_t pick(uint32_t fixed, uint32_t lo, uint32_t hi)
{
    return cfg.random_timing ? rnd_range(lo, hi) : fixed;
}

static void add_seg(uint64_t *t, bool low, uint32_t us)
{
    seg_t[nseg] = *t;
    seg_low[nseg] = low;
    nseg++;
    *t += (uint64_t)us * 16U + rnd_range(0, 15);
}

static void sensor_start(uint64_t t)
{
    nseg = 0;
    add_seg(&t, false, pick(cfg.tgo, 20, 200));
    add_seg(&t, true,  pick(cfg.trel, 75, 85));
    add_seg(&t, false, pick(cfg.treh, 75, 85));
    for (int i = 0; i < 40; i++) {
        if (cfg.truncate_bits >= 0 && i >= cfg.truncate_bits) { break; }
        bool one = (frame[i / 8] & (0x80U >> (i % 8))) != 0U;
        add_seg(&t, true,  pick(cfg.tlow, 48, 55));
        add_seg(&t, false, one ? pick(cfg.th1, 68, 75) : pick(cfg.th0, 22, 30));
    }
    if (cfg.truncate_bits < 0) {
        add_seg(&t, true, pick(cfg.ten, 45, 55));
    }
    seg_t[nseg] = t; seg_low[nseg] = false; nseg++;   /* nha bus */
    frame_end = t;
}

static bool sensor_low_at(uint64_t t)
{
    bool low = false;
    for (int i = 0; i < nseg; i++) {
        if (seg_t[i] <= t) { low = seg_low[i]; } else { break; }
    }
    return low;
}

/* ------------------------------------------------------------------ port gia lap */
void am_port_init(void) { host_low = false; }

void am_port_drive_low(void)
{
    advance(poll_cost);
    if (vt < frame_end) { contention++; }
    host_low  = true;
    host_fall = vt;
}

void am_port_release(void)
{
    advance(poll_cost);
    if (host_low) {
        host_low = false;
        last_start_len = vt - host_fall;
        n_starts++;
        /* Cam bien chi tra loi khi xung bat dau 0.8..20 ms */
        if (cfg.present && !cfg.stuck_low && last_start_len >= 800U * 16U && last_start_len <= 20000U * 16U) {
            sensor_start(vt);
        }
    }
}

bool am_port_read(void)
{
    advance(poll_cost);
    return !(host_low || cfg.stuck_low || sensor_low_at(vt));
}

uint32_t am_port_ticks(void)
{
    advance(poll_cost);
    return (uint32_t)vt;
}

uint32_t am_port_millis(void) { return (uint32_t)(vt / 16000U); }

/* ------------------------------------------------------------------ tien ich test */
static int n_pass, n_fail;

#define CHECK(cond, ...) do { if (cond) { n_pass++; } else { n_fail++; \
    printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void make_frame(uint16_t rh, uint16_t traw)
{
    frame[0] = (uint8_t)(rh >> 8); frame[1] = (uint8_t)rh;
    frame[2] = (uint8_t)(traw >> 8); frame[3] = (uint8_t)traw;
    frame[4] = (uint8_t)(frame[0] + frame[1] + frame[2] + frame[3]);
}

static uint16_t enc_t(int t) { return (t < 0) ? (uint16_t)(0x8000U | (uint16_t)(-t)) : (uint16_t)t; }

static void cfg_default(void)
{
    memset(&cfg, 0, sizeof cfg);
    cfg.present = true;
    cfg.truncate_bits = -1;
    cfg.tgo = 30; cfg.trel = 80; cfg.treh = 80; cfg.tlow = 50; cfg.th0 = 26; cfg.th1 = 70; cfg.ten = 50;
    poll_cost = 6;
    isr_cost = 40;
}

static am2301_status_t do_read(am2301_data_t *d)
{
    vt += 2100U * 16000U;          /* > 2 s tu lan doc truoc */
    nseg = 0; frame_end = 0;
    return am2301_read(d);
}

/* ------------------------------------------------------------------ cac test */
static void test_decode_datasheet(void)
{
    printf("[1] Giai ma theo vi du datasheet muc 5.2\n");
    am2301_data_t d;
    const uint8_t ex1[5] = { 0x02, 0x92, 0x01, 0x0D, 0xA2 };
    const uint8_t ex2[5] = { 0x02, 0x92, 0x01, 0x0D, 0xB2 };
    CHECK(am2301_decode(ex1, &d) == AM2301_OK && d.rh_x10 == 658 && d.temp_x10 == 269, "vi du 1: 65.8%%, 26.9C");
    CHECK(am2301_decode(ex2, &d) == AM2301_ERR_CHECKSUM, "vi du 2: phai bao sai checksum");
    make_frame(658, 0x8065);
    CHECK(am2301_decode(frame, &d) == AM2301_OK && d.temp_x10 == -101, "-10.1C = 0x8065");
}

static void test_decode_all_values(void)
{
    printf("[2] Giai ma moi gia tri T -40.0..80.0 (dau-do lon + bu 2), RH 0..100.0\n");
    am2301_data_t d;
    int bad = 0;
    for (int t = -400; t <= 800; t++) {
        uint16_t rh = (uint16_t)((t + 400) % 1001);
        make_frame(rh, enc_t(t));
        if (am2301_decode(frame, &d) != AM2301_OK || d.temp_x10 != t || d.rh_x10 != rh || d.temp_twos_comp) { bad++; }
        if (t < 0) {
            make_frame(rh, (uint16_t)(int16_t)t);
            if (am2301_decode(frame, &d) != AM2301_OK || d.temp_x10 != t || !d.temp_twos_comp) { bad++; }
        }
    }
    CHECK(bad == 0, "%d gia tri giai ma sai", bad);
    make_frame(500, 801);           CHECK(am2301_decode(frame, &d) == AM2301_ERR_RANGE, "80.1C -> RANGE");
    make_frame(500, 0x8000U | 401); CHECK(am2301_decode(frame, &d) == AM2301_ERR_RANGE, "-40.1C -> RANGE");
    make_frame(1001, 250);          CHECK(am2301_decode(frame, &d) == AM2301_ERR_RANGE, "100.1%% -> RANGE");
    make_frame(0, 0);               CHECK(am2301_decode(frame, &d) == AM2301_ERR_ALL_ZERO, "toan 0 -> ALL_ZERO");
    make_frame(0, 0x8000U);         CHECK(am2301_decode(frame, &d) == AM2301_OK && d.temp_x10 == 0, "-0.0C -> 0");
    const uint8_t ff[5] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    CHECK(am2301_decode(ff, &d) == AM2301_ERR_CHECKSUM, "toan 0xFF -> CHECKSUM");
}

static void test_nominal(void)
{
    printf("[3] Doc 1 khung timing dien hinh (65.8%%, 26.9C)\n");
    cfg_default();
    am2301_data_t d;
    make_frame(658, 269);
    am2301_status_t st = do_read(&d);
    const am2301_diag_t *g = am2301_diag();
    CHECK(st == AM2301_OK && d.temp_x10 == 269 && d.rh_x10 == 658, "st=%d T=%d RH=%u", st, d.temp_x10, d.rh_x10);
    CHECK(last_start_len >= 1900U * 16U && last_start_len <= 2200U * 16U, "xung bat dau %llu us (can ~2 ms, 0.8..20)",
          (unsigned long long)(last_start_len / 16U));
    CHECK(contention == 0, "MCU keo bus khi cam bien dang gui");
    CHECK(g->trel_us >= 76 && g->trel_us <= 86 && g->treh_us >= 76 && g->treh_us <= 86, "Trel=%u Treh=%u", g->trel_us, g->treh_us);
    CHECK(g->th0_min_us >= 22 && g->th0_max_us <= 32 && g->th1_min_us >= 66 && g->th1_max_us <= 76,
          "TH0 %u..%u TH1 %u..%u", g->th0_min_us, g->th0_max_us, g->th1_min_us, g->th1_max_us);
    printf("    do duoc: Tgo=%u Trel=%u Treh=%u Tlow=%u..%u TH0=%u..%u TH1=%u..%u us\n", g->tgo_us, g->trel_us,
           g->treh_us, g->tlow_min_us, g->tlow_max_us, g->th0_min_us, g->th0_max_us, g->th1_min_us, g->th1_max_us);
}

static void test_random(int n, uint32_t cost, const char *name)
{
    printf("[4] %d khung ngau nhien, timing ngau nhien trong dai datasheet, %s\n", n, name);
    cfg_default();
    cfg.random_timing = true;
    poll_cost = cost;
    int bad = 0;
    for (int i = 0; i < n; i++) {
        int t = (int)rnd_range(0, 1200) - 400;
        uint16_t rh = (uint16_t)rnd_range(0, 1000);
        if (t == 0 && rh == 0) { rh = 1; }
        make_frame(rh, enc_t(t));
        am2301_data_t d = { 0, 0, false };
        am2301_status_t st = do_read(&d);
        if (st != AM2301_OK || d.temp_x10 != t || d.rh_x10 != rh) {
            if (bad < 5) { printf("    khung %d: st=%d T=%d/%d RH=%u/%u\n", i, st, d.temp_x10, t, d.rh_x10, rh); }
            bad++;
        }
    }
    CHECK(bad == 0, "%d/%d khung sai", bad, n);
    CHECK(contention == 0, "tranh chap bus %d lan", contention);
}

static void test_corners(void)
{
    printf("[5] Goc timing: tat ca min / tat ca max datasheet\n");
    am2301_data_t d;
    cfg_default();
    cfg.tgo = 20; cfg.trel = 75; cfg.treh = 75; cfg.tlow = 48; cfg.th0 = 22; cfg.th1 = 68; cfg.ten = 45;
    make_frame(999, 0x8190);   /* 99.9%, -40.0C */
    CHECK(do_read(&d) == AM2301_OK && d.temp_x10 == -400 && d.rh_x10 == 999, "tat ca min");
    cfg.tgo = 200; cfg.trel = 85; cfg.treh = 85; cfg.tlow = 55; cfg.th0 = 30; cfg.th1 = 75; cfg.ten = 55;
    make_frame(1, 800);        /* 0.1%, 80.0C */
    CHECK(do_read(&d) == AM2301_OK && d.temp_x10 == 800 && d.rh_x10 == 1, "tat ca max");
    cfg_default();
    poll_cost = 48;            /* CPU cham ~3 us moi lan doc chan */
    isr_cost = 160;            /* ngat 10 us */
    make_frame(455, enc_t(-185));
    CHECK(do_read(&d) == AM2301_OK && d.temp_x10 == -185, "CPU cham + ngat dai 10 us");
}

static void test_faults(void)
{
    printf("[6] Loi phan cung / tin hieu\n");
    am2301_data_t d;

    cfg_default(); cfg.present = false; make_frame(500, 250);
    CHECK(do_read(&d) == AM2301_ERR_NO_RESPONSE, "khong cam bien -> NO_RESPONSE");
    CHECK(n_starts > 0 && last_start_len >= 800U * 16U, "van gui xung bat dau");

    cfg_default(); cfg.stuck_low = true;
    int before = n_starts;
    CHECK(do_read(&d) == AM2301_ERR_BUS_LOW, "SDA chap GND -> BUS_LOW");
    CHECK(n_starts == before, "khong gui xung bat dau khi bus dang thap");

    cfg_default(); make_frame(500, 250); frame[4] ^= 0x01;
    CHECK(do_read(&d) == AM2301_ERR_CHECKSUM, "sai checksum");

    static const int cuts[] = { 0, 1, 7, 8, 20, 31, 39 };
    for (size_t i = 0; i < sizeof cuts / sizeof cuts[0]; i++) {
        cfg_default(); cfg.truncate_bits = cuts[i]; make_frame(500, 250);
        am2301_status_t st = do_read(&d);
        /* bit cuoi cung da gui khong co suon xuong ket thuc -> chua duoc tinh;
         * cat ngay sau xung tra loi -> Treh khong ket thuc -> RESPONSE */
        if (cuts[i] == 0) {
            CHECK(st == AM2301_ERR_RESPONSE, "cat sau 0 bit -> st=%d", st);
        } else {
            CHECK(st == AM2301_ERR_BIT_TIMEOUT && am2301_diag()->bits == cuts[i] - 1,
                  "cat sau %d bit -> st=%d bits=%u", cuts[i], st, am2301_diag()->bits);
        }
    }

    cfg_default(); cfg.th1 = 45; make_frame(500, 250);
    CHECK(do_read(&d) == AM2301_ERR_BIT_TIMING, "bit cao 45 us (mo ho) -> BIT_TIMING");
    cfg_default(); cfg.th0 = 5; make_frame(500, 250);
    CHECK(do_read(&d) == AM2301_ERR_BIT_TIMING, "gai 5 us -> BIT_TIMING");
    cfg_default(); cfg.trel = 30; make_frame(500, 250);
    CHECK(do_read(&d) == AM2301_ERR_RESPONSE, "Trel 30 us -> RESPONSE");
    cfg_default(); cfg.treh = 150; make_frame(500, 250);
    CHECK(do_read(&d) == AM2301_ERR_RESPONSE, "Treh 150 us -> RESPONSE");
    cfg_default(); cfg.tgo = 260; make_frame(500, 250);
    CHECK(do_read(&d) == AM2301_ERR_NO_RESPONSE, "Tgo 260 us -> NO_RESPONSE");
    cfg_default(); make_frame(0, 0);
    CHECK(do_read(&d) == AM2301_ERR_ALL_ZERO, "khung toan 0");
    cfg_default(); make_frame(500, 900);
    CHECK(do_read(&d) == AM2301_ERR_RANGE, "90.0C -> RANGE");

    /* Hoi phuc sau loi */
    cfg_default(); make_frame(612, enc_t(-52));
    CHECK(do_read(&d) == AM2301_OK && d.temp_x10 == -52 && d.rh_x10 == 612, "hoi phuc sau chuoi loi");
}

static void test_interval(void)
{
    printf("[7] Khoang cach doc toi thieu 2 s\n");
    am2301_data_t d;
    cfg_default(); make_frame(500, 250);
    CHECK(do_read(&d) == AM2301_OK, "lan 1");
    int before = n_starts;
    vt += 1500U * 16000U;
    CHECK(am2301_read(&d) == AM2301_ERR_TOO_SOON, "sau 1.5 s -> TOO_SOON");
    CHECK(n_starts == before, "khong danh thuc cam bien khi chua du 2 s");
    vt += 600U * 16000U;
    nseg = 0; frame_end = 0;
    CHECK(am2301_read(&d) == AM2301_OK, "sau 2.1 s -> OK");
}

int main(void)
{
    am2301_init();
    vt = 16000U * 3000U;
    next_isr = vt + 16000U;
    test_decode_datasheet();
    test_decode_all_values();
    test_nominal();
    test_random(5000, 6, "CPU 16 MHz");
    rng_state = 777;
    test_random(2000, 24, "CPU cham (vong lap 3 us)");
    rng_state = 4242;
    test_random(2000, 40, "CPU rat cham (vong lap 5 us, gap 3 lan ban that)");
    test_corners();
    test_faults();
    test_interval();
    printf("\nKET QUA: %d DAT, %d KHONG DAT\n", n_pass, n_fail);
    return n_fail == 0 ? 0 : 1;
}
