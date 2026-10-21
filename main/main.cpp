/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "application.hpp"
#include "esp_err.h"
#include "esp_log.h"

// The only C-linkage application entry point is required by ESP-IDF. Static
// storage outlives app_main's task and every network/HTTP callback it registers.
extern "C" void app_main(void);
extern "C" void app_main(void)
{
    static cam::Application application{};
    // Failure to establish the application is fatal; request/camera failures
    // are recoverable error returns and never use ESP_ERROR_CHECK.
    const auto result = application.run();
    if (result == ESP_ERR_NOT_FOUND) {
        ESP_LOGE("app", "Settings missing or invalid. Flash private config.bin at 0x190000, then reset.");
        return; // Stay idle rather than reboot-loop on an unconfigured device.
    }
    ESP_ERROR_CHECK(result);
}
