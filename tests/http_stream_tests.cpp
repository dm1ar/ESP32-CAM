/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "http_stream.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>

int main()
{
    std::string wire;
    const auto send = [&wire](const char* bytes, std::size_t size) {
        const auto count = std::min(size, std::size_t{3U});
        wire.append(bytes, count);
        return static_cast<int>(count);
    };
    const auto header = cam::httpstream::headers;
    assert(cam::httpstream::sendAll({header.data(), header.size()}, send));
    constexpr std::string_view frame{"--esp32camframe\r\nContent-Type: image/jpeg\r\nContent-Length: 4\r\n\r\n\xff\xd8\xff\xd9\r\n"};
    assert(cam::httpstream::sendAll({frame.data(), frame.size()}, send));
    assert(wire == std::string{header} + std::string{frame});
    assert(wire.find("Transfer-Encoding") == std::string::npos);
    assert(wire.find("Connection: close\r\n") != std::string::npos);
    assert(wire.find("\r\n\r\n--esp32camframe") != std::string::npos);
    const std::span<const char> data{frame.data(), frame.size()};
    assert(!cam::httpstream::sendAll(data, [](const char*, std::size_t) { return 0; }));
    assert(!cam::httpstream::sendAll(data, [](const char*, std::size_t) { return -1; }));
    assert(!cam::httpstream::sendAll(data, [](const char*, std::size_t size) { return static_cast<int>(size + 1U); }));
    unsigned calls{0U};
    assert(!cam::httpstream::sendAll(data, [&calls](const char*, std::size_t) { return (++calls == 1U) ? 2 : -1; }));
    assert(calls == 2U);
    assert(cam::httpstream::sendAll({}, [](const char*, std::size_t) { std::abort(); return 0; }));
    std::puts("PASS: close-delimited MJPEG headers, byte-exact partial sends and send failures");
}
