/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "webrtc_protocol.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
namespace {
void require(bool value) { if (!value) { std::abort(); } }
std::uint32_t get(const std::span<const std::uint8_t> bytes, std::size_t offset)
{
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U)
        | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U)
        | (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) | bytes[offset + 3U];
}
}
int main()
{
    std::vector<std::uint8_t> jpeg(512U * 1024U);
    for (std::size_t i{0U}; i < jpeg.size(); ++i) { jpeg[i] = static_cast<std::uint8_t>(i); }
    std::array<std::uint8_t, cam::webrtc::headerSize + cam::webrtc::payloadSize> packet{};
    std::size_t offset{0U};
    while (offset < jpeg.size()) {
        const auto length = cam::webrtc::chunk(jpeg, 0x12345678U, offset, packet);
        require((length > 16U) && (length <= packet.size()));
        require(get(packet, 0U) == 0x4A504731U && get(packet, 4U) == 0x12345678U);
        require(get(packet, 8U) == jpeg.size() && get(packet, 12U) == offset);
        for (std::size_t i{16U}; i < length; ++i) { require(packet[i] == jpeg[offset + i - 16U]); }
        offset += length - 16U;
    }
    require(cam::webrtc::chunk(jpeg, 0U, jpeg.size(), packet) == 0U);
    require(cam::webrtc::chunk({}, 0U, 0U, packet) == 0U);
    require(cam::webrtc::chunk(jpeg, 0U, 0U, {packet.data(), 10U}) == 0U);
    std::puts("PASS: bounded WebRTC JPEG chunk protocol and byte-exact reassembly");
}
