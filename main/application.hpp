/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef ESP32_CAM_APPLICATION_HPP
#define ESP32_CAM_APPLICATION_HPP

#include "camera.hpp"
#include "network.hpp"
#include "shared_state.hpp"
#include "web_server.hpp"
#include "rtsp_server.hpp"
#include "webrtc_server.hpp"

namespace cam {
// One application has static lifetime. Member declaration order guarantees
// dependencies exist before WebServer and all callback references stay valid.
class Application final {
public:
    Application() noexcept : web_{frames_, state_, webrtc_, settings_.nodeName()} {}
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    [[nodiscard]] esp_err_t run() noexcept;
private:
    Configuration settings_{};
    Network network_{settings_};
    Camera camera_{};
    SharedState state_{};
    FrameService frames_{camera_, state_};
    WebRtcServer webrtc_{frames_, state_};
    WebServer web_;
    RtspServer rtsp_{frames_, state_, 554U, settings_.nodeName()};
};
} // namespace cam
#endif
