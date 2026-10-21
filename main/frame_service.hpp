#ifndef ESP32_CAM_FRAME_SERVICE_HPP
#define ESP32_CAM_FRAME_SERVICE_HPP

#include "frame_store.hpp"
#include "freertos/queue.h"
#include "freertos/semphr.h"

namespace cam {
// Page HTTP, stream HTTP and RTSP callers share one camera owner.
// Each queued stack request stays alive until the worker signals completion.
class FrameService final {
public:
    FrameService(Camera& camera, SharedState& state) noexcept : store_{camera, state}, state_{state} {}
    FrameService(const FrameService&) = delete;
    FrameService& operator=(const FrameService&) = delete;
    [[nodiscard]] esp_err_t start() noexcept;
    [[nodiscard]] esp_err_t capture(bool snapshot) noexcept;
    [[nodiscard]] esp_err_t begin() noexcept;
    [[nodiscard]] esp_err_t finish(bool failed) noexcept;
    [[nodiscard]] JpegView streamView() const noexcept { return store_.streamView(); }
    [[nodiscard]] JpegView snapshotView() const noexcept { return store_.snapshotView(); }
private:
    enum class Operation : std::uint8_t { stream, snapshot, begin, finish, finishFailed };
    struct Request final {
        Operation operation;
        SemaphoreHandle_t done;
        esp_err_t result{ESP_FAIL};
    };
    [[nodiscard]] esp_err_t submit(Operation operation) noexcept;
    static void entry(void* context) noexcept;
    void run() noexcept;
    FrameStore store_;
    SharedState& state_;
    StaticQueue_t queueStorage_{};
    std::array<std::uint8_t, 2U * sizeof(Request*)> queueBytes_{};
    QueueHandle_t queue_{nullptr};
};
} // namespace cam
#endif
