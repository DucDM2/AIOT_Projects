#ifndef TESTS_H
#define TESTS_H

#include <stdbool.h>

void run_gpio_drv_tests(void);
void run_relay_tests(void);
void run_button_led_tests(void);
void run_cmd_parser_tests(void);
void run_status_json_tests(void);
void run_scenario_tests(void);

/** Kịch bản tích hợp; verbose=true in từng lần truy cập thanh ghi. */
void scenario_full_device(bool verbose);

#endif /* TESTS_H */
