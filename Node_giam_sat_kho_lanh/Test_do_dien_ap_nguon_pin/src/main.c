/*
 * main.c - Test do dien ap nguon 3V3 va dien ap pin / 5V tren RAK811 (WisNode-LoRa).
 * Log qua USART1 (CH340 -> USB) 115200 8N1. Phim lenh:
 *   t = bai test tu dong (3V3 trong dai, gon song, mach chia Vin), ket luan DAT / KHONG DAT
 *   i = do chi tiet (ma ADC, hieu chuan, min/max phien), e = nhat ky su kien
 *   c = xoa min/max + nhat ky, h = tro giup
 * Firmware do lien tuc moi 250 ms va GHI NHO su kien (doi nguon, sut ap) vao RAM,
 * nen co the rut USB chay bang pin roi cam lai xem bang phim 'e'.
 */
#include "board.h"
#include "hal.h"
#include "supply.h"

/* ---------------- In an ---------------- */

static void put_2d(uint32_t v)
{
    uart_putc((char)('0' + (v / 10U) % 10U));
    uart_putc((char)('0' + v % 10U));
}

/* hh:mm:ss.d */
static void put_hms(uint32_t ms)
{
    uint32_t s = ms / 1000U;
    put_2d(s / 3600U);
    uart_putc(':');
    put_2d((s / 60U) % 60U);
    uart_putc(':');
    put_2d(s % 60U);
    uart_putc('.');
    uart_putc((char)('0' + (ms / 100U) % 10U));
}

static void put_ts_at(uint32_t ms)
{
    uart_putc('[');
    put_hms(ms);
    uart_puts("] ");
}

static void put_ts(void)
{
    put_ts_at(hal_millis());
}

/* 3312 mV -> "3.312 V" */
static void put_volt(uint32_t mv)
{
    uart_put_u32(mv / 1000U);
    uart_putc('.');
    uart_putc((char)('0' + (mv / 100U) % 10U));
    uart_putc((char)('0' + (mv / 10U) % 10U));
    uart_putc((char)('0' + mv % 10U));
    uart_puts(" V");
}

static void print_help(void)
{
    uart_puts("Lenh: t=test tu dong  i=do chi tiet  e=nhat ky su kien  c=xoa min/max  h=tro giup\n");
}

/* ---------------- Danh gia 3V3 ---------------- */

typedef enum { VDD_ST_OK = 0, VDD_ST_DEV, VDD_ST_LOW, VDD_ST_HIGH } vdd_state_t;

static vdd_state_t vdd_eval(uint16_t mv)
{
    if (mv > VDD_ABS_MAX_MV) { return VDD_ST_HIGH; }
    if (mv < VDD_LOW_MV)     { return VDD_ST_LOW; }
    if ((mv < VDD_OK_MIN_MV) || (mv > VDD_OK_MAX_MV)) { return VDD_ST_DEV; }
    return VDD_ST_OK;
}

static const char *vdd_state_name(vdd_state_t s)
{
    switch (s) {
    case VDD_ST_OK:   return "TOT";
    case VDD_ST_DEV:  return "LECH (van chay duoc)";
    case VDD_ST_LOW:  return "THAP - LoRa de reset/mat goi";
    default:          return "QUA AP - vuot 3.6 V cua RAK811!";
    }
}

/* ---------------- Thong ke phien + nhat ky su kien ---------------- */

static uint16_t ses_vdd_min = 0xFFFFU, ses_vdd_max;
static uint16_t ses_vin_min = 0xFFFFU, ses_vin_max;
static uint32_t ses_meas_count, ses_adc_err;

typedef enum {
    EV_BOOT = 0, EV_SRC, EV_VDD_STATE, EV_VDD_STEP, EV_ADC_ERR
} ev_code_t;

typedef struct {
    uint32_t t_ms;
    uint16_t vdd_mv;
    uint16_t vin_mv;
    uint8_t  code;
    uint8_t  arg;          /* EV_SRC: supply_source_t ; EV_VDD_STATE: vdd_state_t */
} event_t;

#define EVENT_MAX 32U
static event_t  s_ev[EVENT_MAX];
static uint32_t s_ev_count;         /* tong so su kien tu luc xoa (vong tron) */

static void event_print(const event_t *e)
{
    put_ts_at(e->t_ms);
    switch ((ev_code_t)e->code) {
    case EV_BOOT:      uart_puts("KHOI DONG"); break;
    case EV_SRC:       uart_puts("DOI NGUON -> "); uart_puts(supply_source_name((supply_source_t)e->arg)); break;
    case EV_VDD_STATE: uart_puts("3V3 -> "); uart_puts(vdd_state_name((vdd_state_t)e->arg)); break;
    case EV_VDD_STEP:  uart_puts("3V3 THAY DOI DOT NGOT"); break;
    default:           uart_puts("LOI ADC"); break;
    }
    uart_puts(" | 3V3 = ");
    put_volt(e->vdd_mv);
    if (e->vin_mv != 0U) {
        uart_puts(" | Vin = ");
        put_volt(e->vin_mv);
    }
    uart_puts("\n");
}

static void event_add(ev_code_t code, uint8_t arg, const supply_meas_t *m)
{
    event_t *e = &s_ev[s_ev_count % EVENT_MAX];
    e->t_ms   = hal_millis();
    e->code   = (uint8_t)code;
    e->arg    = arg;
    e->vdd_mv = m->vdd_mv;
    e->vin_mv = (supply_classify(m) != SRC_UNKNOWN) ? m->vin_mv : 0U;   /* PA2 bo ho: khong ghi so rac */
    s_ev_count++;
    uart_puts(">>> ");
    event_print(e);
}

static void events_dump(void)
{
    uint32_t first = (s_ev_count > EVENT_MAX) ? (s_ev_count - EVENT_MAX) : 0U;
    uart_puts("--- Nhat ky su kien (");
    uart_put_u32(s_ev_count);
    uart_puts(" su kien");
    if (first != 0U) {
        uart_puts(", chi giu ");
        uart_put_u32(EVENT_MAX);
        uart_puts(" cai moi nhat");
    }
    uart_puts(") ---\n");
    for (uint32_t i = first; i < s_ev_count; i++) {
        uart_puts("  ");
        event_print(&s_ev[i % EVENT_MAX]);
    }
    uart_puts("--- het ---\n");
}

static void session_update(const supply_meas_t *m)
{
    ses_meas_count++;
    if (m->vdd_min_mv < ses_vdd_min) { ses_vdd_min = m->vdd_min_mv; }
    if (m->vdd_max_mv > ses_vdd_max) { ses_vdd_max = m->vdd_max_mv; }
    if (m->vin_mv != 0U) {
        if (m->vin_min_mv < ses_vin_min) { ses_vin_min = m->vin_min_mv; }
        if (m->vin_max_mv > ses_vin_max) { ses_vin_max = m->vin_max_mv; }
    }
}

static void session_clear(void)
{
    ses_vdd_min = 0xFFFFU; ses_vdd_max = 0U;
    ses_vin_min = 0xFFFFU; ses_vin_max = 0U;
    ses_meas_count = 0U;   ses_adc_err = 0U;
    s_ev_count = 0U;
}

/* ---------------- In ket qua do ---------------- */
/* In ly do ADC loi: buoc hong + ADC1->SR (bit 1 EOC, 6 ADONS, 8 RCNR) */
static void print_adc_error(void)
{
    uint32_t sr, val;
    const char *why = supply_last_error(&sr, &val);
    uart_puts("LOI ADC: ");
    uart_puts(why);
    uart_puts(" | SR=0x");
    uart_put_hex8((uint8_t)(sr >> 8));
    uart_put_hex8((uint8_t)sr);
    uart_puts(" | gia tri ");
    uart_put_u32(val);
    uart_puts("\n");
}


static void print_vin_line(const supply_meas_t *m)
{
    supply_source_t src = supply_classify(m);

    if (src == SRC_UNKNOWN) {
        uart_puts("Vin: ");
        uart_puts(supply_source_name(src));
        return;
    }
    uart_puts("Vin = ");
    put_volt(m->vin_mv);
    if (m->vin_saturated) {
        uart_puts(" (CHAM DINH THANG DO - Vin co the cao hon)");
    }
    uart_puts(" -> ");
    uart_puts(supply_source_name(src));
    if (src != SRC_USB_5V) {
        uart_puts(" ~");
        uart_put_u32(supply_liion_percent(m->vin_mv));
        uart_puts("% (uoc tinh Li-ion)");
    }
}

static void print_status(const supply_meas_t *m)
{
    put_ts();
    if (!m->ok) {
        print_adc_error();
        return;
    }
    uart_puts("3V3 = ");
    put_volt(m->vdd_mv);
    uart_puts(" (");
    uart_puts(vdd_state_name(vdd_eval(m->vdd_mv)));
    uart_puts(", gon ");
    uart_put_u32((uint32_t)m->vdd_max_mv - m->vdd_min_mv);
    uart_puts(" mV) | ");
    print_vin_line(m);
    uart_puts("\n");
}

/* LDO sut ap: 3V3 da thap hon dai dat VA Vin khong du cao hon 3V3 mot khoang LDO_DROPOUT_MV
 * (3V3 tut theo Vin). 3V3 con tot thi LDO van on du du dia nho (vd MCP1700 chi can ~0.2 V). */
static bool ldo_dropout(const supply_meas_t *m)
{
    return (m->vdd_mv < VDD_OK_MIN_MV) &&
           ((uint32_t)m->vin_mv < (uint32_t)m->vdd_mv + LDO_DROPOUT_MV);
}

static void print_detail(void)
{
    supply_meas_t m;
    bool cal_ok;
    uint16_t cal = supply_vrefint_cal(&cal_ok);

    uart_puts("\n--- Do chi tiet (256 mau) ---\n");
    if (!supply_measure(&m, 256U)) {
        print_adc_error();
        return;
    }
    uart_puts("  VREFINT_CAL  : ");
    uart_put_u32(cal);
    uart_puts(cal_ok ? " (hieu chuan nha may @3.0 V)\n" : " (O HIEU CHUAN HONG - dung gia tri dien hinh)\n");
    uart_puts("  VREFINT raw  : ");
    uart_put_u32(m.vref_raw);
    uart_puts("\n  3V3 tb       : ");
    put_volt(m.vdd_mv);
    uart_puts("  -> ");
    uart_puts(vdd_state_name(vdd_eval(m.vdd_mv)));
    uart_puts("\n  3V3 min/max  : ");
    put_volt(m.vdd_min_mv);
    uart_puts(" / ");
    put_volt(m.vdd_max_mv);
    uart_puts("  (gon ");
    uart_put_u32((uint32_t)m.vdd_max_mv - m.vdd_min_mv);
    uart_puts(" mV)\n");
    uart_puts("  " VIN_PIN_NAME ": raw ");
    uart_put_u32(m.pa2_raw);
    uart_puts(" = ");
    put_volt(m.pa2_mv);
    uart_puts(" | he so chia (");
    uart_put_u32(VIN_R_TOP_K);
    uart_puts("k+");
    uart_put_u32(VIN_R_BOT_K);
    uart_puts("k)/");
    uart_put_u32(VIN_R_BOT_K);
    uart_puts("k\n  Kiem tra chan: ");
    uart_puts(m.vin_floating ? "PA2 BO HO (so ADC tren la rac, bo qua)\n  "
                             : "PA2 co tai (mach chia + tu)\n  ");
    print_vin_line(&m);
    uart_puts("\n");
    if (supply_classify(&m) != SRC_UNKNOWN) {
        uart_puts("  Vin min/max  : ");
        put_volt(m.vin_min_mv);
        uart_puts(" / ");
        put_volt(m.vin_max_mv);
        uart_puts("\n  Du dia LDO   : Vin - 3V3 = ");
        uart_put_i32((int32_t)m.vin_mv - (int32_t)m.vdd_mv);
        uart_puts(" mV");
        uart_puts(ldo_dropout(&m) ? " (THIEU - LDO dang sut ap, 3V3 tut theo Vin)\n" : "\n");
    }
    uart_puts("  Phien do     : ");
    uart_put_u32(ses_meas_count);
    uart_puts(" lan, loi ADC ");
    uart_put_u32(ses_adc_err);
    if (ses_meas_count != 0U) {
        uart_puts(" | 3V3 min/max ");
        put_volt(ses_vdd_min);
        uart_puts(" / ");
        put_volt(ses_vdd_max);
        if (ses_vin_max != 0U) {
            uart_puts(" | Vin min/max ");
            put_volt(ses_vin_min);
            uart_puts(" / ");
            put_volt(ses_vin_max);
        }
    }
    uart_puts("\n\n");
}

/* ---------------- Bai test tu dong ---------------- */

#define TEST_BURSTS   16U      /* 16 loat x 64 mau, cach nhau 60 ms (~1 s) */
#define TEST_SAMPLES  64U

static void run_test(void)
{
    supply_meas_t m;
    uint32_t sum_vdd = 0U, sum_vin = 0U;
    uint16_t vmin = 0xFFFFU, vmax = 0U;
    uint32_t i, ok_n = 0U, vin_n = 0U;
    bool vin_sat = false;
    bool pass_vdd, pass_ripple, pass_vin, vin_present;
    uint16_t vdd, vin = 0U, ripple;

    uart_puts("\n===== BAT DAU TEST DIEN AP NGUON =====\n");
    uart_puts("Giu nguyen nguon, khong phat LoRa / khong cam rut day trong ~1 s...\n");

    for (i = 0U; i < TEST_BURSTS; i++) {
        hal_iwdg_kick();
        if (supply_measure(&m, TEST_SAMPLES)) {
            ok_n++;
            sum_vdd += m.vdd_mv;
            if (m.vdd_min_mv < vmin) { vmin = m.vdd_min_mv; }
            if (m.vdd_max_mv > vmax) { vmax = m.vdd_max_mv; }
            if (supply_classify(&m) != SRC_UNKNOWN) {
                vin_n++;
                sum_vin += m.vin_mv;
                vin_sat = vin_sat || m.vin_saturated;
            }
        }
        hal_delay_ms(60U);
    }

    if (ok_n != TEST_BURSTS) {
        uart_puts(">>> KHONG DAT: ADC loi ");
        uart_put_u32(TEST_BURSTS - ok_n);
        uart_puts("/");
        uart_put_u32(TEST_BURSTS);
        uart_puts(" lan do\n");
        print_adc_error();
        uart_puts("\n");
        return;
    }
    vdd    = (uint16_t)(sum_vdd / ok_n);
    ripple = (uint16_t)(vmax - vmin);
    vin_present = (vin_n == TEST_BURSTS);       /* phai thay mach chia o MOI loat */
    if (vin_present) {
        vin = (uint16_t)(sum_vin / vin_n);
    }

    /* Buoc 1: 3V3 trong dai */
    pass_vdd = (vdd_eval(vdd) == VDD_ST_OK);
    uart_puts("Buoc 1/3: 3V3 = ");
    put_volt(vdd);
    uart_puts(" (dai dat ");
    put_volt(VDD_OK_MIN_MV);
    uart_puts(" .. ");
    put_volt(VDD_OK_MAX_MV);
    uart_puts(") -> ");
    uart_puts(vdd_state_name(vdd_eval(vdd)));
    uart_puts("\n");

    /* Buoc 2: gon song trong 1 s */
    pass_ripple = (ripple <= RIPPLE_MAX_MV);
    uart_puts("Buoc 2/3: gon 3V3 trong 1 s = ");
    uart_put_u32(ripple);
    uart_puts(" mV (min ");
    put_volt(vmin);
    uart_puts(", max ");
    put_volt(vmax);
    uart_puts(", cho phep <= ");
    uart_put_u32(RIPPLE_MAX_MV);
    uart_puts(" mV)\n");

    /* Buoc 3: mach chia Vin */
    pass_vin = true;
    uart_puts("Buoc 3/3: Vin tren " VIN_PIN_NAME ": ");
    if (!vin_present) {
        uart_puts("khong co mach chia -> BO QUA (khong tinh vao ket qua)\n");
    } else {
        supply_meas_t tmp = { 0 };
        tmp.ok = true;
        tmp.vin_mv = vin;
        tmp.vdd_mv = vdd;
        put_volt(vin);
        uart_puts(" -> ");
        uart_puts(supply_source_name(supply_classify(&tmp)));
        if (vin_sat) {
            pass_vin = false;
            uart_puts(" | CHAM DINH THANG DO: Vin > ");
            put_volt(((uint32_t)vdd * (VIN_R_TOP_K + VIN_R_BOT_K)) / VIN_R_BOT_K);
            uart_puts(", tang R1");
        }
        if (ldo_dropout(&tmp)) {
            pass_vin = false;
            uart_puts(" | Vin - 3V3 = ");
            uart_put_i32((int32_t)vin - (int32_t)vdd);
            uart_puts(" mV < ");
            uart_put_u32(LDO_DROPOUT_MV);
            uart_puts(" mV: LDO SUT AP");
        }
        uart_puts("\n");
    }

    uart_puts("===== KET QUA TEST =====\n");
    uart_puts("  Buoc 1 3V3 trong dai : "); uart_puts(pass_vdd ? "DAT\n" : "KHONG DAT\n");
    uart_puts("  Buoc 2 gon song      : "); uart_puts(pass_ripple ? "DAT\n" : "KHONG DAT\n");
    uart_puts("  Buoc 3 Vin / LDO     : ");
    uart_puts(!vin_present ? "BO QUA\n" : (pass_vin ? "DAT\n" : "KHONG DAT\n"));
    uart_puts("  Doi chieu dong ho: do P3-10 (3V3) -> P5-1 (GND), phai ~ ");
    put_volt(vdd);
    uart_puts(" (sai lech <= 2% la binh thuong)\n");
    uart_puts((pass_vdd && pass_ripple && pass_vin) ? ">>> NGUON HOAT DONG DUNG\n\n"
                                                    : ">>> KHONG DAT - xem muc Khac phuc su co trong README\n\n");
}

/* ---------------- Chuong trinh chinh ---------------- */

static const uint16_t BOR_MV[6] = { 0U, 1690U, 1940U, 2300U, 2540U, 2770U };

int main(void)
{
    supply_meas_t m;
    supply_source_t src_now = SRC_UNKNOWN, src_cand = SRC_UNKNOWN;
    uint32_t src_cand_n = 0U;
    vdd_state_t vdd_st;
    uint16_t vdd_ref;
    uint32_t last_mon, last_status;
    bool cal_ok;
    uint16_t cal;
    uint8_t bor;

    hal_clock_init();
    uart_init(LOG_BAUDRATE);
    const char *rst = hal_reset_cause();
    hal_iwdg_start();
    supply_init();
    cal = supply_vrefint_cal(&cal_ok);
    bor = hal_bor_level();

    uart_puts("\n\n=== Test dien ap nguon 3V3 / pin - RAK811 / WisNode-LoRa ===\n");
    uart_puts("Reset do   : ");
    uart_puts(rst);
    uart_puts("\n3V3 (VDD)  : do qua VREFINT, khong can noi day. VREFINT_CAL = ");
    uart_put_u32(cal);
    uart_puts(cal_ok ? "\n" : " (O HIEU CHUAN HONG!)\n");
    uart_puts("Vin        : chia ap " VIN_PIN_NAME " R1=");
    uart_put_u32(VIN_R_TOP_K);
    uart_puts("k R2=");
    uart_put_u32(VIN_R_BOT_K);
    uart_puts("k (+100 nF), khong lap thi chi do 3V3\n");
    uart_puts("Brown-out  : ");
    if (bor == 0U) {
        uart_puts("TAT (chi co POR ~1.5 V) - nen bat muc 3..4 bang STM32CubeProgrammer\n");
    } else {
        uart_puts("muc ");
        uart_put_u32(bor);
        uart_puts(", reset khi 3V3 < ~");
        put_volt(BOR_MV[bor]);
        uart_puts("\n");
    }
    print_help();

    if (supply_measure(&m, 256U)) {
        src_now = supply_classify(&m);
        session_update(&m);
    } else {
        ses_adc_err++;
    }
    vdd_st  = vdd_eval(m.vdd_mv);
    vdd_ref = m.vdd_mv;
    event_add(EV_BOOT, 0U, &m);
    print_status(&m);
    if (m.ok && (vdd_st != VDD_ST_OK)) {
        event_add(EV_VDD_STATE, (uint8_t)vdd_st, &m);
    }
    last_mon = last_status = hal_millis();

    for (;;) {
        uint32_t now;
        int c;

        hal_iwdg_kick();

        c = uart_getc();
        switch (c) {
        case 't': case 'T': run_test();     break;
        case 'i': case 'I': print_detail(); break;
        case 'e': case 'E': events_dump();  break;
        case 'c': case 'C':
            session_clear();
            uart_puts("Da xoa min/max va nhat ky\n");
            break;
        case 'h': case 'H': case '?': print_help(); break;
        default: break;
        }

        now = hal_millis();
        if ((now - last_mon) < MONITOR_PERIOD_MS) {
            __WFI();                      /* SysTick danh thuc moi 1 ms */
            continue;
        }
        last_mon = now;

        if (!supply_measure(&m, 16U)) {
            ses_adc_err++;
            if (ses_adc_err == 1U) {
                event_add(EV_ADC_ERR, 0U, &m);
            }
            continue;
        }
        session_update(&m);

        /* Doi nguon: phai thay nguon moi 2 lan lien tiep (0.5 s) moi ghi */
        {
            supply_source_t s = supply_classify(&m);
            if (s == src_now) {
                src_cand_n = 0U;
            } else if ((s == src_cand) && (++src_cand_n >= 2U)) {
                src_now = s;
                src_cand_n = 0U;
                event_add(EV_SRC, (uint8_t)s, &m);
            } else if (s != src_cand) {
                src_cand = s;
                src_cand_n = 1U;
            }
        }

        /* Trang thai 3V3 (co tre 30 mV de khong nhay qua lai o sat nguong) */
        {
            vdd_state_t st = vdd_eval(m.vdd_mv);
            if ((st != vdd_st) &&
                (vdd_eval((uint16_t)(m.vdd_mv + 30U)) == st) &&
                (vdd_eval((uint16_t)(m.vdd_mv - 30U)) == st)) {
                vdd_st = st;
                event_add(EV_VDD_STATE, (uint8_t)st, &m);
                vdd_ref = m.vdd_mv;
            }
        }

        /* 3V3 nhay dot ngot (vd: rut USB khi dang co pin, LDO khac dien ap) */
        if ((m.vdd_mv > vdd_ref + VDD_EVENT_STEP_MV) || (m.vdd_mv + VDD_EVENT_STEP_MV < vdd_ref)) {
            event_add(EV_VDD_STEP, 0U, &m);
            vdd_ref = m.vdd_mv;
        }

        if ((now - last_status) >= STATUS_PERIOD_MS) {
            last_status = now;
            print_status(&m);
        }
    }
}
