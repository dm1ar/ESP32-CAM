#ifndef TEST_ESP_CAMERA_H
#define TEST_ESP_CAMERA_H
#include <cstddef>
#include <cstdint>
#include "esp_err.h"
enum framesize_t { FRAMESIZE_QVGA, FRAMESIZE_VGA, FRAMESIZE_SVGA, FRAMESIZE_XGA, FRAMESIZE_UXGA, FRAMESIZE_INVALID };
enum pixformat_t { PIXFORMAT_JPEG };
enum { LEDC_TIMER_0, LEDC_CHANNEL_0, CAMERA_FB_IN_PSRAM, CAMERA_GRAB_LATEST };
struct camera_config_t {
    int pin_pwdn{}, pin_reset{}, pin_xclk{}, pin_sccb_sda{}, pin_sccb_scl{};
    int pin_d0{}, pin_d1{}, pin_d2{}, pin_d3{}, pin_d4{}, pin_d5{}, pin_d6{}, pin_d7{};
    int pin_vsync{}, pin_href{}, pin_pclk{}, xclk_freq_hz{}, ledc_timer{}, ledc_channel{};
    pixformat_t pixel_format{};
    framesize_t frame_size{};
    int jpeg_quality{};
    std::size_t fb_count{};
    int fb_location{}, grab_mode{};
};
struct camera_fb_t { std::uint8_t* buf{}; std::size_t len{}, width{}, height{}; pixformat_t format{}; };
struct sensor_t { struct { std::uint16_t PID{}; } id; };
esp_err_t esp_camera_init(const camera_config_t* config);
esp_err_t esp_camera_deinit();
camera_fb_t* esp_camera_fb_get();
void esp_camera_fb_return(camera_fb_t* frame);
sensor_t* esp_camera_sensor_get();
#endif
