/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef ESP32_CAM_HTTP_STREAM_HPP
#define ESP32_CAM_HTTP_STREAM_HPP
#include <span>
#include <string_view>

namespace cam::httpstream {
// A live response has no finite Content-Length. Closing the connection
// delimits it for both HTTP/1.0 and HTTP/1.1; HTTP chunk coding is not used.
inline constexpr std::string_view headers{
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: multipart/x-mixed-replace; boundary=esp32camframe\r\n"
    "Cache-Control: no-store\r\nConnection: close\r\n\r\n"};
inline constexpr std::string_view closing{"--esp32camframe--\r\n"};

template<class Sender>
[[nodiscard]] bool sendAll(std::span<const char> bytes, Sender sender) noexcept
{
    while (!bytes.empty()) {
        const auto sent = sender(bytes.data(), bytes.size());
        if ((sent <= 0) || (static_cast<std::size_t>(sent) > bytes.size())) { return false; }
        bytes = bytes.subspan(static_cast<std::size_t>(sent));
    }
    return true;
}
} // namespace cam::httpstream
#endif
