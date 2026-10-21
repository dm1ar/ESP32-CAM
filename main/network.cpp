/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "network.hpp"
#include <cstring>
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

namespace cam {
namespace {
constexpr char tag[]{"network"};
constexpr EventBits_t connectedBit{BIT0};
}

const char* Network::nodeName() const noexcept
{
    return settings_.nodeName();
}

void Network::logUrl() const noexcept
{
    esp_netif_ip_info_t info{};
    if ((station_ != nullptr) && (esp_netif_get_ip_info(station_, &info) == ESP_OK)
        && (info.ip.addr != 0U)) {
        ESP_LOGI(tag, "Node %s, browser URL: http://" IPSTR "/", nodeName(), IP2STR(&info.ip));
        ESP_LOGI(tag, "RTSP stream: rtsp://" IPSTR ":554/stream", IP2STR(&info.ip));
        ESP_LOGI(tag, "MJPEG stream: http://" IPSTR ":81/stream", IP2STR(&info.ip));
    }
}

void Network::onEvent(void* context, esp_event_base_t base,
                      std::int32_t id, void* data) noexcept
{
    // ESP-IDF owns these opaque pointers. Registration guarantees the context
    // type and object lifetime; check null before crossing that C API boundary.
    if (context == nullptr) {
        return;
    }
    auto& self = *static_cast<Network*>(context);
    if ((base == WIFI_EVENT) && (id == WIFI_EVENT_STA_DISCONNECTED)) {
        static_cast<void>(xEventGroupClearBits(self.connected_, connectedBit));
        if (data != nullptr) {
            const auto* const event = static_cast<const wifi_event_sta_disconnected_t*>(data);
            ESP_LOGW(tag, "Disconnected (reason %u); reconnecting", static_cast<unsigned>(event->reason));
        }
    }
    if ((base == WIFI_EVENT)
        && ((id == WIFI_EVENT_STA_START) || (id == WIFI_EVENT_STA_DISCONNECTED))) {
        const auto result = esp_wifi_connect();
        if (result != ESP_OK) {
            ESP_LOGE(tag, "Wi-Fi connect failed: %s", esp_err_to_name(result));
        }
    } else if ((base == IP_EVENT) && (id == IP_EVENT_STA_GOT_IP)) {
        self.logUrl();
        static_cast<void>(xEventGroupSetBits(self.connected_, connectedBit));
    }
}

esp_err_t Network::setStreamingPower(bool streaming) noexcept
{
    return esp_wifi_set_ps(streaming ? WIFI_PS_NONE : WIFI_PS_MIN_MODEM);
}

esp_err_t Network::start() noexcept
{
    auto result = nvs_flash_init();
    if ((result == ESP_ERR_NVS_NO_FREE_PAGES) || (result == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        result = nvs_flash_erase();
        if (result == ESP_OK) {
            result = nvs_flash_init();
        }
    }
    if (result != ESP_OK) { return result; }
    connected_ = xEventGroupCreateStatic(&eventStorage_);
    if (connected_ == nullptr) { return ESP_ERR_NO_MEM; }
    result = esp_netif_init();
    if (result != ESP_OK) { return result; }
    result = esp_event_loop_create_default();
    if (result != ESP_OK) { return result; }
    station_ = esp_netif_create_default_wifi_sta();
    if (station_ == nullptr) { return ESP_ERR_NO_MEM; }
    // Set before Wi-Fi/DHCP starts so routers can identify separate cameras.
    result = esp_netif_set_hostname(station_, nodeName());
    if (result != ESP_OK) { return result; }
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&init);
    if (result != ESP_OK) { return result; }
    // RAM storage prevents the Wi-Fi driver persisting credentials in NVS.
    result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (result != ESP_OK) { return result; }
    result = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &Network::onEvent, this);
    if (result != ESP_OK) { return result; }
    result = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &Network::onEvent, this);
    if (result != ESP_OK) { return result; }
    wifi_config_t config{};
    // NVS values were checked before startup, including full-width SSID/PSK.
    const auto ssid = settings_.ssid();
    const auto password = settings_.password();
    std::memcpy(config.sta.ssid, ssid.data(), ssid.size());
    std::memcpy(config.sta.password, password.data(), password.size());
    config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    result = esp_wifi_set_mode(WIFI_MODE_STA);
    if (result != ESP_OK) { return result; }
    result = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (result != ESP_OK) { return result; }
    result = esp_wifi_start();
    if (result != ESP_OK) { return result; }
    result = setStreamingPower(false);
    if (result != ESP_OK) { return result; }
    ESP_LOGI(tag, "Connecting to configured 2.4 GHz network");
    while ((xEventGroupWaitBits(connected_, connectedBit, pdFALSE, pdTRUE,
                               pdMS_TO_TICKS(30'000U)) & connectedBit) == 0U) {
        ESP_LOGW(tag, "Waiting for Wi-Fi/DHCP; check credentials and coverage");
    }
    return ESP_OK;
}
} // namespace cam
