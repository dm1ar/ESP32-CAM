#include "shared_state.hpp"

namespace cam {
StreamSnapshot SharedState::snapshot() const noexcept
{
    const CriticalSection guard{lock_};
    return data_;
}

bool SharedState::select(const CameraSettings& settings) noexcept
{
    const auto* const info = findResolution(settings.resolution);
    if (!settings.valid() || (info == nullptr)) {
        return false;
    }
    const CriticalSection guard{lock_};
    data_.selected = settings;
    if (!data_.streaming) {
        data_.width = info->width;
        data_.height = info->height;
        data_.frameBytes = 0U;
    }
    return true;
}

void SharedState::requestStop() noexcept
{
    const CriticalSection guard{lock_};
    data_.stopRequested = true;
}

bool SharedState::shouldStop(std::int64_t now) noexcept
{
    const CriticalSection guard{lock_};
    if (!data_.stopRequested && streamExpired(data_, now)) {
        data_.streamTimedOut = true;
        data_.stopRequested = true;
    }
    return data_.stopRequested;
}

void SharedState::begin(std::int64_t now) noexcept
{
    const CriticalSection guard{lock_};
    data_.streaming = true;
    data_.stopRequested = false;
    data_.streamTimedOut = false;
    data_.cameraError = 0;
    data_.streamDeadlineUs = data_.policy.autoOff ? now + data_.policy.limitUs() : 0;
    data_.lastFrameUs = now;
    data_.rates = {};
}

void SharedState::waking() noexcept
{
    const CriticalSection guard{lock_};
    data_.cameraState = CameraState::waking;
    data_.rates = {};
}

void SharedState::ready(const CameraSettings& applied, std::uint32_t pid) noexcept
{
    const auto* const info = findResolution(applied.resolution);
    const CriticalSection guard{lock_};
    data_.applied = applied;
    data_.hasApplied = true;
    data_.sensorPid = pid;
    data_.cameraState = CameraState::streaming;
    data_.frameBytes = 0U;
    if (info != nullptr) {
        data_.width = info->width;
        data_.height = info->height;
    }
}

void SharedState::sent(std::uint32_t width, std::uint32_t height, std::size_t bytes,
                       std::int64_t now, Rates rates) noexcept
{
    const CriticalSection guard{lock_};
    data_.width = width;
    data_.height = height;
    data_.frameBytes = bytes;
    ++data_.framesSent;
    data_.lastFrameUs = now;
    data_.rates = rates;
}

void SharedState::captureFailed() noexcept
{
    const CriticalSection guard{lock_};
    ++data_.captureErrors;
}

void SharedState::cameraFailed(std::int32_t error) noexcept
{
    const CriticalSection guard{lock_};
    data_.cameraError = error;
}

void SharedState::finish(bool failed) noexcept
{
    const CriticalSection guard{lock_};
    data_.streaming = false;
    data_.rates = {};
    data_.hasApplied = false;
    data_.cameraState = failed ? CameraState::error : CameraState::standby;
    // A setting accepted during the final frame must also appear in idle UI.
    const auto* const info = findResolution(data_.selected.resolution);
    if (info != nullptr) {
        data_.width = info->width;
        data_.height = info->height;
    }
}
} // namespace cam
