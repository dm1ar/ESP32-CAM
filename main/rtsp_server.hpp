#ifndef ESP32_CAM_RTSP_SERVER_HPP
#define ESP32_CAM_RTSP_SERVER_HPP
#include "frame_service.hpp"

namespace cam {
class RtspServer final {
public:
    RtspServer(FrameService& frames, SharedState& state, std::uint16_t port = 554U) noexcept
        : frames_{frames}, state_{state}, port_{port} {}
    RtspServer(const RtspServer&) = delete;
    RtspServer& operator=(const RtspServer&) = delete;
    [[nodiscard]] esp_err_t start() noexcept;
private:
    static void entry(void* context) noexcept;
    void run() noexcept;
    FrameService& frames_;
    SharedState& state_;
    std::uint16_t port_;
    int listener_{-1};
};
} // namespace cam
#endif
