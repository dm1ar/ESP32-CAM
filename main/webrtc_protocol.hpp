/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef ESP32_CAM_WEBRTC_PROTOCOL_HPP
#define ESP32_CAM_WEBRTC_PROTOCOL_HPP
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
namespace cam::webrtc {
inline constexpr std::size_t headerSize{16U};
inline constexpr std::size_t payloadSize{4096U};
// JPG1 + big-endian frame id, whole JPEG size, byte offset. One ordered,
// reliable data-channel message is acknowledged by one ASCII "next".
[[nodiscard]] inline std::size_t chunk(std::span<const std::uint8_t> jpeg,
    std::uint32_t frame, std::size_t offset, std::span<std::uint8_t> out) noexcept
{
    if (jpeg.empty() || (jpeg.size() > 512U * 1024U) || (offset >= jpeg.size())
        || (out.size() < headerSize + payloadSize)) { return 0U; }
    out[0U] = 'J'; out[1U] = 'P'; out[2U] = 'G'; out[3U] = '1';
    const std::array<std::uint32_t, 3U> values{frame, static_cast<std::uint32_t>(jpeg.size()),
                                           static_cast<std::uint32_t>(offset)};
    for (std::size_t i{0U}; i < values.size(); ++i) {
        for (std::size_t byte{0U}; byte < 4U; ++byte) {
            out[4U + (i * 4U) + byte] = static_cast<std::uint8_t>(values[i] >> ((3U - byte) * 8U));
        }
    }
    const auto size = std::min(payloadSize, jpeg.size() - offset);
    std::copy_n(jpeg.data() + offset, size, out.data() + headerSize);
    return headerSize + size;
}
} // namespace cam::webrtc
#endif
