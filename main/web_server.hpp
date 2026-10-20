#ifndef ESP32_CAM_WEB_SERVER_HPP
#define ESP32_CAM_WEB_SERVER_HPP

#include "camera.hpp"
#include "esp_http_server.h"
#include "shared_state.hpp"

namespace cam {
// The two SDK server tasks borrow this application-lifetime object. Camera
// access is confined to stream(); all other handlers use locked snapshots.
class WebServer final {
public:
    WebServer(Camera& camera, SharedState& state) noexcept : camera_{camera}, state_{state} {}
    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;
    [[nodiscard]] esp_err_t start() noexcept;
private:
    template<esp_err_t (WebServer::*Handler)(httpd_req_t*) noexcept>
    static esp_err_t bridge(httpd_req_t* request) noexcept
    {
        if ((request == nullptr) || (request->user_ctx == nullptr)) {
            return ESP_ERR_INVALID_ARG;
        }
        // user_ctx was registered as this; ESP-IDF preserves it unchanged.
        auto& self = *static_cast<WebServer*>(request->user_ctx);
        return (self.*Handler)(request);
    }
    [[nodiscard]] esp_err_t index(httpd_req_t* request) noexcept;
    [[nodiscard]] esp_err_t health(httpd_req_t* request) noexcept;
    [[nodiscard]] esp_err_t status(httpd_req_t* request) noexcept;
    [[nodiscard]] esp_err_t control(httpd_req_t* request) noexcept;
    [[nodiscard]] esp_err_t stop(httpd_req_t* request) noexcept;
    [[nodiscard]] esp_err_t stream(httpd_req_t* request) noexcept;
    [[nodiscard]] esp_err_t sendFrame(httpd_req_t* request, RateMeter& meter) noexcept;
    Camera& camera_;
    SharedState& state_;
    httpd_handle_t pageServer_{nullptr};
    httpd_handle_t streamServer_{nullptr};
};
} // namespace cam
#endif
