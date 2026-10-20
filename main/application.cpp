#include "application.hpp"
#include "board.hpp"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"

namespace cam {
esp_err_t Application::run() noexcept
{
    // Latch the lamp off before enabling its output, preventing a bright flash.
    auto result = gpio_set_level(board::flashLamp, 0U);
    if (result != ESP_OK) { return result; }
    result = gpio_set_direction(board::flashLamp, GPIO_MODE_OUTPUT);
    if (result != ESP_OK) { return result; }
    // Boot has exclusive ownership until the capture worker is started.
    result = camera_.sleep();
    if (result != ESP_OK) { return result; }
    esp_chip_info_t chip{};
    esp_chip_info(&chip);
    ESP_LOGI("app", "ESP-IDF %s, %u cores, revision %u", esp_get_idf_version(),
             static_cast<unsigned>(chip.cores), static_cast<unsigned>(chip.revision));
    if (!esp_psram_is_initialized()) {
        ESP_LOGE("app", "PSRAM unavailable; camera capture cannot start");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI("app", "PSRAM: %zu bytes", esp_psram_get_size());
    result = network_.start();
    if (result != ESP_OK) { return result; }
    result = frames_.start();
    if (result != ESP_OK) { return result; }
    result = web_.start();
    if (result == ESP_OK) {
        network_.logUrl();
    }
    return result;
}
} // namespace cam
