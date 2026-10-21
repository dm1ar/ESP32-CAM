/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef ESP32_CAM_CONFIGURATION_HPP
#define ESP32_CAM_CONFIGURATION_HPP
#include <array>
#include <cstring>
#include <string_view>
#include "esp_err.h"
#include "model.hpp"

namespace cam {
// Loaded once before service startup. Buffers and borrowed pointers then remain
// unchanged for the lifetime of Application. No embedded credential fallback.
class Configuration final {
public:
    [[nodiscard]] esp_err_t load() noexcept;
    [[nodiscard]] const char* nodeName() const noexcept { return node_.data(); }
    [[nodiscard]] StreamPolicy policy() const noexcept { return {autoOff_ != 0U, minutes_ * 60U}; }
    [[nodiscard]] std::string_view ssid() const noexcept { return ssid_.data(); }
    [[nodiscard]] std::string_view password() const noexcept { return password_.data(); }
private:
    [[nodiscard]] bool valid() const noexcept;
    std::array<char, 33U> ssid_{};
    std::array<char, 65U> password_{};
    std::array<char, 33U> node_{"ESP32-CAM01"};
    std::uint32_t schema_{0U};
    std::uint32_t minutes_{20U};
    std::uint8_t autoOff_{1U};
};
} // namespace cam
#endif
