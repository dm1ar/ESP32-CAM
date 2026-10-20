#ifndef ESP32_CAM_BOARD_HPP
#define ESP32_CAM_BOARD_HPP

#include "driver/gpio.h"
#include "esp_camera.h"

namespace cam::board {
// AI-Thinker pin assignment from assets/ESP32-Cam_datasheet.pdf.
// GPIO0 is XCLK at runtime: remove the download-mode jumper before reset.
inline constexpr gpio_num_t powerDown{GPIO_NUM_32};
inline constexpr gpio_num_t flashLamp{GPIO_NUM_4}; // Also used by SD data.

[[nodiscard]] inline camera_config_t cameraConfig() noexcept
{
    camera_config_t config{};
    config.pin_pwdn = powerDown;
    config.pin_reset = -1; // This board does not expose a separate sensor reset.
    config.pin_xclk = 0;
    config.pin_sccb_sda = 26;
    config.pin_sccb_scl = 27;
    config.pin_d0 = 5;
    config.pin_d1 = 18;
    config.pin_d2 = 19;
    config.pin_d3 = 21;
    config.pin_d4 = 36;
    config.pin_d5 = 39;
    config.pin_d6 = 34;
    config.pin_d7 = 35;
    config.pin_vsync = 25;
    config.pin_href = 23;
    config.pin_pclk = 22;
    // Conservative OV2640 input clock for the sensor-write timing trial.
    // Remain within the camera driver's supported 10/20 MHz configurations;
    // actual transmitted FPS continues to be measured rather than assumed.
    config.xclk_freq_hz = 10'000'000;
    config.ledc_timer = LEDC_TIMER_0;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count = 2U;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;
    return config;
}
} // namespace cam::board
#endif
