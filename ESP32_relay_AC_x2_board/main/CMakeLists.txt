idf_component_register(
    SRCS
        "main.c"
        "app_ctrl.c"
        "app_wifi.c"
        "app_http.c"
        "app_mqtt.c"
        "app_io.c"
    INCLUDE_DIRS "."
    REQUIRES relay_core nvs_flash esp_wifi esp_netif esp_event esp_http_server
             mqtt mbedtls esp_timer freertos log
    EMBED_TXTFILES "web/index.html"
)

# Tạo app_secrets.h từ mẫu nếu chưa có (file thật nằm trong .gitignore).
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/app_secrets.h")
    configure_file("${CMAKE_CURRENT_LIST_DIR}/app_secrets.example.h"
                   "${CMAKE_CURRENT_LIST_DIR}/app_secrets.h" COPYONLY)
    message(WARNING "Da tao main/app_secrets.h tu mau – hay dien Wi-Fi/EMQX vao file nay.")
endif()

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wshadow -Werror)
