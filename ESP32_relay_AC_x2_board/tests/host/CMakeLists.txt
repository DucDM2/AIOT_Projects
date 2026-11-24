# Build & chạy unit test trên máy tính (không cần ESP32):
#   cmake -S tests/host -B tests/host/build -G Ninja
#   cmake --build tests/host/build
#   ctest --test-dir tests/host/build --output-on-failure
cmake_minimum_required(VERSION 3.16)
project(relay_core_host_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CORE ${CMAKE_CURRENT_LIST_DIR}/../../components/relay_core)

# Lõi firmware – build y nguyên như trên ESP32, cảnh báo nghiêm ngặt và -Werror.
add_library(relay_core STATIC
    ${CORE}/src/rc_types.c
    ${CORE}/src/gpio_drv.c
    ${CORE}/src/relay.c
    ${CORE}/src/button.c
    ${CORE}/src/led_pattern.c
    ${CORE}/src/str_buf.c
    ${CORE}/src/status_json.c
    ${CORE}/src/cmd_parser.c
)
target_include_directories(relay_core PUBLIC ${CORE}/include)
target_compile_options(relay_core PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
    -Wstrict-prototypes -Wmissing-prototypes -Wcast-qual -Wundef -Wswitch-default -Werror)

# Bộ mô phỏng thanh ghi thay cho port/esp32/reg_io_esp32.c
add_executable(relay_tests
    sim/reg_io_sim.c
    test_main.c
    test_gpio_drv.c
    test_relay.c
    test_button_led.c
    test_cmd_parser.c
    test_status_json.c
    test_scenario.c
)
target_include_directories(relay_tests PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/sim)
target_link_libraries(relay_tests PRIVATE relay_core)
target_compile_options(relay_tests PRIVATE -Wall -Wextra)

enable_testing()
add_test(NAME relay_core_unit_tests COMMAND relay_tests)
add_test(NAME register_trace_scenario COMMAND relay_tests --trace)
