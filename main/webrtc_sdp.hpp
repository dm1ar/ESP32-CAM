/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef ESP32_CAM_WEBRTC_SDP_HPP
#define ESP32_CAM_WEBRTC_SDP_HPP
#include <array>
#include <cstring>
#include <span>
#include <string_view>

namespace cam::webrtc {
// Only ICE address tokens are replaced. Fingerprints, ICE credentials, ports,
// priorities, foundations and the rest of SDP remain byte-for-byte unchanged.
template<class Resolver>
[[nodiscard]] std::size_t resolveSdp(std::string_view input, std::span<char> output,
                                     Resolver resolve) noexcept
{
    std::size_t written{0U}, queries{0U}, usable{0U};
    bool hadMdns{false};
    const auto append = [&output, &written](std::string_view bytes) {
        if ((written >= output.size()) || (bytes.size() >= output.size() - written)) { return false; }
        std::memcpy(output.data() + written, bytes.data(), bytes.size());
        written += bytes.size(); return true;
    };
    while (!input.empty()) {
        const auto end = input.find('\n');
        const auto count = (end == std::string_view::npos) ? input.size() : end + 1U;
        const auto line = input.substr(0U, count);
        input.remove_prefix(count);
        if (!line.starts_with("a=candidate:")) {
            if (!append(line)) { return 0U; }
            continue;
        }
        std::array<std::string_view, 8U> fields{};
        auto remaining = line;
        for (auto& field : fields) {
            const auto begin = remaining.find_first_not_of(" \t\r\n");
            if (begin == std::string_view::npos) { return 0U; }
            remaining.remove_prefix(begin);
            const auto stop = remaining.find_first_of(" \t\r\n");
            field = remaining.substr(0U, stop);
            remaining.remove_prefix(field.size());
        }
        const auto address = fields[4U];
        if (!address.ends_with(".local")) {
            if ((fields[2U] == "udp") || (fields[2U] == "UDP")) { ++usable; }
            if (!append(line)) { return 0U; }
            continue;
        }
        hadMdns = true;
        if ((fields[2U] != "udp") && (fields[2U] != "UDP")) { continue; }
        const auto hostname = address.substr(0U, address.size() - 6U);
        bool valid = !hostname.empty() && (hostname.size() <= 63U);
        for (const char ch : hostname) {
            valid = valid && (((ch >= 'a') && (ch <= 'z')) || ((ch >= 'A') && (ch <= 'Z'))
                || ((ch >= '0') && (ch <= '9')) || (ch == '-') || (ch == '_'));
        }
        std::array<char, 16U> ip{};
        if (!valid || (queries >= 4U)) { continue; }
        ++queries;
        const auto length = resolve(hostname, std::span<char>{ip});
        if ((length == 0U) || (length >= ip.size())) { continue; }
        const auto prefix = static_cast<std::size_t>(address.data() - line.data());
        if (!append(line.substr(0U, prefix)) || !append({ip.data(), length})
            || !append(line.substr(prefix + address.size()))) { return 0U; }
        ++usable;
    }
    if ((written == 0U) || (hadMdns && (usable == 0U))) { return 0U; }
    output[written] = '\0';
    return written;
}
} // namespace cam::webrtc
#endif
