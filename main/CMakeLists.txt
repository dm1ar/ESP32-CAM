idf_component_register(SRCS "main.cpp" "application.cpp" "network.cpp" "configuration.cpp"
    "camera.cpp" "frame_store.cpp" "frame_service.cpp" "shared_state.cpp" "model.cpp" "web_server.cpp" "rtsp_protocol.cpp" "rtsp_server.cpp" "webrtc_server.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_psram esp_driver_gpio esp_wifi esp_event esp_netif nvs_flash esp_http_server esp_timer lwip esp_hw_support
    EMBED_TXTFILES "index.html" "webrtc.html")

# Do not silently fall back to an older dialect. The v6.1 ESP32 toolchain
# supports GNU++26; all first-party firmware sources use this exact mode.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=gnu++26 -Wall -Wextra -Wconversion -Wsign-conversion
    -Wshadow -Wformat=2 -Werror -fno-exceptions -fno-rtti)
