#ifndef ESP32_CAM_CAMERA_HPP
#define ESP32_CAM_CAMERA_HPP

#include "esp_camera.h"
#include "model.hpp"

namespace cam {
// A borrowed driver frame has exactly one return operation. A lease cannot be
// copied or moved and must leave scope before reinitializing/deinitializing.
class FrameLease final {
public:
    FrameLease() noexcept : frame_{esp_camera_fb_get()} {}
    ~FrameLease() noexcept
    {
        if (frame_ != nullptr) {
            esp_camera_fb_return(frame_);
        }
    }
    FrameLease(const FrameLease&) = delete;
    FrameLease& operator=(const FrameLease&) = delete;
    [[nodiscard]] const camera_fb_t* get() const noexcept { return frame_; }
private:
    camera_fb_t* const frame_;
};

// After boot, only the capture worker may access this object or camera APIs.
// The page task submits settings through SharedState instead of touching GPIO.
class Camera final {
public:
    Camera() noexcept = default;
    Camera(const Camera&) = delete;
    Camera& operator=(const Camera&) = delete;
    [[nodiscard]] esp_err_t sleep() noexcept;
    [[nodiscard]] esp_err_t prepare(const CameraSettings& settings) noexcept;
    [[nodiscard]] bool matches(const CameraSettings& settings) const noexcept
    {
        return initialized_ && (applied_ == settings);
    }
    [[nodiscard]] std::uint32_t sensorPid() const noexcept;
private:
    CameraSettings applied_{};
    bool initialized_{false};
};
} // namespace cam
#endif
