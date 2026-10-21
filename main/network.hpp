/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef ESP32_CAM_NETWORK_HPP
#define ESP32_CAM_NETWORK_HPP

#include <cstdint>
#include "configuration.hpp"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

namespace cam {
// Application-lifetime object: event callbacks retain its address until reset.
// The event group uses static storage; driver allocations stay inside ESP-IDF.
class Network final {
public:
    explicit Network(const Configuration& settings) noexcept : settings_{settings} {}
    Network(const Network&) = delete;
    Network& operator=(const Network&) = delete;
    [[nodiscard]] esp_err_t start() noexcept;
    void logUrl() const noexcept;
    [[nodiscard]] const char* nodeName() const noexcept;
    [[nodiscard]] static esp_err_t setStreamingPower(bool streaming) noexcept;
private:
    static void onEvent(void* context, esp_event_base_t base,
                        std::int32_t id, void* data) noexcept;
    const Configuration& settings_;
    StaticEventGroup_t eventStorage_{};
    EventGroupHandle_t connected_{nullptr};
    esp_netif_t* station_{nullptr};
};
} // namespace cam
#endif
