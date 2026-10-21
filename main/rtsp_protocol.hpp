/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef ESP32_CAM_RTSP_PROTOCOL_HPP
#define ESP32_CAM_RTSP_PROTOCOL_HPP

#include <array>
#include <span>
#include <string_view>
#include "frame_store.hpp"

namespace cam::rtsp {
struct Request final {
    std::string_view method{};
    std::string_view uri{};
    std::string_view transport{};
    std::string_view session{};
    std::uint32_t cseq{0U};
};
// Views borrow input until the request has been answered. Strict bounded
// framing and parsing; the socket adapter removes interleaved RTCP first.
[[nodiscard]] bool parseRequest(std::string_view input, Request& request) noexcept;
[[nodiscard]] bool contentLength(std::string_view header, std::size_t& length) noexcept;
[[nodiscard]] bool validUri(std::string_view uri, bool track) noexcept;
struct Transport final {
    bool tcp{false};
    std::uint16_t first{0U}; // Channel for TCP, client port for UDP.
    std::uint16_t second{1U};
};
[[nodiscard]] bool parseTransport(std::string_view input, Transport& result) noexcept;
struct Jpeg final {
    std::span<const std::uint8_t> scan{};
    std::array<std::uint8_t, 128U> quantization{};
    std::uint8_t type{0U};
    std::uint8_t widthBlocks{0U};
    std::uint8_t heightBlocks{0U};
};
// RFC 2435 baseline YCbCr 4:2:2 or 4:2:0, standard Huffman coding, no DRI.
// Reject unsupported camera output rather than repairing or mislabeling it.
[[nodiscard]] bool parseJpeg(JpegView frame, Jpeg& result) noexcept;
class Packetizer final {
public:
    static constexpr std::size_t capacity{1400U};
    Packetizer(const Jpeg& jpeg, std::uint16_t sequence, std::uint32_t timestamp,
               std::uint32_t ssrc) noexcept
        : jpeg_{jpeg}, sequence_{sequence}, timestamp_{timestamp}, ssrc_{ssrc} {}
    [[nodiscard]] std::size_t next(std::span<std::uint8_t> packet) noexcept;
    [[nodiscard]] std::uint16_t sequence() const noexcept { return sequence_; }
private:
    const Jpeg& jpeg_;
    std::size_t offset_{0U};
    std::uint16_t sequence_;
    std::uint32_t timestamp_;
    std::uint32_t ssrc_;
};
void put16(std::span<std::uint8_t> output, std::size_t offset, std::uint16_t value) noexcept;
void put32(std::span<std::uint8_t> output, std::size_t offset, std::uint32_t value) noexcept;
} // namespace cam::rtsp
#endif
