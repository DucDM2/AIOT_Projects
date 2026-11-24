/*
 * sx1276.c - Driver SX1276 che do LoRa.
 */
#include "sx1276.h"
#include "board.h"
#include "hal.h"

/* ---- Thanh ghi (che do LoRa) ---- */
#define REG_FIFO                0x00
#define REG_OPMODE              0x01
#define REG_FRF_MSB             0x06
#define REG_FRF_MID             0x07
#define REG_FRF_LSB             0x08
#define REG_PACONFIG            0x09
#define REG_OCP                 0x0B
#define REG_LNA                 0x0C
#define REG_FIFO_ADDR_PTR       0x0D
#define REG_FIFO_TX_BASE        0x0E
#define REG_FIFO_RX_BASE        0x0F
#define REG_FIFO_RX_CURRENT     0x10
#define REG_IRQ_FLAGS_MASK      0x11
#define REG_IRQ_FLAGS           0x12
#define REG_RX_NB_BYTES         0x13
#define REG_PKT_SNR             0x19
#define REG_PKT_RSSI            0x1A
#define REG_MODEM_CONFIG1       0x1D
#define REG_MODEM_CONFIG2       0x1E
#define REG_PREAMBLE_MSB        0x20
#define REG_PREAMBLE_LSB        0x21
#define REG_PAYLOAD_LENGTH      0x22
#define REG_MAX_PAYLOAD_LENGTH  0x23
#define REG_MODEM_CONFIG3       0x26
#define REG_RSSI_WIDEBAND       0x2C
#define REG_IF_FREQ1            0x2F
#define REG_IF_FREQ2            0x30
#define REG_DETECT_OPTIMIZE     0x31
#define REG_HIGH_BW_OPT1        0x36
#define REG_DETECTION_THRESHOLD 0x37
#define REG_SYNC_WORD           0x39
#define REG_HIGH_BW_OPT2        0x3A
#define REG_IMAGE_CAL           0x3B      /* (truy cap o che do FSK) */
#define REG_DIO_MAPPING1        0x40
#define REG_VERSION             0x42
#define REG_PADAC               0x4D

#define OPMODE_LORA             0x80
#define OPMODE_LF               0x08      /* LowFrequencyModeOn (< 525 MHz) */
#define MODE_SLEEP              0x00
#define MODE_STDBY              0x01
#define MODE_TX                 0x03
#define MODE_RX_CONT            0x05

#define IRQ_RX_DONE             0x40
#define IRQ_CRC_ERROR           0x20
#define IRQ_TX_DONE             0x08

#define MID_BAND_HZ             525000000UL
#define FXOSC_HZ                32000000ULL

static sx1276_config_t s_cfg;
static uint8_t s_lf;            /* OPMODE_LF neu tan so thap */
static bool    s_ldro;
static bool    s_present;
static bool    s_mismatch;
static int     s_variant;          /* 0 = khong ro, 1 = H, 2 = L */

/* ---------------- SPI1 ---------------- */

static uint8_t spi_xfer(uint8_t b)
{
    uint32_t t = 0;
    while (!(SPI1->SR & SPI_SR_TXE) && t++ < 10000U) { }
    *(volatile uint8_t *)&SPI1->DR = b;
    t = 0;
    while (!(SPI1->SR & SPI_SR_RXNE) && t++ < 10000U) { }
    return *(volatile uint8_t *)&SPI1->DR;
}

static inline void nss_low(void)  { gpio_low(RADIO_NSS_PORT, RADIO_NSS_PIN); }
static void nss_high(void)
{
    uint32_t t = 0;
    while ((SPI1->SR & SPI_SR_BSY) && t++ < 10000U) { }
    gpio_high(RADIO_NSS_PORT, RADIO_NSS_PIN);
}

static void wr(uint8_t reg, uint8_t val)
{
    nss_low();
    spi_xfer(reg | 0x80U);
    spi_xfer(val);
    nss_high();
}

static uint8_t rd(uint8_t reg)
{
    uint8_t v;
    nss_low();
    spi_xfer(reg & 0x7FU);
    v = spi_xfer(0x00);
    nss_high();
    return v;
}

static void wr_burst(uint8_t reg, const uint8_t *d, uint8_t n)
{
    nss_low();
    spi_xfer(reg | 0x80U);
    for (uint8_t i = 0; i < n; i++) { spi_xfer(d[i]); }
    nss_high();
}

static void rd_burst(uint8_t reg, uint8_t *d, uint8_t n)
{
    nss_low();
    spi_xfer(reg & 0x7FU);
    for (uint8_t i = 0; i < n; i++) { d[i] = spi_xfer(0x00); }
    nss_high();
}

static void spi_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    (void)RCC->APB2ENR;
    hal_gpio_af(RADIO_SCK_PORT,  RADIO_SCK_PIN,  5U);
    hal_gpio_af(RADIO_MISO_PORT, RADIO_MISO_PIN, 5U);
    hal_gpio_af(RADIO_MOSI_PORT, RADIO_MOSI_PIN, 5U);

    SPI1->CR1 = 0U;
    SPI1->CR2 = 0U;
    /* Master, CPOL=0 CPHA=0, MSB truoc, 8 bit, f = 16 MHz / 4 = 4 MHz (SX1276 toi da 10 MHz) */
    SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_BR_0 | SPI_CR1_SSM | SPI_CR1_SSI;
    SPI1->CR1 |= SPI_CR1_SPE;
}

/* ---------------- Cong tac anten ---------------- */

static void antsw_tx(void)
{
    gpio_low(RADIO_CRX_RX_PORT, RADIO_CRX_RX_PIN);
    gpio_low(RADIO_CBT_HF_PORT, RADIO_CBT_HF_PIN);
    gpio_high(RADIO_CTX_PA_PORT, RADIO_CTX_PA_PIN);
}

static void antsw_rx(void)
{
    gpio_low(RADIO_CTX_PA_PORT, RADIO_CTX_PA_PIN);
    gpio_low(RADIO_CBT_HF_PORT, RADIO_CBT_HF_PIN);
    gpio_high(RADIO_CRX_RX_PORT, RADIO_CRX_RX_PIN);
}

static void antsw_off(void)
{
    gpio_low(RADIO_CTX_PA_PORT, RADIO_CTX_PA_PIN);
    gpio_low(RADIO_CBT_HF_PORT, RADIO_CBT_HF_PIN);
    gpio_low(RADIO_CRX_RX_PORT, RADIO_CRX_RX_PIN);
}

/* ---------------- Ho tro ---------------- */

static void set_mode(uint8_t mode)
{
    wr(REG_OPMODE, (uint8_t)(OPMODE_LORA | s_lf | mode));
}

static void set_freq(uint32_t hz)
{
    uint32_t frf = (uint32_t)(((uint64_t)hz << 19) / FXOSC_HZ);
    wr(REG_FRF_MSB, (uint8_t)(frf >> 16));
    wr(REG_FRF_MID, (uint8_t)(frf >> 8));
    wr(REG_FRF_LSB, (uint8_t)frf);
}

static void set_tx_power(int8_t p)
{
    if (p > 20) { p = 20; }
    if (p < 2)  { p = 2; }
    if (p > 17) {
        wr(REG_PADAC, 0x87);                                   /* +20 dBm mode */
        wr(REG_OCP, 0x20 | 0x0F);                              /* OCP 120 mA */
        wr(REG_PACONFIG, (uint8_t)(0x80 | 0x70 | (p - 5)));
    } else {
        wr(REG_PADAC, 0x84);
        wr(REG_OCP, 0x20 | 0x0B);                              /* OCP 100 mA */
        wr(REG_PACONFIG, (uint8_t)(0x80 | 0x70 | (p - 2)));    /* PA_BOOST */
    }
}

static uint8_t bw_code(uint16_t khz)
{
    switch (khz) {
    case 250: return 8;
    case 500: return 9;
    default:  return 7;   /* 125 kHz */
    }
}

/* Hieu chuan duong thu theo tan so (phai lam o che do FSK standby) */
static void image_calibration(uint32_t hz)
{
    uint8_t pa = rd(REG_PACONFIG);
    uint32_t t;

    wr(REG_OPMODE, (uint8_t)(s_lf | MODE_SLEEP));      /* FSK sleep */
    hal_delay_ms(1);
    wr(REG_OPMODE, (uint8_t)(s_lf | MODE_STDBY));      /* FSK standby */
    hal_delay_ms(1);
    /* Giong init cua RAK: TAT ngo ra xung clock (RegOsc ClkOut = OFF) va dat lai DIO5 mapping,
     * tranh DIO cua SX1276 xuat xung trong khi STM32 dang day muc len chan noi voi no (PH1 = SX_GPIO0). */
    wr(0x24, 0x07);                                    /* RegOsc (FSK): ClkOut off */
    wr(0x41, 0x30);                                    /* RegDioMapping2 */
    wr(REG_PACONFIG, 0x00);
    set_freq(hz);
    wr(REG_IMAGE_CAL, (uint8_t)(rd(REG_IMAGE_CAL) | 0x40));   /* ImageCalStart */
    for (t = 0; (rd(REG_IMAGE_CAL) & 0x20) && t < 50U; t++) { /* ImageCalRunning */
        hal_delay_ms(1);
    }
    wr(REG_PACONFIG, pa);
}

/* Giat thap 1 chan ~1 ms; true neu SX1276 vua bi reset (RegFrfMsb 0x06 ve mac dinh 0x6C) */
static bool pulse_resets_radio(GPIO_TypeDef *port, uint32_t pin)
{
    wr(REG_FRF_MSB, 0x77);
    if (rd(REG_FRF_MSB) != 0x77) {
        return false;                       /* radio khong nghe SPI */
    }
    hal_gpio_output(port, pin, false);
    hal_delay_ms(1);
    hal_gpio_input(port, pin);
    hal_delay_ms(10);
    return rd(REG_FRF_MSB) == 0x6C;
}

int  sx1276_detected_variant(void) { return s_variant; }
bool sx1276_variant_mismatch(void) { return s_mismatch; }

/* ---------------- API ---------------- */

uint8_t sx1276_init(const sx1276_config_t *cfg)
{
    uint8_t ver = 0;

    s_cfg = *cfg;
    s_lf  = (cfg->freq_hz < MID_BAND_HZ) ? OPMODE_LF : 0U;
    /* LowDataRateOptimize khi thoi gian 1 symbol >= 16 ms */
    s_ldro = (((1UL << cfg->sf) * 1000UL) / cfg->bw_khz) >= 16000UL;

    /* GPIO: NSS, cap nguon dao dong, cong tac RF */
    hal_gpio_output(RADIO_NSS_PORT, RADIO_NSS_PIN, true);
    hal_gpio_output(RADIO_XTAL_PORT, RADIO_XTAL_PIN, true);
    hal_gpio_output(RADIO_CTX_PA_PORT, RADIO_CTX_PA_PIN, false);
    hal_gpio_output(RADIO_CRX_RX_PORT, RADIO_CRX_RX_PIN, false);
    hal_gpio_output(RADIO_CBT_HF_PORT, RADIO_CBT_HF_PIN, false);
    hal_gpio_input(RADIO_DIO0_PORT, RADIO_DIO0_PIN);
    spi_init();
    hal_delay_ms(5);

    /* AN TOAN: nhan dien ban module that (chan nao reset duoc radio). Nap nham H/L -> khoa radio,
     * vi cong tac anten se bi dieu khien sai chan va phat sai bang tan -> co the hong tang phat. */
    s_variant = pulse_resets_radio(RADIO_RESET_H_PORT, RADIO_RESET_H_PIN) ? 1 :
                (pulse_resets_radio(RADIO_RESET_L_PORT, RADIO_RESET_L_PIN) ? 2 : 0);
    s_mismatch = (RADIO_FW_IS_H == 1) ? (s_variant == 2) : (s_variant == 1);

    /* Reset cung: keo thap 1 ms, tha noi, cho 10 ms (datasheet: >= 5 ms) */
    hal_gpio_output(RADIO_RESET_PORT, RADIO_RESET_PIN, false);
    hal_delay_ms(1);
    hal_gpio_input(RADIO_RESET_PORT, RADIO_RESET_PIN);
    hal_delay_ms(10);

    for (int i = 0; i < 3 && ver != 0x12U; i++) {
        ver = rd(REG_VERSION);
        if (ver != 0x12U) { hal_delay_ms(5); }
    }
    s_present = (ver == 0x12U) && !s_mismatch;
    if (s_mismatch) {
        wr(REG_OPMODE, MODE_SLEEP);         /* radio ngu, khong bao gio phat */
        return 0;
    }
    if (!s_present) {
        return ver;
    }

    image_calibration(cfg->freq_hz);

    /* Chuyen sang LoRa: chi doi duoc bit LongRangeMode khi o Sleep */
    wr(REG_OPMODE, (uint8_t)(s_lf | MODE_SLEEP));
    hal_delay_ms(1);
    set_mode(MODE_SLEEP);
    hal_delay_ms(1);
    set_mode(MODE_STDBY);

    set_freq(cfg->freq_hz);
    set_tx_power(cfg->tx_power_dbm);
    wr(REG_LNA, s_lf ? 0x20 : 0x23);                  /* gain max, LNA boost (HF) */

    wr(REG_MODEM_CONFIG1, (uint8_t)((bw_code(cfg->bw_khz) << 4) | ((cfg->cr & 0x07U) << 1)));  /* explicit header */
    wr(REG_MODEM_CONFIG2, (uint8_t)((cfg->sf << 4) | 0x04));      /* RxPayloadCrcOn */
    wr(REG_MODEM_CONFIG3, (uint8_t)(0x04 | (s_ldro ? 0x08 : 0x00))); /* AGC auto */
    wr(REG_PREAMBLE_MSB, (uint8_t)(cfg->preamble >> 8));
    wr(REG_PREAMBLE_LSB, (uint8_t)cfg->preamble);
    wr(REG_SYNC_WORD, cfg->sync_word);
    wr(REG_MAX_PAYLOAD_LENGTH, 0xFF);
    wr(REG_FIFO_TX_BASE, 0x00);
    wr(REG_FIFO_RX_BASE, 0x00);
    wr(REG_DETECT_OPTIMIZE, (uint8_t)((rd(REG_DETECT_OPTIMIZE) & 0xF8) | 0x03));  /* SF7..12 */
    wr(REG_DETECTION_THRESHOLD, 0x0A);

    /* Errata 2.1: toi uu do nhay khi BW 500 kHz */
    if (cfg->bw_khz == 500U) {
        wr(REG_HIGH_BW_OPT1, 0x02);
        wr(REG_HIGH_BW_OPT2, s_lf ? 0x7F : 0x64);
        wr(REG_DETECT_OPTIMIZE, (uint8_t)(rd(REG_DETECT_OPTIMIZE) | 0x80));
    } else {
        wr(REG_HIGH_BW_OPT1, 0x03);
        /* Errata 2.3: chong thu nham tin hieu gia (BW < 500 kHz) */
        wr(REG_DETECT_OPTIMIZE, (uint8_t)(rd(REG_DETECT_OPTIMIZE) & 0x7F));
        wr(REG_IF_FREQ2, 0x00);
        wr(REG_IF_FREQ1, 0x40);
    }

    wr(REG_IRQ_FLAGS_MASK, 0x00);
    wr(REG_IRQ_FLAGS, 0xFF);
    antsw_off();
    return ver;
}

bool sx1276_present(void)
{
    return s_present;
}

void sx1276_standby(void)
{
    set_mode(MODE_STDBY);
    antsw_off();
}

void sx1276_sleep(void)
{
    set_mode(MODE_SLEEP);
    antsw_off();
}

bool sx1276_send(const uint8_t *data, uint8_t len)
{
    uint32_t timeout = sx1276_airtime_ms(len) * 2U + 100U;
    uint32_t start;
    bool done = false;

    if (!s_present) {
        return false;
    }
    set_mode(MODE_STDBY);
    wr(REG_FIFO_ADDR_PTR, 0x00);
    wr_burst(REG_FIFO, data, len);
    wr(REG_PAYLOAD_LENGTH, len);
    wr(REG_IRQ_FLAGS, 0xFF);
    wr(REG_DIO_MAPPING1, 0x40);           /* DIO0 = TxDone */
    antsw_tx();
    set_mode(MODE_TX);

    start = hal_millis();
    while ((hal_millis() - start) < timeout) {
        if (rd(REG_IRQ_FLAGS) & IRQ_TX_DONE) {
            done = true;
            break;
        }
    }
    wr(REG_IRQ_FLAGS, 0xFF);
    set_mode(MODE_STDBY);
    antsw_off();
    return done;
}

void sx1276_rx_start(void)
{
    if (!s_present) {
        return;
    }
    set_mode(MODE_STDBY);
    wr(REG_FIFO_ADDR_PTR, 0x00);
    wr(REG_IRQ_FLAGS, 0xFF);
    wr(REG_DIO_MAPPING1, 0x00);           /* DIO0 = RxDone */
    antsw_rx();
    set_mode(MODE_RX_CONT);
}

int sx1276_rx_poll(uint8_t *buf, uint8_t maxlen, sx1276_pkt_info_t *info)
{
    uint8_t flags, len, cur;
    int8_t snr;
    int16_t rssi;

    if (!s_present) {
        return 0;
    }
    flags = rd(REG_IRQ_FLAGS);
    if ((flags & IRQ_RX_DONE) == 0U) {
        return 0;
    }
    wr(REG_IRQ_FLAGS, 0xFF);

    snr  = (int8_t)rd(REG_PKT_SNR);
    rssi = (int16_t)rd(REG_PKT_RSSI);
    rssi += s_lf ? -164 : -157;
    if (snr < 0) {
        rssi = (int16_t)(rssi + snr / 4);
    }
    if (info != 0) {
        info->rssi_dbm = rssi;
        info->snr_x4   = snr;
    }

    if (flags & IRQ_CRC_ERROR) {
        return -1;
    }

    len = rd(REG_RX_NB_BYTES);
    cur = rd(REG_FIFO_RX_CURRENT);
    wr(REG_FIFO_ADDR_PTR, cur);
    if (len > maxlen) {
        len = maxlen;
    }
    rd_burst(REG_FIFO, buf, len);
    return (int)len;
}

uint32_t sx1276_airtime_ms(uint8_t len)
{
    /* Datasheet SX1276 muc 4.1.1.7: explicit header, CRC bat */
    uint32_t sf = s_cfg.sf;
    uint32_t tsym_us = ((1UL << sf) * 1000UL) / s_cfg.bw_khz;
    int32_t de  = s_ldro ? 1 : 0;
    int32_t num = 8 * (int32_t)len - 4 * (int32_t)sf + 28 + 16;
    int32_t den = 4 * ((int32_t)sf - 2 * de);
    int32_t n   = (num > 0) ? (num + den - 1) / den : 0;
    uint32_t n_payload = 8U + (uint32_t)n * (s_cfg.cr + 4U);
    uint32_t t_pre_us  = ((uint32_t)s_cfg.preamble * 4U + 17U) * tsym_us / 4U;   /* (N + 4.25) * Tsym */
    uint32_t total_us  = t_pre_us + n_payload * tsym_us;
    return (total_us + 999U) / 1000U;
}

uint32_t sx1276_random(void)
{
    uint32_t r = 0;
    if (!s_present) {
        return 0x1234567U;
    }
    antsw_rx();
    set_mode(MODE_RX_CONT);
    for (int i = 0; i < 32; i++) {
        hal_delay_us(100);
        r = (r << 1) | (rd(REG_RSSI_WIDEBAND) & 0x01U);
    }
    set_mode(MODE_STDBY);
    antsw_off();
    return r;
}
