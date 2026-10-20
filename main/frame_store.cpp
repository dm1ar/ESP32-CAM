#include "frame_store.hpp"
#include <cstring>
#include "esp_heap_caps.h"

namespace cam {
JpegBuffer::~JpegBuffer() noexcept { heap_caps_free(storage_); }

bool JpegBuffer::allocate() noexcept
{
    if (storage_ == nullptr) {
        storage_ = static_cast<std::uint8_t*>(
            heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    return storage_ != nullptr;
}

bool JpegBuffer::assign(JpegView frame) noexcept
{
    clear();
    if ((storage_ == nullptr) || (frame.data == nullptr) || (frame.size == 0U)
        || (frame.size > capacity)) {
        return false;
    }
    std::memcpy(storage_, frame.data, frame.size);
    view_ = {storage_, frame.size, frame.width, frame.height};
    return true;
}

esp_err_t FrameStore::start() noexcept
{
    return (latest_.allocate() && stream_.allocate() && snapshot_.allocate())
        ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t FrameStore::sleep() noexcept
{
    latest_.clear(); // A new session must never reuse an old camera image.
    return camera_.sleep();
}

esp_err_t FrameStore::fresh(const CameraSettings& settings) noexcept
{
    latest_.clear();
    if (!camera_.matches(settings)) {
        state_.waking();
        const auto result = camera_.prepare(settings);
        if (result != ESP_OK) { return result; }
        state_.ready(settings, camera_.sensorPid());
    }
    const FrameLease lease{};
    const auto* const frame = lease.get();
    if ((frame == nullptr) || (frame->format != PIXFORMAT_JPEG)
        || (frame->buf == nullptr) || (frame->len == 0U)) {
        state_.captureFailed();
        return ESP_ERR_INVALID_STATE;
    }
    if (!latest_.assign({frame->buf, frame->len, static_cast<std::uint32_t>(frame->width),
                        static_cast<std::uint32_t>(frame->height)})) {
        state_.captureFailed();
        return ESP_ERR_INVALID_SIZE;
    }
    cachedSettings_ = settings;
    return ESP_OK; // Driver lease returns before any HTTP transmission.
}

esp_err_t FrameStore::capture(bool snapshot) noexcept
{
    const auto desired = state_.snapshot();
    if (snapshot && !desired.streaming) { state_.cameraFailed(0); }
    auto& output = snapshot ? snapshot_ : stream_;
    output.clear();
    // A snapshot during a stream uses the most recently captured frame, even
    // while the stream HTTP task is blocked sending its independent copy.
    const bool reuse = snapshot && desired.streaming && camera_.matches(desired.selected)
        && (cachedSettings_ == desired.selected) && (latest_.view().size != 0U);
    auto result = reuse ? ESP_OK : fresh(desired.selected);
    if ((result == ESP_OK) && !output.assign(latest_.view())) {
        result = ESP_ERR_INVALID_SIZE;
    }
    if (snapshot && !desired.streaming) {
        const auto cleanup = sleep();
        if (result == ESP_OK) { result = cleanup; }
        if (result != ESP_OK) { state_.cameraFailed(result); }
        state_.finish(result != ESP_OK);
    }
    if (result != ESP_OK) { state_.cameraFailed(result); }
    return result;
}
} // namespace cam
