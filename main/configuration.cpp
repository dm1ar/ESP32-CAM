/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "configuration.hpp"
#include "nvs.h"
#include "nvs_flash.h"

namespace cam {
namespace {
template<std::size_t Size>
[[nodiscard]] bool readString(nvs_handle_t handle, const char* key, std::array<char, Size>& value) noexcept
{
    auto size = value.size();
    if (nvs_get_str(handle, key, value.data(), &size) != ESP_OK) { return false; }
    return (size > 0U) && (size <= value.size()) && (value[size - 1U] == '\0')
        && (std::strlen(value.data()) + 1U == size);
}
}
bool Configuration::valid() const noexcept
{
    const auto network = ssid();
    const auto pass = password();
    const std::string_view name{nodeName()};
    if ((schema_ != 1U) || network.empty() || (network.size() > 32U)
        || (autoOff_ > 1U) || (minutes_ < 5U) || (minutes_ > 120U)
        || name.empty() || (name.size() > 32U)) { return false; }
    if (pass.size() == 64U) {
        for (const auto ch : pass) {
            if (!(((ch >= '0') && (ch <= '9')) || ((ch >= 'a') && (ch <= 'f'))
                || ((ch >= 'A') && (ch <= 'F')))) { return false; }
        }
    } else if ((pass.size() < 8U) || (pass.size() > 63U)) { return false; }
    for (const auto ch : name) {
        if (!(((ch >= 'a') && (ch <= 'z')) || ((ch >= 'A') && (ch <= 'Z'))
            || ((ch >= '0') && (ch <= '9')) || (ch == '-'))) { return false; }
    }
    return (name.front() != '-') && (name.back() != '-');
}
esp_err_t Configuration::load() noexcept
{
    // Do not erase on error: a missing, corrupt or newer config needs explicit
    // re-provisioning, and must not silently destroy existing device settings.
    if (nvs_flash_init_partition("config") != ESP_OK) { return ESP_ERR_NOT_FOUND; }
    nvs_handle_t handle{};
    if (nvs_open_from_partition("config", "camera", NVS_READONLY, &handle) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    Configuration candidate{};
    const bool loaded = (nvs_get_u32(handle, "schema", &candidate.schema_) == ESP_OK)
        && readString(handle, "ssid", candidate.ssid_)
        && readString(handle, "password", candidate.password_)
        && readString(handle, "node_name", candidate.node_)
        && (nvs_get_u8(handle, "auto_off", &candidate.autoOff_) == ESP_OK)
        && (nvs_get_u32(handle, "minutes", &candidate.minutes_) == ESP_OK);
    nvs_close(handle);
    if (!loaded || !candidate.valid()) { return ESP_ERR_NOT_FOUND; }
    *this = candidate;
    return ESP_OK;
}
} // namespace cam
