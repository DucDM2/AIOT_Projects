/* Kiểm thử chống dội phím và kiểu nháy LED. */
#include "ut.h"
#include "tests.h"
#include "button.h"
#include "led_pattern.h"

#define DEB   (30UL)
#define LONG  (1500UL)

/* Giữ mức @p pressed từ t0 đến t1 (bước 10 ms), đếm sự kiện. */
static void feed(button_t *b, bool pressed, uint32_t t0, uint32_t t1, int *shorts, int *longs)
{
    uint32_t t;
    for (t = t0; t < t1; t += 10UL)
    {
        const button_event_t e = button_update(b, pressed, t);
        if (e == BUTTON_EVT_SHORT) { (*shorts)++; }
        if (e == BUTTON_EVT_LONG)  { (*longs)++; }
    }
}

static void test_button_short_press(void)
{
    button_t b;
    int s = 0, l = 0;
    UT_EQ_INT(button_init(&b, DEB, LONG, false, 0), RC_OK);
    feed(&b, false, 0, 100, &s, &l);
    feed(&b, true, 100, 300, &s, &l);   /* giữ 200 ms */
    UT_EQ_INT(s, 0);                    /* nhấn ngắn chỉ báo khi nhả */
    feed(&b, false, 300, 400, &s, &l);
    UT_EQ_INT(s, 1);
    UT_EQ_INT(l, 0);
}

static void test_button_long_press(void)
{
    button_t b;
    int s = 0, l = 0;
    UT_EQ_INT(button_init(&b, DEB, LONG, false, 0), RC_OK);
    feed(&b, true, 0, 1400, &s, &l);
    UT_EQ_INT(l, 0);
    feed(&b, true, 1400, 3000, &s, &l); /* qua 1500 ms */
    UT_EQ_INT(l, 1);                    /* chỉ một lần dù giữ tiếp */
    feed(&b, false, 3000, 3100, &s, &l);
    UT_EQ_INT(s, 0);                    /* nhả sau nhấn giữ không sinh SHORT */
}

static void test_button_bounce_ignored(void)
{
    button_t b;
    int s = 0, l = 0;
    uint32_t t;
    UT_EQ_INT(button_init(&b, DEB, LONG, false, 0), RC_OK);
    /* Dội 20 ms nhanh hơn thời gian chống dội → không có sự kiện. */
    for (t = 0; t < 500; t += 20UL)
    {
        feed(&b, ((t / 20UL) % 2UL) == 0UL, t, t + 20UL, &s, &l);
    }
    UT_EQ_INT(s, 0);
    UT_EQ_INT(l, 0);
    /* Nhấn có dội ở mép rồi ổn định → đúng một SHORT. */
    feed(&b, false, 500, 600, &s, &l);
    feed(&b, true, 600, 610, &s, &l);
    feed(&b, false, 610, 620, &s, &l);
    feed(&b, true, 620, 800, &s, &l);
    feed(&b, false, 800, 900, &s, &l);
    UT_EQ_INT(s, 1);
}

static void test_button_held_at_boot(void)
{
    button_t b;
    int s = 0, l = 0;
    UT_EQ_INT(button_init(&b, DEB, LONG, true, 0), RC_OK); /* giữ IO0 lúc cấp nguồn */
    feed(&b, true, 0, 3000, &s, &l);
    feed(&b, false, 3000, 3100, &s, &l);
    UT_EQ_INT(s, 0);
    UT_EQ_INT(l, 0);
    feed(&b, true, 3100, 3200, &s, &l);   /* lần nhấn tiếp theo hoạt động bình thường */
    feed(&b, false, 3200, 3300, &s, &l);
    UT_EQ_INT(s, 1);
}

static void test_button_timer_wrap(void)
{
    button_t b;
    int s = 0, l = 0;
    const uint32_t t0 = 0xFFFFFF00UL;
    UT_EQ_INT(button_init(&b, DEB, LONG, false, t0), RC_OK);
    feed(&b, true, t0, t0 + 0x200UL, &s, &l); /* tràn qua 0 trong lúc giữ (wrap có chủ đích) */
    UT_EQ_INT(l, 0);
    UT_EQ_INT(s, 0);
}

static void test_button_init_args(void)
{
    button_t b;
    UT_EQ_INT(button_init(NULL, DEB, LONG, false, 0), RC_ERR_ARG);
    UT_EQ_INT(button_init(&b, LONG, DEB, false, 0), RC_ERR_ARG); /* long ≤ debounce */
    UT_EQ_INT(button_update(NULL, true, 0), BUTTON_EVT_NONE);
}

static void test_led_patterns(void)
{
    UT_CHECK(!led_pattern_is_on(LED_MODE_OFF, 0));
    UT_CHECK(led_pattern_is_on(LED_MODE_ON, 12345));
    UT_CHECK(led_pattern_is_on(LED_MODE_BLINK_FAST, 0));
    UT_CHECK(led_pattern_is_on(LED_MODE_BLINK_FAST, 99));
    UT_CHECK(!led_pattern_is_on(LED_MODE_BLINK_FAST, 100));
    UT_CHECK(led_pattern_is_on(LED_MODE_BLINK_FAST, 200));
    UT_CHECK(led_pattern_is_on(LED_MODE_BLINK_SLOW, 499));
    UT_CHECK(!led_pattern_is_on(LED_MODE_BLINK_SLOW, 500));
    UT_CHECK(!led_pattern_is_on((led_mode_t)99, 0));
}

void run_button_led_tests(void)
{
    UT_RUN(test_button_short_press);
    UT_RUN(test_button_long_press);
    UT_RUN(test_button_bounce_ignored);
    UT_RUN(test_button_held_at_boot);
    UT_RUN(test_button_timer_wrap);
    UT_RUN(test_button_init_args);
    UT_RUN(test_led_patterns);
}
