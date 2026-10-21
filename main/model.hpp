/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef ESP32_CAM_MODEL_HPP
#define ESP32_CAM_MODEL_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cam {

// This module has no ESP-IDF dependencies. Its validation and measurement
// contracts can therefore be exercised on the host without camera hardware.
enum class Resolution : std::uint8_t { qvga, vga, svga, xga, uxga };
enum class CameraState : std::uint8_t { standby, waking, streaming, error };

struct ResolutionInfo final {
    Resolution value;
    std::string_view name;
    std::uint32_t width;
    std::uint32_t height;
};

inline constexpr std::array<ResolutionInfo, 5U> resolutions{{
    {Resolution::qvga, "QVGA", 320U, 240U},
    {Resolution::vga, "VGA", 640U, 480U},
    {Resolution::svga, "SVGA", 800U, 600U},
    {Resolution::xga, "XGA", 1024U, 768U},
    {Resolution::uxga, "UXGA", 1600U, 1200U}
}};

[[nodiscard]] constexpr const ResolutionInfo* findResolution(Resolution value) noexcept
{
    for (const auto& option : resolutions) {
        if (option.value == value) {
            return &option;
        }
    }
    return nullptr;
}

[[nodiscard]] constexpr const char* stateName(CameraState state) noexcept
{
    switch (state) {
    case CameraState::standby: return "standby";
    case CameraState::waking: return "waking";
    case CameraState::streaming: return "streaming";
    case CameraState::error: return "error";
    }
    return "error";
}

struct CameraSettings final {
    Resolution resolution{Resolution::vga};
    std::uint32_t quality{12U};
    [[nodiscard]] bool operator==(const CameraSettings&) const noexcept = default;
    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return (findResolution(resolution) != nullptr) && (quality <= 63U);
    }
};

// Accept exactly the two browser form fields, in either order. No decoding is
// necessary: every allowed value is plain ASCII. Reject duplicates, unknown
// keys, embedded NULs, signs and trailing junk rather than partially applying
// a malformed request. On failure the caller's settings remain untouched.
[[nodiscard]] inline bool parseControl(std::string_view body, CameraSettings& result) noexcept
{
    CameraSettings parsed{};
    bool haveResolution{false};
    bool haveQuality{false};
    while (!body.empty()) {
        const auto separator = body.find('&');
        const auto field = body.substr(0U, separator);
        const auto equals = field.find('=');
        if (equals == std::string_view::npos) {
            return false;
        }
        const auto key = field.substr(0U, equals);
        const auto value = field.substr(equals + 1U);
        if ((key == "resolution") && !haveResolution) {
            for (const auto& option : resolutions) {
                if (value == option.name) {
                    parsed.resolution = option.value;
                    haveResolution = true;
                }
            }
            if (!haveResolution) {
                return false;
            }
        } else if ((key == "quality") && !haveQuality) {
            if (value.empty() || (value.size() > 2U)) {
                return false;
            }
            std::uint32_t number{0U};
            for (const char digit : value) {
                if ((digit < '0') || (digit > '9')) {
                    return false;
                }
                number = (number * 10U) + static_cast<std::uint32_t>(digit - '0');
            }
            if (number > 63U) {
                return false;
            }
            parsed.quality = number;
            haveQuality = true;
        } else {
            return false;
        }
        if (separator == std::string_view::npos) {
            body = {};
        } else {
            body.remove_prefix(separator + 1U);
            if (body.empty()) {
                return false;
            }
        }
    }
    if (!haveResolution || !haveQuality) {
        return false;
    }
    result = parsed;
    return true;
}

struct Rates final {
    float fps{0.0F};
    float kibPerSecond{0.0F};
};

// One limit per HTTP stream session, measured by the device's monotonic clock.
// Applying settings resets rate measurement, never this session deadline.
inline constexpr std::int64_t microsecondsPerSecond{1'000'000};
struct StreamPolicy final {
    bool autoOff{true};
    std::uint32_t limitSeconds{20U * 60U};
    [[nodiscard]] constexpr std::int64_t limitUs() const noexcept
    {
        return static_cast<std::int64_t>(limitSeconds) * microsecondsPerSecond;
    }
};

// Browser policy updates are RAM-only and bounded to the displayed slider.
[[nodiscard]] inline bool parseAutoOff(std::string_view body, StreamPolicy& result) noexcept
{
    StreamPolicy parsed{};
    bool enabled{false};
    bool duration{false};
    while (!body.empty()) {
        const auto separator = body.find('&');
        const auto field = body.substr(0U, separator);
        const auto equals = field.find('=');
        if (equals == std::string_view::npos) { return false; }
        const auto key = field.substr(0U, equals);
        const auto value = field.substr(equals + 1U);
        if ((key == "auto_off") && !enabled) {
            if ((value != "true") && (value != "false")) { return false; }
            parsed.autoOff = value == "true";
            enabled = true;
        } else if ((key == "auto_off_minutes") && !duration) {
            if (value.empty() || (value.size() > 3U)) { return false; }
            std::uint32_t minutes{0U};
            for (const char digit : value) {
                if ((digit < '0') || (digit > '9')) { return false; }
                minutes = (minutes * 10U) + static_cast<std::uint32_t>(digit - '0');
            }
            if ((minutes < 5U) || (minutes > 120U)) { return false; }
            parsed.limitSeconds = minutes * 60U;
            duration = true;
        } else { return false; }
        if (separator == std::string_view::npos) { body = {}; }
        else {
            body.remove_prefix(separator + 1U);
            if (body.empty()) { return false; }
        }
    }
    if (!enabled || !duration) { return false; }
    result = parsed;
    return true;
}

// Owned by the stream task. Only complete, successfully transmitted frames
// enter the one-second window; browser rendering never contributes to FPS.
class RateMeter final {
public:
    void reset(std::int64_t now) noexcept;
    [[nodiscard]] Rates sent(std::int64_t now, std::size_t bytes) noexcept;
private:
    std::int64_t startUs_{0};
    std::uint64_t frames_{0U};
    std::uint64_t bytes_{0U};
    Rates rates_{};
};

struct StreamSnapshot final {
    StreamPolicy policy{};
    CameraSettings selected{};
    CameraSettings applied{};
    bool hasApplied{false};
    std::uint32_t width{640U};
    std::uint32_t height{480U};
    std::size_t frameBytes{0U};
    std::uint64_t framesSent{0U};
    std::uint32_t captureErrors{0U};
    std::int32_t cameraError{0};
    std::uint32_t sensorPid{0U};
    Rates rates{};
    std::int64_t lastFrameUs{0};
    std::int64_t streamStartedUs{0};
    std::int64_t streamDeadlineUs{0};
    CameraState cameraState{CameraState::standby};
    bool streaming{false};
    bool stopRequested{false};
    bool streamTimedOut{false};
};

[[nodiscard]] inline bool streamExpired(const StreamSnapshot& snapshot, std::int64_t now) noexcept
{
    return snapshot.policy.autoOff && snapshot.streaming && (now >= snapshot.streamDeadlineUs);
}

[[nodiscard]] inline std::uint32_t streamRemainingSeconds(
    const StreamSnapshot& snapshot, std::int64_t now) noexcept
{
    if (!snapshot.policy.autoOff || !snapshot.streaming || streamExpired(snapshot, now)) {
        return 0U;
    }
    // Round upwards, so 00:00 means expired rather than the final partial
    // second. A slightly older status timestamp cannot exceed the full limit.
    const auto remaining = snapshot.streamDeadlineUs - now;
    if (remaining >= snapshot.policy.limitUs()) {
        return snapshot.policy.limitSeconds;
    }
    return static_cast<std::uint32_t>(
        (remaining + microsecondsPerSecond - 1) / microsecondsPerSecond);
}

// Apply expiry to a copy outside the critical section. Taking a snapshot must
// never require a slow network/driver operation while holding the spinlock.
inline void expireRates(StreamSnapshot& snapshot, std::int64_t now) noexcept
{
    constexpr std::int64_t expiryUs{3'000'000};
    if (!snapshot.streaming || ((now - snapshot.lastFrameUs) >= expiryUs)) {
        snapshot.rates = {};
    }
}

} // namespace cam
#endif
