/*
 * main.c - Test LoRa giua 2 mach RAK811 (WisNode-LoRa).
 *
 * Mot firmware chung cho ca 2 node. Che do chon bang phim qua cong COM (115200 8N1),
 * duoc luu vao EEPROM (giu sau khi mat dien):
 *   a = AUTO     : tu dong ping-pong. Ca 2 node bat dau la MASTER (gui PING);
 *                  node nao nghe duoc PING truoc se tu chuyen thanh SLAVE (tra PONG).  [mac dinh]
 *   m = MASTER   : chi gui PING va cho PONG (do RTT, ty le thanh cong, RSSI/SNR 2 chieu)
 *   s = SLAVE    : chi nghe PING va tra PONG
 *   t = TX-only  : chuyen truyen goi DATA dinh ky
 *   r = RX-only  : chuyen nhan, in RSSI/SNR, dem goi mat (theo so thu tu)
 *   i = thong ke, c = xoa thong ke, h = tro giup
 */
#include "board.h"
#include "hal.h"
#include "sx1276.h"
#include "lora_config.h"

/* ---------------- Goi tin (16 byte) ----------------
 * [0..1] 'R''K'  [2] loai  [3..4] src  [5..6] dst  [7..10] seq
 * [11..12] RSSI do duoc (int16)  [13] SNR x4 (int8)  [14..15] du tru
 */
#define PKT_LEN     16U
#define PKT_PING    1U
#define PKT_PONG    2U
#define PKT_DATA    3U
#define ID_BROADCAST 0xFFFFU

typedef struct {
    uint8_t  type;
    uint16_t src;
    uint16_t dst;
    uint32_t seq;
    int16_t  rssi;
    int8_t   snr_x4;
} pkt_t;

static void pkt_encode(const pkt_t *p, uint8_t *b)
{
    b[0] = 'R'; b[1] = 'K'; b[2] = p->type;
    b[3] = (uint8_t)p->src;  b[4] = (uint8_t)(p->src >> 8);
    b[5] = (uint8_t)p->dst;  b[6] = (uint8_t)(p->dst >> 8);
    b[7] = (uint8_t)p->seq;  b[8] = (uint8_t)(p->seq >> 8);
    b[9] = (uint8_t)(p->seq >> 16); b[10] = (uint8_t)(p->seq >> 24);
    b[11] = (uint8_t)p->rssi; b[12] = (uint8_t)((uint16_t)p->rssi >> 8);
    b[13] = (uint8_t)p->snr_x4;
    b[14] = 0; b[15] = 0;
}

static bool pkt_decode(const uint8_t *b, int len, pkt_t *p)
{
    if (len < (int)PKT_LEN || b[0] != 'R' || b[1] != 'K') {
        return false;
    }
    p->type   = b[2];
    p->src    = (uint16_t)(b[3] | (b[4] << 8));
    p->dst    = (uint16_t)(b[5] | (b[6] << 8));
    p->seq    = (uint32_t)b[7] | ((uint32_t)b[8] << 8) | ((uint32_t)b[9] << 16) | ((uint32_t)b[10] << 24);
    p->rssi   = (int16_t)(b[11] | (b[12] << 8));
    p->snr_x4 = (int8_t)b[13];
    return p->type >= PKT_PING && p->type <= PKT_DATA;
}

/* ---------------- Trang thai ung dung ---------------- */

typedef enum { MODE_AUTO = 0, MODE_MASTER, MODE_SLAVE, MODE_TX, MODE_RX, MODE_COUNT } app_mode_t;
static const char *const MODE_NAME[MODE_COUNT] = {
    "AUTO ping-pong", "MASTER (gui PING)", "SLAVE (tra PONG)", "TX-only (chuyen truyen)", "RX-only (chuyen nhan)"
};

#define EE_MODE_ADDR   0U
#define EE_MAGIC       0xA5C30000UL

static app_mode_t s_mode;
static bool       s_is_master;       /* vai tro hien tai (AUTO/MASTER/SLAVE) */
static uint16_t   s_id;
static uint32_t   s_rng;

static uint32_t   s_seq;
static bool       s_waiting;
static uint32_t   s_wait_seq;
static uint32_t   s_tx_start;
static uint32_t   s_deadline;
static uint32_t   s_next_tx;
static uint32_t   s_last_ping_rx;

/* Thong ke */
static uint32_t   st_sent, st_ok, st_timeout, st_crc, st_rx;
static uint32_t   st_rx_lost, st_last_rx_seq;
static uint16_t   st_last_rx_src;
static bool       st_have_last;
static int32_t    st_rssi_min = 0, st_rssi_max = -200;

static uint32_t rnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

static void put_id(uint16_t id)
{
    uart_put_hex8((uint8_t)(id >> 8));
    uart_put_hex8((uint8_t)id);
}

static void put_sig(int16_t rssi, int16_t snr_x4)
{
    uart_puts("RSSI ");
    uart_put_i32(rssi);
    uart_puts(" dBm, SNR ");
    uart_put_centi((int32_t)snr_x4 * 25);
    uart_puts(" dB");
}

static void track_rssi(int16_t rssi)
{
    if (rssi < st_rssi_min) { st_rssi_min = rssi; }
    if (rssi > st_rssi_max) { st_rssi_max = rssi; }
}

static void put_time(void)
{
    uint32_t ms = hal_millis();
    uart_puts("[");
    uart_put_u32(ms / 1000U);
    uart_putc('.');
    uart_putc((char)('0' + (ms / 100U) % 10U));
    uart_puts(" s] ");
}

static void stats_clear(void)
{
    st_sent = st_ok = st_timeout = st_crc = st_rx = st_rx_lost = 0;
    st_have_last = false;
    st_rssi_min = 0;
    st_rssi_max = -200;
}

static void print_stats(void)
{
    uart_puts("--- Thong ke ---\n  Gui: ");
    uart_put_u32(st_sent);
    uart_puts("  PONG OK: ");
    uart_put_u32(st_ok);
    uart_puts("  Timeout: ");
    uart_put_u32(st_timeout);
    if (st_sent > 0U) {
        uart_puts("  Thanh cong: ");
        uart_put_centi((int32_t)((st_ok * 10000U) / st_sent));
        uart_puts(" %");
    }
    uart_puts("\n  Nhan: ");
    uart_put_u32(st_rx);
    uart_puts("  Mat (theo seq): ");
    uart_put_u32(st_rx_lost);
    uart_puts("  Loi CRC: ");
    uart_put_u32(st_crc);
    if (st_rssi_max > -200) {
        uart_puts("  RSSI min/max: ");
        uart_put_i32(st_rssi_min);
        uart_puts("/");
        uart_put_i32(st_rssi_max);
        uart_puts(" dBm");
    }
    uart_puts("\n");
}

static void print_help(void)
{
    uart_puts("Lenh: a=AUTO  m=MASTER  s=SLAVE  t=TX-only  r=RX-only  i=thong ke  c=xoa thong ke  h=tro giup\n");
}

static void enter_mode(app_mode_t m, bool save)
{
    s_mode = m;
    s_is_master = (m == MODE_AUTO || m == MODE_MASTER);
    s_waiting = false;
    s_seq = 0;
    s_next_tx = hal_millis() + 500U + (rnd() % 1000U);   /* lech pha ngau nhien giua 2 node */
    s_last_ping_rx = hal_millis();
    stats_clear();
    if (save) {
        (void)hal_eeprom_write(EE_MODE_ADDR, EE_MAGIC | (uint32_t)m);
    }
    uart_puts(">>> Che do: ");
    uart_puts(MODE_NAME[m]);
    uart_puts("\n");
    sx1276_rx_start();
}

static void handle_key(int c)
{
    switch (c) {
    case 'a': case 'A': enter_mode(MODE_AUTO, true);   break;
    case 'm': case 'M': enter_mode(MODE_MASTER, true); break;
    case 's': case 'S': enter_mode(MODE_SLAVE, true);  break;
    case 't': case 'T': enter_mode(MODE_TX, true);     break;
    case 'r': case 'R': enter_mode(MODE_RX, true);     break;
    case 'i': case 'I': print_stats();                 break;
    case 'c': case 'C': stats_clear(); uart_puts("Da xoa thong ke\n"); break;
    case 'h': case 'H': case '?': print_help();        break;
    default: break;
    }
}

static void send_pkt(uint8_t type, uint16_t dst, uint32_t seq, int16_t rssi, int8_t snr_x4)
{
    uint8_t buf[PKT_LEN];
    pkt_t p = { type, s_id, dst, seq, rssi, snr_x4 };
    pkt_encode(&p, buf);
    if (hal_millis() < APP_TX_HOLDOFF_MS) {
        return;                                /* an toan: chua phat trong 10 s dau */
    }
    if (!sx1276_send(buf, PKT_LEN)) {
        put_time();
        uart_puts("LOI: phat khong xong (TxDone timeout)\n");
    }
}

/* ---------------- Xu ly goi nhan duoc ---------------- */

static void on_packet(const pkt_t *p, const sx1276_pkt_info_t *info)
{
    uint32_t now = hal_millis();

    if (p->src == s_id) {
        return;                                    /* goi cua chinh minh (khong the, nhung cu chan) */
    }
    if (p->dst != ID_BROADCAST && p->dst != s_id) {
        return;                                    /* khong phai gui cho minh */
    }
    st_rx++;
    track_rssi(info->rssi_dbm);

    switch (p->type) {
    case PKT_PING:
        if (s_mode == MODE_TX) {
            return;
        }
        if (s_mode == MODE_RX || s_mode == MODE_MASTER) {
            put_time();
            uart_puts("Nghe PING #");
            uart_put_u32(p->seq);
            uart_puts(" tu ");
            put_id(p->src);
            uart_puts(" | ");
            put_sig(info->rssi_dbm, info->snr_x4);
            uart_puts(s_mode == MODE_MASTER ? " (dang la MASTER -> khong tra loi)\n" : "\n");
            return;
        }
        if (s_mode == MODE_AUTO && s_is_master) {
            s_is_master = false;
            s_waiting = false;
            put_time();
            uart_puts(">>> Nghe thay node ");
            put_id(p->src);
            uart_puts(" -> chuyen sang SLAVE\n");
        }
        /* Tra PONG ngay, kem chat luong song chieu di ma minh do duoc */
        send_pkt(PKT_PONG, p->src, p->seq, info->rssi_dbm, (int8_t)info->snr_x4);
        sx1276_rx_start();
        s_last_ping_rx = now;
        put_time();
        uart_puts("PING #");
        uart_put_u32(p->seq);
        uart_puts(" tu ");
        put_id(p->src);
        uart_puts(" | ");
        put_sig(info->rssi_dbm, info->snr_x4);
        uart_puts(" -> da tra PONG\n");
        break;

    case PKT_PONG:
        if (!s_is_master || !s_waiting || p->seq != s_wait_seq) {
            return;
        }
        s_waiting = false;
        st_ok++;
        put_time();
        uart_puts("PONG #");
        uart_put_u32(p->seq);
        uart_puts(" tu ");
        put_id(p->src);
        uart_puts(" | RTT ");
        uart_put_u32(now - s_tx_start);
        uart_puts(" ms\n    chieu di  (slave do): ");
        put_sig(p->rssi, p->snr_x4);
        uart_puts("\n    chieu ve  (master do): ");
        put_sig(info->rssi_dbm, info->snr_x4);
        uart_puts("\n    thanh cong ");
        uart_put_u32(st_ok);
        uart_puts("/");
        uart_put_u32(st_sent);
        uart_puts(" (");
        uart_put_centi((int32_t)((st_ok * 10000U) / st_sent));
        uart_puts(" %)\n");
        break;

    case PKT_DATA:
        if (s_mode != MODE_RX && !(s_mode == MODE_AUTO && !s_is_master) && s_mode != MODE_SLAVE) {
            return;
        }
        if (st_have_last && p->src == st_last_rx_src && p->seq > st_last_rx_seq + 1U) {
            st_rx_lost += p->seq - st_last_rx_seq - 1U;
        }
        st_have_last = true;
        st_last_rx_src = p->src;
        st_last_rx_seq = p->seq;
        put_time();
        uart_puts("DATA #");
        uart_put_u32(p->seq);
        uart_puts(" tu ");
        put_id(p->src);
        uart_puts(" | ");
        put_sig(info->rssi_dbm, info->snr_x4);
        uart_puts(" | nhan ");
        uart_put_u32(st_rx);
        uart_puts(", mat ");
        uart_put_u32(st_rx_lost);
        uart_puts("\n");
        break;

    default:
        break;
    }
}

/* ---------------- Chuong trinh chinh ---------------- */

static void print_banner(const char *rst, uint8_t ver)
{
    uart_puts("\n\n=== Test LoRa ping-pong - RAK811 / WisNode-LoRa ===\n");
    uart_puts("Phien ban : " BOARD_VARIANT_NAME "\n");
    uart_puts("Reset do  : ");
    uart_puts(rst);
    uart_puts("\nNode ID   : ");
    put_id(s_id);
    uart_puts("\nSX1276    : ");
    if (sx1276_variant_mismatch()) {
        uart_puts("!!!!! NAP NHAM FILE: module la ban ");
        uart_puts(sx1276_detected_variant() == 2 ? "L (433/470)" : "H (868/915/923)");
        uart_puts(" nhung firmware la " BOARD_VARIANT_NAME "\n!!!!! RADIO DA BI KHOA (khong phat) de bao ve tang phat. Nap dung file!\n");
    } else if (ver == 0x12U) {
        uart_puts(sx1276_detected_variant() == 0 ? "OK (RegVersion=0x12) - CANH BAO: khong tu nhan dien duoc ban H/L\n"
                                                 : "OK (RegVersion=0x12), ban module khop firmware\n");
    } else {
        uart_puts("LOI - khong doc duoc (RegVersion=0x");
        uart_put_hex8(ver);
        uart_puts(", can 0x12). Kiem tra dung file H/L!\n");
    }
    uart_puts("Song      : ");
    uart_put_u32(LORA_FREQ_HZ / 1000000UL);
    uart_putc('.');
    {
        uint32_t khz = (LORA_FREQ_HZ / 1000UL) % 1000UL;
        uart_putc((char)('0' + khz / 100U));
        uart_putc((char)('0' + (khz / 10U) % 10U));
        uart_putc((char)('0' + khz % 10U));
    }
    uart_puts(" MHz, SF");
    uart_put_u32(LORA_SF);
    uart_puts(", BW ");
    uart_put_u32(LORA_BW_KHZ);
    uart_puts(" kHz, CR 4/");
    uart_put_u32(LORA_CR + 4U);
    uart_puts(", ");
    uart_put_i32(LORA_TX_POWER_DBM);
    uart_puts(" dBm, sync 0x");
    uart_put_hex8(LORA_SYNC_WORD);
    uart_puts("\nAirtime   : ");
    uart_put_u32(sx1276_airtime_ms(PKT_LEN));
    uart_puts(" ms / goi ");
    uart_put_u32(PKT_LEN);
    uart_puts(" byte\n");
    uart_puts("Cong suat: ");
    uart_put_i32(LORA_TX_POWER_DBM);
    uart_puts(" dBm. Bat dau phat sau 10 s - DAM BAO DA GAN ANTEN DUNG BANG TAN!\n");
    print_help();
}

int main(void)
{
    static const sx1276_config_t cfg = {
        .freq_hz      = LORA_FREQ_HZ,
        .tx_power_dbm = LORA_TX_POWER_DBM,
        .sf           = LORA_SF,
        .bw_khz       = LORA_BW_KHZ,
        .cr           = LORA_CR,
        .preamble     = LORA_PREAMBLE_LEN,
        .sync_word    = LORA_SYNC_WORD,
    };
    uint8_t rxbuf[64];
    uint8_t ver;
    uint32_t last_retry = 0;

    hal_clock_init();
    uart_init(LOG_BAUDRATE);
    const char *rst = hal_reset_cause();
    hal_iwdg_start();

    s_id = hal_node_id();
    ver  = sx1276_init(&cfg);
    s_rng = sx1276_random() ^ ((uint32_t)s_id << 16) ^ s_id ^ SysTick->VAL;
    if (s_rng == 0U) { s_rng = 0x9E3779B9UL; }

    print_banner(rst, ver);

    {
        uint32_t ee = hal_eeprom_read(EE_MODE_ADDR);
        app_mode_t m = MODE_RX;          /* AN TOAN: board moi khoi dong o CHI THU (khong phat). Go a/m/s/t de phat */
        if ((ee & 0xFFFF0000UL) == EE_MAGIC && (ee & 0xFFU) < MODE_COUNT) {
            m = (app_mode_t)(ee & 0xFFU);
        }
        enter_mode(m, false);
    }

    for (;;) {
        uint32_t now = hal_millis();
        sx1276_pkt_info_t info;
        pkt_t p;
        int c, n;

        hal_iwdg_kick();

        c = uart_getc();
        if (c >= 0) {
            handle_key(c);
        }

        /* Radio chua san sang (vd nap nham ban H/L): thu khoi tao lai moi 3 s */
        if (!sx1276_present()) {
            if (now - last_retry >= 3000U) {
                last_retry = now;
                ver = sx1276_init(&cfg);
                put_time();
                if (sx1276_present()) {
                    uart_puts("SX1276 da san sang\n");
                    sx1276_rx_start();
                } else {
                    uart_puts("SX1276 van khong tra loi (RegVersion=0x");
                    uart_put_hex8(ver);
                    uart_puts(")\n");
                }
            }
            continue;
        }

        /* Nhan */
        n = sx1276_rx_poll(rxbuf, sizeof rxbuf, &info);
        if (n < 0) {
            st_crc++;
            put_time();
            uart_puts("Goi loi CRC | ");
            put_sig(info.rssi_dbm, info.snr_x4);
            uart_puts("\n");
        } else if (n > 0 && pkt_decode(rxbuf, n, &p)) {
            on_packet(&p, &info);
        }

        now = hal_millis();

        /* MASTER: het thoi gian cho PONG */
        if (s_is_master && s_waiting && (int32_t)(now - s_deadline) >= 0) {
            s_waiting = false;
            st_timeout++;
            put_time();
            uart_puts("PING #");
            uart_put_u32(s_wait_seq);
            uart_puts(" -> khong co PONG (timeout) | thanh cong ");
            uart_put_u32(st_ok);
            uart_puts("/");
            uart_put_u32(st_sent);
            uart_puts("\n");
        }

        /* MASTER: den luc gui PING */
        if ((s_mode == MODE_AUTO || s_mode == MODE_MASTER) && s_is_master && !s_waiting &&
            (int32_t)(now - s_next_tx) >= 0) {
            uint32_t air = sx1276_airtime_ms(PKT_LEN);
            s_seq++;
            st_sent++;
            s_tx_start = hal_millis();
            send_pkt(PKT_PING, ID_BROADCAST, s_seq, 0, 0);
            sx1276_rx_start();
            s_waiting = true;
            s_wait_seq = s_seq;
            /* PONG ve sau ~1 airtime + thoi gian xu ly cua slave */
            s_deadline = hal_millis() + air + 300U;
            s_next_tx  = s_tx_start + APP_PERIOD_MS + (rnd() % 500U);
            put_time();
            uart_puts("Gui PING #");
            uart_put_u32(s_seq);
            uart_puts("\n");
        }

        /* AUTO: slave lau khong thay PING -> quay ve master */
        if (s_mode == MODE_AUTO && !s_is_master && (now - s_last_ping_rx) >= APP_SLAVE_IDLE_MS) {
            s_is_master = true;
            s_next_tx = now + (rnd() % 1000U);
            put_time();
            uart_puts(">>> Khong con nghe PING -> quay lai MASTER\n");
        }

        /* TX-only */
        if (s_mode == MODE_TX && (int32_t)(now - s_next_tx) >= 0) {
            s_seq++;
            st_sent++;
            s_next_tx = now + APP_PERIOD_MS;
            send_pkt(PKT_DATA, ID_BROADCAST, s_seq, 0, 0);
            sx1276_rx_start();
            put_time();
            uart_puts("Gui DATA #");
            uart_put_u32(s_seq);
            uart_puts(" (");
            uart_put_u32(sx1276_airtime_ms(PKT_LEN));
            uart_puts(" ms)\n");
        }
    }
}
