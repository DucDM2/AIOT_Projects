/*
 * main.c - Test cam bien cua MC-38 NC tren RAK811 (WisNode-LoRa).
 * Log qua USART1 (CH340 -> USB) 115200 8N1. Phim lenh:
 *   t = bai test co huong dan (DONG -> MO -> DONG), ket luan DAT / KHONG DAT
 *   i = trang thai + thong ke, c = xoa thong ke, h = tro giup
 */
#include "board.h"
#include "hal.h"
#include "door.h"

/* ---------------- Thong ke ---------------- */
static uint32_t st_open_count;
static uint32_t st_open_total_ms;
static uint32_t st_open_max_ms;
static uint32_t st_alarm_count;

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

static void put_ts(void)
{
    uart_putc('[');
    put_hms(hal_millis());
    uart_puts("] ");
}

static void put_secs(uint32_t ms)
{
    uart_put_u32(ms / 1000U);
    uart_putc('.');
    uart_putc((char)('0' + (ms / 100U) % 10U));
    uart_puts(" s");
}

static void print_help(void)
{
    uart_puts("Lenh: t=test co huong dan  i=trang thai  c=xoa thong ke  h=tro giup\n");
}

static void print_status(void)
{
    uint32_t now = hal_millis();
    put_ts();
    uart_puts("Trang thai: CUA ");
    uart_puts(door_state_name(door_state()));
    uart_puts(" (");
    put_secs(now - door_since_ms());
    uart_puts(") | chan " DOOR_PIN_NAME " = ");
    uart_putc(door_raw_level() ? '1' : '0');
    uart_puts(" | so lan mo: ");
    uart_put_u32(st_open_count);
    uart_puts("\n");
}

static void print_stats(void)
{
    print_status();
    uart_puts("  Tong thoi gian mo: ");
    put_secs(st_open_total_ms);
    uart_puts(" | lau nhat: ");
    put_secs(st_open_max_ms);
    uart_puts(" | canh bao: ");
    uart_put_u32(st_alarm_count);
    uart_puts(" | tong canh EXTI: ");
    uart_put_u32(door_edge_total());
    uart_puts("\n");
}

/* ---------------- Bai test co huong dan ---------------- */

typedef enum { T_IDLE = 0, T_CLOSE1, T_OPEN, T_CLOSE2 } test_step_t;

#define TEST_STEP_TIMEOUT_MS 20000U
#define TEST_HOLD_MS         1500U    /* trang thai dung phai giu on dinh it nhat 1.5 s */

static test_step_t s_test;
static uint32_t    s_test_step_start;
static uint32_t    s_test_bounces;
static bool        s_test_ok[3];

static const char *const TEST_PROMPT[4] = {
    "",
    "Buoc 1/3: DONG cua - dat nam cham SAT cong tac (khe < 10 mm), giu yen...",
    "Buoc 2/3: MO cua - keo nam cham ra XA (> 4 cm), giu yen...",
    "Buoc 3/3: DONG cua lai, giu yen...",
};

static void test_enter(test_step_t step)
{
    s_test = step;
    s_test_step_start = hal_millis();
    if (step != T_IDLE) {
        put_ts();
        uart_puts(TEST_PROMPT[step]);
        uart_puts(" (toi da 20 s)\n");
    }
}

static void test_start(void)
{
    s_test_ok[0] = s_test_ok[1] = s_test_ok[2] = false;
    s_test_bounces = 0;
    uart_puts("\n===== BAT DAU TEST CAM BIEN CUA MC-38 NC =====\n");
    test_enter(T_CLOSE1);
}

static void test_finish(void)
{
    bool pass = s_test_ok[0] && s_test_ok[1] && s_test_ok[2];
    uart_puts("===== KET QUA TEST =====\n");
    uart_puts("  Buoc 1 DONG: "); uart_puts(s_test_ok[0] ? "DAT\n" : "KHONG DAT\n");
    uart_puts("  Buoc 2 MO  : "); uart_puts(s_test_ok[1] ? "DAT\n" : "KHONG DAT\n");
    uart_puts("  Buoc 3 DONG: "); uart_puts(s_test_ok[2] ? "DAT\n" : "KHONG DAT\n");
    uart_puts("  So canh nhieu khi chuyen trang thai: ");
    uart_put_u32(s_test_bounces);
    uart_puts(s_test_bounces > 20U ? " (nhieu - kiem tra day/khe nam cham)\n" : " (binh thuong)\n");
    uart_puts(pass ? ">>> CAM BIEN HOAT DONG DUNG\n\n"
                   : ">>> KHONG DAT - xem muc Khac phuc su co trong README\n\n");
    s_test = T_IDLE;
}

/* Goi moi vong lap; evt = su kien vua xay ra (de cong don so canh nhieu) */
static void test_poll(door_event_t evt)
{
    uint32_t now = hal_millis();
    door_state_t want;
    int idx;

    if (s_test == T_IDLE) {
        return;
    }
    if (evt != DOOR_EVT_NONE) {
        s_test_bounces += door_last_bounces() > 1U ? door_last_bounces() - 1U : 0U;
    }

    want = (s_test == T_OPEN) ? DOOR_OPEN : DOOR_CLOSED;
    idx  = (int)s_test - 1;

    if (door_state() == want && (now - door_since_ms()) >= TEST_HOLD_MS &&
        (now - s_test_step_start) >= 300U) {
        s_test_ok[idx] = true;
        put_ts();
        uart_puts("  -> OK: doc duoc CUA ");
        uart_puts(door_state_name(want));
        uart_puts(" (chan = ");
        uart_putc(door_raw_level() ? '1' : '0');
        uart_puts(")\n");
    } else if ((now - s_test_step_start) >= TEST_STEP_TIMEOUT_MS) {
        put_ts();
        uart_puts("  -> HET GIO: van doc CUA ");
        uart_puts(door_state_name(door_state()));
        uart_puts(", can CUA ");
        uart_puts(door_state_name(want));
        uart_puts("\n");
    } else {
        return;
    }

    if (s_test == T_CLOSE2) {
        test_finish();
    } else {
        test_enter((test_step_t)(s_test + 1));
    }
}

/* ---------------- Chuong trinh chinh ---------------- */

int main(void)
{
    uint32_t last_status;
    uint32_t next_alarm;
    uint32_t open_start;
    bool alarm_active = false;

    hal_clock_init();
    uart_init(LOG_BAUDRATE);
    const char *rst = hal_reset_cause();
    hal_iwdg_start();
    door_init();

    uart_puts("\n\n=== Test cam bien cua MC-38 NC - RAK811 / WisNode-LoRa ===\n");
    uart_puts("Reset do  : ");
    uart_puts(rst);
    uart_puts("\nChan      : " DOOR_PIN_NAME ", pull-up noi; day con lai -> GND\n");
    uart_puts("Quy uoc   : chan=0 -> CUA DONG (nam cham sat) | chan=1 -> CUA MO / dut day\n");
    uart_puts("Chong doi : ");
    uart_put_u32(DOOR_DEBOUNCE_MS);
    uart_puts(" ms | canh bao mo qua ");
    uart_put_u32(DOOR_OPEN_ALARM_S);
    uart_puts(" s\n");
    print_help();
    put_ts();
    uart_puts("Trang thai ban dau: CUA ");
    uart_puts(door_state_name(door_state()));
    uart_puts("\n");
    /* Cua dang mo ngay khi khoi dong: van tinh gio de canh bao */
    open_start = door_since_ms();
    next_alarm = open_start + DOOR_OPEN_ALARM_S * 1000U;
    last_status = hal_millis();

    for (;;) {
        uint32_t now;
        door_event_t evt;
        int c;

        hal_iwdg_kick();

        c = uart_getc();
        switch (c) {
        case 't': case 'T': test_start();  break;
        case 'i': case 'I': print_stats(); break;
        case 'c': case 'C':
            st_open_count = st_open_total_ms = st_open_max_ms = st_alarm_count = 0;
            uart_puts("Da xoa thong ke\n");
            break;
        case 'h': case 'H': case '?': print_help(); break;
        default: break;
        }

        evt = door_poll();
        now = hal_millis();

        if (evt == DOOR_EVT_OPENED) {
            st_open_count++;
            alarm_active = false;
            open_start = door_since_ms();
            next_alarm = open_start + DOOR_OPEN_ALARM_S * 1000U;
            put_ts();
            uart_puts(">>> CUA MO   (lan thu ");
            uart_put_u32(st_open_count);
            uart_puts(") | so canh: ");
            uart_put_u32(door_last_bounces());
            uart_puts(door_last_bounces() <= 1U ? " (sach)\n" : " (co rung tiep diem)\n");
        } else if (evt == DOOR_EVT_CLOSED) {
            /* thoi gian mo = tu luc on dinh MO den luc on dinh DONG */
            uint32_t opened_ms = door_since_ms() - open_start;
            st_open_total_ms += opened_ms;
            if (opened_ms > st_open_max_ms) { st_open_max_ms = opened_ms; }
            put_ts();
            uart_puts(">>> CUA DONG            | da mo trong ");
            put_secs(opened_ms);
            uart_puts(" | so canh: ");
            uart_put_u32(door_last_bounces());
            uart_puts(door_last_bounces() <= 1U ? " (sach)\n" : " (co rung tiep diem)\n");
            if (alarm_active) {
                put_ts();
                uart_puts("    Het canh bao (cua da dong)\n");
            }
            alarm_active = false;
        }

        /* Canh bao cua mo qua lau */
        if (door_state() == DOOR_OPEN && (int32_t)(now - next_alarm) >= 0) {
            if (!alarm_active) {
                st_alarm_count++;
            }
            alarm_active = true;
            next_alarm = now + DOOR_ALARM_REPEAT_S * 1000U;
            put_ts();
            uart_puts("!!! CANH BAO: cua MO lien tuc ");
            put_secs(now - door_since_ms());
            uart_puts("\n");
        }

        test_poll(evt);

        if (s_test == T_IDLE && (now - last_status) >= STATUS_PERIOD_MS) {
            last_status = now;
            print_status();
        }
    }
}
