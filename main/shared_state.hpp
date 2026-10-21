/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef ESP32_CAM_SHARED_STATE_HPP
#define ESP32_CAM_SHARED_STATE_HPP

#include "freertos/FreeRTOS.h"
#include "model.hpp"

namespace cam {
// FreeRTOS spinlocks are SDK boundary types. RAII keeps all exit paths balanced.
class CriticalSection final {
public:
    explicit CriticalSection(portMUX_TYPE& lock) noexcept : lock_{lock}
    {
        portENTER_CRITICAL(&lock_);
    }
    ~CriticalSection() noexcept { portEXIT_CRITICAL(&lock_); }
    CriticalSection(const CriticalSection&) = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;
private:
    portMUX_TYPE& lock_;
};

// The page and stream servers run in different tasks. Every access to data_
// goes through this object; no caller can retain a reference to mutable state.
class SharedState final {
public:
    // Compiled TOML defaults at boot; browser policy updates are RAM-only.
    // Camera settings never change the deadline; policy uses original start.
    explicit SharedState(StreamPolicy policy = {}) noexcept { data_.policy = policy; }
    SharedState(const SharedState&) = delete;
    SharedState& operator=(const SharedState&) = delete;
    [[nodiscard]] StreamSnapshot snapshot() const noexcept;
    [[nodiscard]] bool select(const CameraSettings& settings) noexcept;
    [[nodiscard]] bool setPolicy(StreamPolicy policy, std::int64_t now) noexcept;
    void requestStop() noexcept;
    [[nodiscard]] bool shouldStop(std::int64_t now) noexcept;
    void begin(std::int64_t now) noexcept;
    void waking() noexcept;
    void ready(const CameraSettings& applied, std::uint32_t pid) noexcept;
    void sent(std::uint32_t width, std::uint32_t height, std::size_t bytes,
              std::int64_t now, Rates rates) noexcept;
    void captureFailed() noexcept;
    void cameraFailed(std::int32_t error) noexcept;
    void finish(bool failed) noexcept;
private:
    mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
    StreamSnapshot data_{};
};
} // namespace cam
#endif
