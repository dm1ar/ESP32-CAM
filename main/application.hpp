#ifndef ESP32_CAM_APPLICATION_HPP
#define ESP32_CAM_APPLICATION_HPP

#include "camera.hpp"
#include "network.hpp"
#include "shared_state.hpp"
#include "web_server.hpp"

namespace cam {
// One application has static lifetime. Member declaration order guarantees
// dependencies exist before WebServer and all callback references stay valid.
class Application final {
public:
    Application() noexcept : web_{frames_, state_} {}
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    [[nodiscard]] esp_err_t run() noexcept;
private:
    Network network_{};
    Camera camera_{};
    SharedState state_{Network::streamPolicy()};
    FrameService frames_{camera_, state_};
    WebServer web_;
};
} // namespace cam
#endif
