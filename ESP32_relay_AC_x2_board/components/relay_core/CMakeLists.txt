# Lõi relay_core: thuần C99, không phụ thuộc ESP-IDF (trừ port/esp32/reg_io_esp32.c).
# Cùng mã nguồn này được build trên máy tính trong tests/host với bộ mô phỏng thanh ghi.
idf_component_register(
    SRCS
        "src/rc_types.c"
        "src/gpio_drv.c"
        "src/relay.c"
        "src/button.c"
        "src/led_pattern.c"
        "src/str_buf.c"
        "src/status_json.c"
        "src/cmd_parser.c"
        "port/esp32/reg_io_esp32.c"
    INCLUDE_DIRS "include"
)

# Cảnh báo nghiêm ngặt (hỗ trợ MISRA chương 10 – essential type).
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow
    -Wstrict-prototypes -Wmissing-prototypes -Werror)
