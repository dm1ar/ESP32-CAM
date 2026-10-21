/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef ESP32_CAM_FRAME_STORE_HPP
#define ESP32_CAM_FRAME_STORE_HPP

#include "camera.hpp"
#include "shared_state.hpp"

namespace cam {
struct JpegView final {
    const std::uint8_t* data{nullptr};
    std::size_t size{0U};
    std::uint32_t width{0U};
    std::uint32_t height{0U};
};

// D09: bounded PSRAM allocation at startup only; never allocate per request.
class JpegBuffer final {
public:
    static constexpr std::size_t capacity{512U * 1024U};
    JpegBuffer() noexcept = default;
    ~JpegBuffer() noexcept;
    JpegBuffer(const JpegBuffer&) = delete;
    JpegBuffer& operator=(const JpegBuffer&) = delete;
    [[nodiscard]] bool allocate() noexcept;
    [[nodiscard]] bool assign(JpegView frame) noexcept;
    [[nodiscard]] JpegView view() const noexcept { return view_; }
    void clear() noexcept { view_ = {}; }
private:
    std::uint8_t* storage_{nullptr}; // Sole owner; released in destructor.
    JpegView view_{};
};

// Only the capture worker calls these methods after startup. HTTP tasks read
// their own output buffer until the next request from that same HTTP task.
class FrameStore final {
public:
    FrameStore(Camera& camera, SharedState& state) noexcept : camera_{camera}, state_{state} {}
    FrameStore(const FrameStore&) = delete;
    FrameStore& operator=(const FrameStore&) = delete;
    [[nodiscard]] esp_err_t start() noexcept;
    [[nodiscard]] esp_err_t capture(bool snapshot) noexcept;
    [[nodiscard]] esp_err_t sleep() noexcept;
    [[nodiscard]] JpegView streamView() const noexcept { return stream_.view(); }
    [[nodiscard]] JpegView snapshotView() const noexcept { return snapshot_.view(); }
private:
    [[nodiscard]] esp_err_t fresh(const CameraSettings& settings) noexcept;
    Camera& camera_;
    SharedState& state_;
    JpegBuffer latest_{};
    JpegBuffer stream_{};
    JpegBuffer snapshot_{};
    CameraSettings cachedSettings_{};
};
} // namespace cam
#endif
