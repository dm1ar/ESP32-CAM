#ifndef ESP32_CAM_NETWORK_HPP
#define ESP32_CAM_NETWORK_HPP

#include <cstdint>
#include "model.hpp"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

namespace cam {
// Application-lifetime object: event callbacks retain its address until reset.
// The event group uses static storage; driver allocations stay inside ESP-IDF.
class Network final {
public:
    Network() noexcept = default;
    Network(const Network&) = delete;
    Network& operator=(const Network&) = delete;
    [[nodiscard]] esp_err_t start() noexcept;
    void logUrl() const noexcept;
    [[nodiscard]] static const char* nodeName() noexcept;
    [[nodiscard]] static StreamPolicy streamPolicy() noexcept;
    [[nodiscard]] static esp_err_t setStreamingPower(bool streaming) noexcept;
private:
    static void onEvent(void* context, esp_event_base_t base,
                        std::int32_t id, void* data) noexcept;
    StaticEventGroup_t eventStorage_{};
    EventGroupHandle_t connected_{nullptr};
    esp_netif_t* station_{nullptr};
};
} // namespace cam
#endif
