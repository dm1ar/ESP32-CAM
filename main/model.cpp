#include "model.hpp"

namespace cam {
void RateMeter::reset(std::int64_t now) noexcept
{
    startUs_ = now;
    frames_ = 0U;
    bytes_ = 0U;
    rates_ = {};
}

Rates RateMeter::sent(std::int64_t now, std::size_t bytes) noexcept
{
    ++frames_;
    bytes_ += bytes;
    constexpr std::int64_t windowUs{1'000'000};
    const auto elapsed = now - startUs_;
    if (elapsed >= windowUs) {
        const auto seconds = static_cast<float>(elapsed) / 1'000'000.0F;
        rates_.fps = static_cast<float>(frames_) / seconds;
        rates_.kibPerSecond = static_cast<float>(bytes_) / (1024.0F * seconds);
        startUs_ = now;
        frames_ = 0U;
        bytes_ = 0U;
    }
    return rates_;
}
} // namespace cam
