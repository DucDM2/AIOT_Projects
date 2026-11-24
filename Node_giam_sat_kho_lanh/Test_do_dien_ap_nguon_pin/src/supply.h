/*
 * supply.h - Do dien ap nguon 3V3 (qua VREFINT) va dien ap vao Vin (chia ap tren PA2).
 */
#ifndef SUPPLY_H
#define SUPPLY_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    bool     ok;            /* false: ADC khong chay / VREFINT bat thuong */
    uint16_t samples;       /* so cap mau da do */
    uint16_t vref_raw;      /* trung binh ma ADC cua VREFINT */
    uint16_t vdd_mv;        /* trung binh 3V3 */
    uint16_t vdd_min_mv;    /* min/max trong loat mau -> gon song */
    uint16_t vdd_max_mv;
    uint16_t pa2_raw;       /* trung binh ma ADC tai PA2 */
    uint16_t pa2_mv;        /* dien ap tai chan PA2 */
    uint16_t vin_mv;        /* dien ap truoc mach chia; 0 neu khong lap mach chia */
    uint16_t vin_min_mv;
    uint16_t vin_max_mv;
    bool     vin_saturated; /* PA2 cham dinh thang do -> Vin vuot gioi han do */
    bool     vin_floating;  /* PA2 bo ho (phat hien bang xung pull) -> vin_mv = 0 */
} supply_meas_t;

typedef enum {
    SRC_UNKNOWN = 0,   /* khong co mach chia: chi biet 3V3 */
    SRC_USB_5V,        /* Vin >= VIN_USB_MIN_MV */
    SRC_BATTERY,       /* Vin hop le, thap hon muc 5V */
    SRC_BATTERY_LOW    /* Vin < BATT_LOW_MV */
} supply_source_t;

void            supply_init(void);
/* Do n cap mau (VREFINT + PA2), n = 1..1024. ADC chi bat trong luc do. */
bool            supply_measure(supply_meas_t *m, uint32_t n);
supply_source_t supply_classify(const supply_meas_t *m);
const char     *supply_source_name(supply_source_t s);
/* Gia tri hieu chuan VREFINT_CAL doc tu chip; *valid = false neu nam ngoai dai hop ly */
uint16_t        supply_vrefint_cal(bool *valid);
/* Phan tram pin Li-ion uoc tinh theo dien ap ho mach (0..100) */
uint8_t         supply_liion_percent(uint16_t mv);
/* Ly do lan do loi gan nhat + ADC1->SR luc loi + gia tri kem theo (kenh / ma ADC) */
const char     *supply_last_error(uint32_t *sr, uint32_t *val);

#endif /* SUPPLY_H */
