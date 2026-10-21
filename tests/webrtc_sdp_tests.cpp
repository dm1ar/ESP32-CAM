/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "webrtc_sdp.hpp"
#include <cassert>
#include <cstdio>
#include <string>

int main()
{
    std::array<char, 4096U> output{};
    unsigned queries{0U};
    const auto resolve = [&queries](std::string_view name, std::span<char> ip) {
        ++queries;
        if (name == "missing") { return std::size_t{0U}; }
        const std::string_view address{"192.168.1.126"};
        std::memcpy(ip.data(), address.data(), address.size()); return address.size();
    };
    const std::string_view base{"v=0\r\na=ice-ufrag:secret\r\na=fingerprint:sha-256 AA:BB\r\n"};
    const std::string_view numeric{"a=candidate:abc 1 udp 123 192.168.1.126 4321 typ host generation 0\r\n"};
    const auto literal = std::string{base} + std::string{numeric};
    auto size = cam::webrtc::resolveSdp(literal, output, resolve);
    assert(std::string_view(output.data(), size) == literal && queries == 0U);
    const std::string offer = std::string{base}
        + "a=candidate:abc 1 udp 123 browser.local 4321 typ host generation 0\r\n";
    size = cam::webrtc::resolveSdp(offer, output, resolve);
    assert(std::string_view(output.data(), size) == literal && queries == 1U && output[size] == '\0');
    const std::string failed = std::string{base} + "a=candidate:x 1 udp 123 missing.local 4321 typ host\r\n";
    assert(cam::webrtc::resolveSdp(failed, output, resolve) == 0U);
    size = cam::webrtc::resolveSdp(failed + std::string{numeric}, output, resolve);
    assert(std::string_view(output.data(), size) == literal);
    assert(cam::webrtc::resolveSdp("a=candidate:malformed\r\n", output, resolve) == 0U);
    assert(cam::webrtc::resolveSdp(offer, std::span<char>{output.data(), 20U}, resolve) == 0U);
    const auto before = queries;
    const std::string injection = std::string{base} + "a=candidate:x 1 udp 123 bad/name.local 4321 typ host\r\n";
    assert(cam::webrtc::resolveSdp(injection, output, resolve) == 0U && queries == before);
    std::string many{base};
    for (unsigned i = 0; i < 8U; ++i) { many += "a=candidate:x 1 udp 123 browser.local 4321 typ host\r\n"; }
    assert(cam::webrtc::resolveSdp(many, output, resolve) != 0U && queries == before + 4U);
    const std::string tcp = std::string{base} + "a=candidate:x 1 tcp 123 browser.local 9 typ host tcptype active\r\n";
    assert(cam::webrtc::resolveSdp(tcp, output, resolve) == 0U);
    std::puts("PASS: bounded browser mDNS SDP resolution, literal preservation, failures and candidate limits");
}
