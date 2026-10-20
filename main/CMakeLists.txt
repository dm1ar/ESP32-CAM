set(wifi_file "${CMAKE_CURRENT_LIST_DIR}/../wifi.toml")
set(wifi_generator "${CMAKE_CURRENT_LIST_DIR}/../tools/wifi_config.py")
set(wifi_header_dir "${CMAKE_BINARY_DIR}/private_config")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${wifi_file}" "${wifi_generator}")
execute_process(
    COMMAND "${PYTHON}" "${wifi_generator}" "${wifi_file}" "${wifi_header_dir}/wifi_config.h"
    RESULT_VARIABLE wifi_result
)
if(NOT wifi_result EQUAL 0)
    message(FATAL_ERROR "Wi-Fi configuration is missing or invalid. Edit wifi.toml before building.")
endif()

idf_component_register(SRCS "main.cpp" "application.cpp" "network.cpp"
    "camera.cpp" "shared_state.cpp" "model.cpp" "web_server.cpp"
    INCLUDE_DIRS "." "${wifi_header_dir}"
    PRIV_REQUIRES esp_psram esp_driver_gpio esp_wifi esp_event esp_netif nvs_flash esp_http_server esp_timer
    EMBED_TXTFILES "index.html")

# Do not silently fall back to an older dialect. The v6.1 ESP32 toolchain
# supports GNU++26; all first-party firmware sources use this exact mode.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=gnu++26 -Wall -Wextra -Wconversion -Wsign-conversion
    -Wshadow -Wformat=2 -Werror -fno-exceptions -fno-rtti)
