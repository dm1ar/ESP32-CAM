#include "camera.hpp"
#include "board.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace cam {
namespace {
constexpr char tag[]{"camera"};

[[nodiscard]] framesize_t driverResolution(Resolution value) noexcept
{
    switch (value) {
    case Resolution::qvga: return FRAMESIZE_QVGA;
    case Resolution::vga: return FRAMESIZE_VGA;
    case Resolution::svga: return FRAMESIZE_SVGA;
    case Resolution::xga: return FRAMESIZE_XGA;
    case Resolution::uxga: return FRAMESIZE_UXGA;
    }
    return FRAMESIZE_INVALID;
}

[[nodiscard]] esp_err_t powerDown() noexcept
{
    const auto direction = gpio_set_direction(board::powerDown, GPIO_MODE_OUTPUT);
    // Attempt the level change even if direction configuration failed.
    const auto level = gpio_set_level(board::powerDown, 1U);
    return (direction != ESP_OK) ? direction : level;
}
} // namespace

esp_err_t Camera::sleep() noexcept
{
    esp_err_t result{ESP_OK};
    if (initialized_) {
        result = esp_camera_deinit(); // Releases buffers, capture task and XCLK.
        initialized_ = false;
    }
    const auto powerResult = powerDown();
    return (result != ESP_OK) ? result : powerResult;
}

esp_err_t Camera::prepare(const CameraSettings& settings) noexcept
{
    if (!settings.valid()) {
        return ESP_ERR_INVALID_ARG;
    }
    if (matches(settings)) {
        return ESP_OK;
    }
    // The caller has returned all frame leases before reaching this boundary.
    auto result = sleep();
    if (result != ESP_OK) {
        return result;
    }
    auto config = board::cameraConfig();
    config.frame_size = driverResolution(settings.resolution);
    config.jpeg_quality = static_cast<int>(settings.quality); // Validated 0..63.
    constexpr std::uint32_t maxAttempts{3U};
    for (std::uint32_t attempt{1U}; attempt <= maxAttempts; ++attempt) {
        result = powerDown();
        if (result != ESP_OK) {
            return result;
        }
        // A complete PWDN interval recovers transient sensor I2C/PLL state.
        vTaskDelay(pdMS_TO_TICKS(100U));
        result = esp_camera_init(&config);
        if (result == ESP_OK) {
            initialized_ = true;
            applied_ = settings;
            ESP_LOGI(tag, "Camera awake; configured XCLK %d Hz, JPEG quality %u",
                     config.xclk_freq_hz, static_cast<unsigned>(settings.quality));
            return ESP_OK;
        }
        // Driver 2.1.7 cleans up a failed initialization. Assert PWDN on the
        // final failure too; never leave the sensor awake after an error.
        ESP_LOGW(tag, "Initialization attempt %u/3: %s",
                 static_cast<unsigned>(attempt), esp_err_to_name(result));
    }
    const auto cleanup = powerDown();
    if (cleanup != ESP_OK) {
        ESP_LOGE(tag, "Power-down failed: %s", esp_err_to_name(cleanup));
    }
    return result;
}

std::uint32_t Camera::sensorPid() const noexcept
{
    const auto* const sensor = esp_camera_sensor_get();
    return (sensor != nullptr) ? sensor->id.PID : 0U;
}
} // namespace cam
