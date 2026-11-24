/*
 * supply.c - ADC1 STM32L151: VREFINT (kenh 17) + PA2 (kenh 2), polling, khong DMA.
 * Moi cap mau: doc VREFINT -> tinh VDD tuc thoi -> doc PA2 quy doi bang chinh VDD do,
 * nen ket qua Vin dung ca khi 3V3 dang dao dong.
 * Cach lam giong Project_node_kho_lanh/node/battery.c, them min/max de do gon song.
 */
#include "supply.h"
#include "board.h"
#include <stddef.h>
#include "hal.h"

#define VREFINT_CAL_ADDR   0x1FF80078UL
#define VREFINT_CAL_VDD_MV 3000U
#define VREFINT_TYP_RAW    1671U          /* ~1.224 V @ 3.0 V, dung khi o hieu chuan khong hop le */
#define ADC_CH_VREFINT     17U
#define ADC_FULL_SCALE     4095U
#define ADC_TIMEOUT        100000U

void supply_init(void)
{
    hal_gpio_analog(VIN_PORT, VIN_PIN);
}

uint16_t supply_vrefint_cal(bool *valid)
{
    uint16_t cal = *(const volatile uint16_t *)VREFINT_CAL_ADDR;
    bool ok = (cal >= 1400U) && (cal <= 1900U);
    if (valid != NULL) {
        *valid = ok;
    }
    return ok ? cal : (uint16_t)VREFINT_TYP_RAW;
}

/* Chan doan lan do loi gan nhat (in ra log de biet hong o buoc nao) */
static const char *s_err = "";
static uint32_t    s_err_sr;
static uint32_t    s_err_val;

static bool adc_fail(const char *why, uint32_t val)
{
    s_err = why;
    s_err_sr = ADC1->SR;
    s_err_val = val;
    return false;
}

static bool adc_convert(uint32_t ch, uint32_t *raw)
{
    ADC1->SQR5 = ch;
    /* STM32L1: ghi SQRx lam RCNR = 1 mot luc; SWSTART phat ra khi RCNR = 1 bi BO QUA
     * -> phai cho RCNR ve 0 truoc khi bat dau chuyen doi */
    if (!hal_wait_flag(&ADC1->SR, ADC_SR_RCNR, false, ADC_TIMEOUT)) {
        return adc_fail("RCNR khong ve 0 sau khi chon kenh", ch);
    }
    ADC1->CR2 |= ADC_CR2_SWSTART;
    if (!hal_wait_flag(&ADC1->SR, ADC_SR_EOC, true, ADC_TIMEOUT)) {
        return adc_fail("EOC het gio (chuyen doi khong xong)", ch);
    }
    *raw = ADC1->DR & 0x0FFFU;      /* doc DR tu xoa EOC */
    return true;
}

static void adc_on(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;          /* de doc PWR_CSR.VREFINTRDYF */
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    (void)RCC->APB2ENR;
    ADC->CCR = (ADC->CCR & ~ADC_CCR_ADCPRE) | (2UL << 16) | ADC_CCR_TSVREFE;   /* HSI/4 = 4 MHz */
    (void)hal_wait_flag(&PWR->CSR, PWR_CSR_VREFINTRDYF, true, ADC_TIMEOUT);
    hal_delay_us(20U);                          /* t_START VREFINT / bo dem noi */

    ADC1->CR1 = 0U;                             /* 12 bit, khong scan */
    ADC1->CR2 = 0U;                             /* single, can phai */
    ADC1->SMPR2 = (ADC1->SMPR2 & ~(7UL << 21)) | (7UL << 21);   /* ch17: 384 chu ky */
    ADC1->SMPR3 = (ADC1->SMPR3 & ~(7UL << (VIN_ADC_CH * 3U))) |
                  (7UL << (VIN_ADC_CH * 3U));                  /* ch2 : 384 chu ky (nguon tro 500k) */
    ADC1->SQR1 = 0U;                            /* 1 kenh trong chuoi */
    ADC1->CR2 |= ADC_CR2_ADON;
}

static void adc_off(void)
{
    ADC1->CR2 &= ~ADC_CR2_ADON;
    ADC->CCR &= ~ADC_CCR_TSVREFE;
    RCC->APB2ENR &= ~RCC_APB2ENR_ADC1EN;
}

/* Phat hien PA2 bo ho: bat pull 40k trong VIN_PROBE_US roi doc muc logic.
 *  - co mach chia: tu 100 nF giu dien ap (tau ~3.7 ms) -> pull-down va pull-up doc CUNG muc
 *  - bo ho: vai chuc pF, pull keo theo ngay (tau < 2 us) -> pull-down doc 0, pull-up doc 1 */
static bool vin_pin_floating(void)
{
    bool lo, hi;
    hal_gpio_input_pulldown(VIN_PORT, VIN_PIN);
    hal_delay_us(VIN_PROBE_US);
    lo = gpio_read(VIN_PORT, VIN_PIN);
    hal_gpio_input_pullup(VIN_PORT, VIN_PIN);
    hal_delay_us(VIN_PROBE_US);
    hi = gpio_read(VIN_PORT, VIN_PIN);
    hal_gpio_analog(VIN_PORT, VIN_PIN);
    return !lo && hi;
}

bool supply_measure(supply_meas_t *m, uint32_t n)
{
    uint32_t cal = supply_vrefint_cal(NULL);
    uint32_t sum_vref = 0U, sum_pa2 = 0U, sum_vdd = 0U, sum_vin = 0U, sum_pa2_mv = 0U;
    uint32_t vdd_min = 0xFFFFU, vdd_max = 0U, vin_min = 0xFFFFU, vin_max = 0U;
    uint32_t i;
    bool ok;
    bool sat = false;

    *m = (supply_meas_t){ 0 };
    if (n == 0U)   { n = 1U; }
    if (n > 1024U) { n = 1024U; }

    adc_on();
    ok = hal_wait_flag(&ADC1->SR, ADC_SR_ADONS, true, ADC_TIMEOUT);
    if (!ok) {
        (void)adc_fail("ADONS khong len sau khi bat ADON", ADC1->CR2);
    }

    for (i = 0U; ok && (i < n); i++) {
        uint32_t vref, pa2, vdd, pa2_mv, vin;

        ok = adc_convert(ADC_CH_VREFINT, &vref) && adc_convert(VIN_ADC_CH, &pa2);
        if (ok && ((vref < 1000U) || (vref >= ADC_FULL_SCALE))) {
            ok = adc_fail("VREFINT vo ly (ma ADC)", vref);   /* VDD > 5 V hoac < 1.3 V: khong the */
        }
        if (!ok) {
            break;
        }
        vdd    = (VREFINT_CAL_VDD_MV * cal + vref / 2U) / vref;
        pa2_mv = (pa2 * vdd + ADC_FULL_SCALE / 2U) / ADC_FULL_SCALE;
        vin    = (pa2_mv * (VIN_R_TOP_K + VIN_R_BOT_K) + VIN_R_BOT_K / 2U) / VIN_R_BOT_K;
        if (pa2 >= ADC_FULL_SCALE - 4U) {
            sat = true;
        }

        sum_vref += vref;
        sum_pa2  += pa2;
        sum_vdd  += vdd;
        sum_pa2_mv += pa2_mv;
        sum_vin  += vin;
        if (vdd < vdd_min) { vdd_min = vdd; }
        if (vdd > vdd_max) { vdd_max = vdd; }
        if (vin < vin_min) { vin_min = vin; }
        if (vin > vin_max) { vin_max = vin; }
    }

    adc_off();

    if (!ok) {
        return false;
    }

    m->ok         = true;
    m->samples    = (uint16_t)n;
    m->vref_raw   = (uint16_t)((sum_vref + n / 2U) / n);
    m->pa2_raw    = (uint16_t)((sum_pa2 + n / 2U) / n);
    m->vdd_mv     = (uint16_t)((sum_vdd + n / 2U) / n);
    m->vdd_min_mv = (uint16_t)vdd_min;
    m->vdd_max_mv = (uint16_t)vdd_max;
    m->pa2_mv     = (uint16_t)((sum_pa2_mv + n / 2U) / n);
    m->vin_saturated = sat;

    m->vin_floating = vin_pin_floating();      /* sau khi do ADC: xung pull khong anh huong so vua do */
    if ((!m->vin_floating) && (((sum_vin + n / 2U) / n) >= VIN_MIN_VALID_MV)) {
        m->vin_mv     = (uint16_t)((sum_vin + n / 2U) / n);
        m->vin_min_mv = (uint16_t)vin_min;
        m->vin_max_mv = (uint16_t)vin_max;
    }
    return true;
}

supply_source_t supply_classify(const supply_meas_t *m)
{
    if (!m->ok || (m->vin_mv == 0U) ||
        ((uint32_t)(m->vin_max_mv - m->vin_min_mv) > VIN_FLOAT_SPREAD_MV)) {   /* PA2 bo ho: so nhay lung tung */
        return SRC_UNKNOWN;
    }
    if (m->vin_mv >= VIN_USB_MIN_MV) {
        return SRC_USB_5V;
    }
    return (m->vin_mv < BATT_LOW_MV) ? SRC_BATTERY_LOW : SRC_BATTERY;
}

const char *supply_source_name(supply_source_t s)
{
    switch (s) {
    case SRC_USB_5V:      return "NGUON 5V (USB/adapter)";
    case SRC_BATTERY:     return "PIN";
    case SRC_BATTERY_LOW: return "PIN YEU";
    default:              return "KHONG RO (chua lap mach chia PA2)";
    }
}

/* Duong cong ho mach Li-ion 1 cell o ~25 C, tai nhe (gan dung) */
static const uint16_t LIION_MV[]  = { 4200, 4100, 4000, 3900, 3800, 3750, 3700, 3650, 3600, 3500, 3400, 3300 };
static const uint8_t  LIION_PCT[] = {  100,   90,   79,   68,   55,   45,   35,   25,   15,    7,    3,    0 };

uint8_t supply_liion_percent(uint16_t mv)
{
    uint32_t i;
    const uint32_t cnt = sizeof(LIION_MV) / sizeof(LIION_MV[0]);

    if (mv >= LIION_MV[0]) {
        return 100U;
    }
    for (i = 1U; i < cnt; i++) {
        if (mv >= LIION_MV[i]) {
            /* noi suy tuyen tinh giua 2 diem */
            uint32_t span_mv  = (uint32_t)LIION_MV[i - 1U] - LIION_MV[i];
            uint32_t span_pct = (uint32_t)LIION_PCT[i - 1U] - LIION_PCT[i];
            return (uint8_t)(LIION_PCT[i] + ((uint32_t)(mv - LIION_MV[i]) * span_pct + span_mv / 2U) / span_mv);
        }
    }
    return 0U;
}

const char *supply_last_error(uint32_t *sr, uint32_t *val)
{
    *sr  = s_err_sr;
    *val = s_err_val;
    return s_err;
}
