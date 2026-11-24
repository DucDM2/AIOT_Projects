/*
 * Chạy toàn bộ unit test trên máy tính:
 *   relay_tests            chạy test
 *   relay_tests --trace    in nhật ký thanh ghi của kịch bản thiết bị đầy đủ
 */
#include "ut.h"
#include "tests.h"
#include "gpio_drv.h"
#include "reg_io_sim.h"

int ut_checks = 0;
int ut_failures = 0;

void ut_run(const char *name, ut_fn_t fn)
{
    const int before = ut_failures;
    sim_reset();
    gpio_drv_init();
    fn();
    printf("%s %s\n", (ut_failures == before) ? "[  OK  ]" : "[ FAIL ]", name);
}

int main(int argc, char **argv)
{
    if ((argc > 1) && (strcmp(argv[1], "--trace") == 0))
    {
        sim_reset();
        scenario_full_device(true);
        printf("\n%s (%d kiem tra, %d loi)\n", (ut_failures == 0) ? "KICH BAN DAT" : "KICH BAN LOI",
               ut_checks, ut_failures);
        return (ut_failures == 0) ? 0 : 1;
    }

    printf("== gpio_drv (muc thanh ghi) ==\n");
    run_gpio_drv_tests();
    printf("== relay ==\n");
    run_relay_tests();
    printf("== button / led ==\n");
    run_button_led_tests();
    printf("== cmd_parser ==\n");
    run_cmd_parser_tests();
    printf("== status_json ==\n");
    run_status_json_tests();
    printf("== kich ban thiet bi day du ==\n");
    run_scenario_tests();

    printf("\nTong: %d kiem tra, %d loi -> %s\n", ut_checks, ut_failures,
           (ut_failures == 0) ? "TAT CA DAT" : "CO LOI");
    return (ut_failures == 0) ? 0 : 1;
}
