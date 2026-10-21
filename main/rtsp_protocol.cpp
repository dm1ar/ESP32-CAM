#include "rtsp_protocol.hpp"
#include <algorithm>
#include <charconv>
#include <cstring>

namespace cam::rtsp {
namespace {
[[nodiscard]] std::string_view trim(std::string_view value) noexcept
{
    while (!value.empty() && ((value.front() == ' ') || (value.front() == '\t'))) { value.remove_prefix(1U); }
    while (!value.empty() && ((value.back() == ' ') || (value.back() == '\t'))) { value.remove_suffix(1U); }
    return value;
}
[[nodiscard]] bool equal(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size()) { return false; }
    for (std::size_t i{0U}; i < a.size(); ++i) {
        const auto lower = [](char c) noexcept { return ((c >= 'A') && (c <= 'Z')) ? c + ('a' - 'A') : c; };
        if (lower(a[i]) != lower(b[i])) { return false; }
    }
    return true;
}
[[nodiscard]] bool number(std::string_view value, std::uint32_t& output) noexcept
{
    if (value.empty()) { return false; }
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), output);
    return (parsed.ec == std::errc{}) && (parsed.ptr == value.data() + value.size());
}
[[nodiscard]] bool headerValue(std::string_view input, std::string_view name,
                               std::string_view& result, bool& present) noexcept
{
    present = false;
    auto start = input.find("\r\n");
    if (start == std::string_view::npos) { return false; }
    input.remove_prefix(start + 2U);
    while (!input.empty()) {
        const auto end = input.find("\r\n");
        if (end == std::string_view::npos) { return false; }
        const auto line = input.substr(0U, end);
        input.remove_prefix(end + 2U);
        if (line.empty()) { return true; }
        const auto colon = line.find(':');
        if ((colon == 0U) || (colon == std::string_view::npos)) { return false; }
        if (equal(line.substr(0U, colon), name)) {
            if (present) { return false; }
            present = true;
            result = trim(line.substr(colon + 1U));
        }
    }
    return false;
}
[[nodiscard]] std::uint16_t read16(std::span<const std::uint8_t> input, std::size_t offset) noexcept
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(input[offset]) << 8U) | input[offset + 1U]);
}
} // namespace

bool contentLength(std::string_view header, std::size_t& length) noexcept
{
    std::string_view value{};
    bool present{false};
    if (!headerValue(header, "Content-Length", value, present)) { return false; }
    std::uint32_t parsed{0U};
    if (present && (!number(value, parsed) || (parsed > 1024U))) { return false; }
    length = parsed;
    return true;
}

bool parseRequest(std::string_view input, Request& request) noexcept
{
    if ((input.size() > 4096U) || (input.find('\0') != std::string_view::npos)) { return false; }
    const auto headerEnd = input.find("\r\n\r\n");
    if (headerEnd == std::string_view::npos) { return false; }
    std::size_t body{0U};
    const auto header = input.substr(0U, headerEnd + 4U);
    if (!contentLength(header, body) || (input.size() != header.size() + body)) { return false; }
    const auto lineEnd = header.find("\r\n");
    const auto line = header.substr(0U, lineEnd);
    const auto first = line.find(' ');
    if ((first == 0U) || (first == std::string_view::npos)) { return false; }
    const auto second = line.find(' ', first + 1U);
    if ((second == std::string_view::npos) || (second == first + 1U)
        || (line.substr(second + 1U) != "RTSP/1.0")) { return false; }
    Request parsed{};
    parsed.method = line.substr(0U, first);
    parsed.uri = line.substr(first + 1U, second - first - 1U);
    std::string_view value{};
    bool present{false};
    if (!headerValue(header, "CSeq", value, present) || !present || !number(value, parsed.cseq)) { return false; }
    if (!headerValue(header, "Transport", parsed.transport, present)) { return false; }
    if (!headerValue(header, "Session", parsed.session, present)) { return false; }
    parsed.session = trim(parsed.session.substr(0U, parsed.session.find(';')));
    request = parsed;
    return true;
}

bool validUri(std::string_view uri, bool track) noexcept
{
    if (uri.starts_with("rtsp://")) {
        const auto path = uri.find('/', 7U);
        if (path == std::string_view::npos) { return false; }
        uri.remove_prefix(path);
    }
    return track ? (uri == "/stream/trackID=0") : ((uri == "/stream") || (uri == "/stream/"));
}

bool parseTransport(std::string_view input, Transport& result) noexcept
{
    const auto first = input.find(';');
    const auto protocol = input.substr(0U, first);
    Transport parsed{};
    parsed.tcp = equal(protocol, "RTP/AVP/TCP");
    if (!parsed.tcp && !equal(protocol, "RTP/AVP") && !equal(protocol, "RTP/AVP/UDP")) { return false; }
    if (first == std::string_view::npos) { return false; }
    input.remove_prefix(first + 1U);
    bool unicast{false};
    bool pair{false};
    while (!input.empty()) {
        const auto end = input.find(';');
        const auto field = trim(input.substr(0U, end));
        const auto equals = field.find('=');
        const auto key = field.substr(0U, equals);
        if (equal(field, "unicast") && !unicast) {
            unicast = true;
        } else if (equal(key, parsed.tcp ? "interleaved" : "client_port") && !pair
                   && (equals != std::string_view::npos)) {
            const auto value = field.substr(equals + 1U);
            const auto dash = value.find('-');
            std::uint32_t a{0U};
            std::uint32_t b{0U};
            if ((dash == std::string_view::npos) || !number(value.substr(0U, dash), a)
                || !number(value.substr(dash + 1U), b)
                || (a >= (parsed.tcp ? 255U : 65535U)) || (b != a + 1U)
                || (b > (parsed.tcp ? 255U : 65535U)) || (!parsed.tcp && (a == 0U))) { return false; }
            parsed.first = static_cast<std::uint16_t>(a);
            parsed.second = static_cast<std::uint16_t>(b);
            pair = true;
        } else if (!equal(field, "mode=play") && !equal(field, "mode=\"play\"")) {
            return false; // No multicast, arbitrary destination or unknown parameters.
        }
        if (end == std::string_view::npos) { input = {}; }
        else { input.remove_prefix(end + 1U); }
    }
    if (!pair || !unicast) { return false; }
    result = parsed;
    return true;
}

bool parseJpeg(JpegView frame, Jpeg& result) noexcept
{
    if ((frame.data == nullptr) || (frame.size < 4U) || (frame.size > JpegBuffer::capacity)) { return false; }
    const std::span<const std::uint8_t> data{frame.data, frame.size};
    if ((data[0U] != 0xFFU) || (data[1U] != 0xD8U)) { return false; }
    Jpeg parsed{};
    bool sof{false};
    std::array<bool, 4U> huffman{};
    std::array<bool, 2U> quant{};
    std::size_t offset{2U};
    while (offset + 4U <= data.size()) {
        if (data[offset] != 0xFFU) { return false; }
        const auto marker = data[offset + 1U];
        const auto length = static_cast<std::size_t>(read16(data, offset + 2U));
        if ((length < 2U) || (length > data.size() - offset - 2U)) { return false; }
        const auto segment = data.subspan(offset + 4U, length - 2U);
        offset += length + 2U;
        if (marker == 0xDBU) {
            std::size_t q{0U};
            while (q < segment.size()) {
                const auto id = segment[q];
                if ((id > 1U) || quant[id] || (segment.size() - q < 65U)) { return false; }
                std::copy_n(segment.data() + q + 1U, 64U, parsed.quantization.data() + (64U * id));
                quant[id] = true;
                q += 65U;
            }
        } else if (marker == 0xC0U) {
            if (sof || (segment.size() != 15U) || (segment[0U] != 8U) || (segment[5U] != 3U)
                || (segment[6U] != 1U) || ((segment[7U] != 0x21U) && (segment[7U] != 0x22U))
                || (segment[8U] != 0U) || (segment[9U] != 2U) || (segment[10U] != 0x11U)
                || (segment[11U] != 1U) || (segment[12U] != 3U) || (segment[13U] != 0x11U)
                || (segment[14U] != 1U)) { return false; }
            const auto height = read16(segment, 1U);
            const auto width = read16(segment, 3U);
            if ((width == 0U) || (height == 0U) || (width > 2040U) || (height > 2040U)
                || ((width % 8U) != 0U) || ((height % 8U) != 0U)
                || (width != frame.width) || (height != frame.height)) { return false; }
            parsed.widthBlocks = static_cast<std::uint8_t>(width / 8U);
            parsed.heightBlocks = static_cast<std::uint8_t>(height / 8U);
            parsed.type = (segment[7U] == 0x22U) ? 1U : 0U;
            sof = true;
        } else if (marker == 0xC4U) {
            // Fingerprints of the four standard JPEG Huffman tables (Annex K).
            // Check table structure and symbols, not just the marker length.
            constexpr std::array<std::uint32_t, 4U> standard{
                0x8C4FCF27U, 0x6569BE85U, 0x3DCEACBCU, 0xAA231D1CU};
            std::size_t table{0U};
            while (table < segment.size()) {
                if (segment.size() - table < 17U) { return false; }
                const auto id = segment[table];
                if ((id != 0U) && (id != 1U) && (id != 0x10U) && (id != 0x11U)) { return false; }
                const auto index = static_cast<std::size_t>(((id & 1U) * 2U) + (id >> 4U));
                std::size_t symbols{0U};
                for (std::size_t i{1U}; i <= 16U; ++i) { symbols += segment[table + i]; }
                const auto size = 17U + symbols;
                if (huffman[index] || (size > segment.size() - table)) { return false; }
                std::uint32_t hash{2166136261U};
                for (std::size_t i{0U}; i < size; ++i) { hash = (hash ^ segment[table + i]) * 16777619U; }
                if (hash != standard[index]) { return false; }
                huffman[index] = true;
                table += size;
            }
        } else if (marker == 0xDAU) {
            constexpr std::array<std::uint8_t, 10U> expected{3U, 1U, 0U, 2U, 0x11U, 3U, 0x11U, 0U, 63U, 0U};
            if (!sof || !quant[0U] || !quant[1U]
                || !std::all_of(huffman.begin(), huffman.end(), [](bool value) noexcept { return value; })
                || (segment.size() != expected.size())
                || !std::equal(segment.begin(), segment.end(), expected.begin())) { return false; }
            const auto scanStart = offset;
            while (offset + 1U < data.size()) {
                if (data[offset] == 0xFFU) {
                    if (data[offset + 1U] == 0xD9U) {
                        if (offset == scanStart) { return false; }
                        parsed.scan = data.subspan(scanStart, offset - scanStart);
                        result = parsed;
                        return true;
                    }
                    if (data[offset + 1U] != 0U) { return false; }
                    ++offset;
                }
                ++offset;
            }
            return false;
        } else if ((marker != 0xFEU) && ((marker < 0xE0U) || (marker > 0xEFU))) {
            return false;
        }
    }
    return false;
}

void put16(std::span<std::uint8_t> output, std::size_t offset, std::uint16_t value) noexcept
{
    output[offset] = static_cast<std::uint8_t>(value >> 8U);
    output[offset + 1U] = static_cast<std::uint8_t>(value);
}
void put32(std::span<std::uint8_t> output, std::size_t offset, std::uint32_t value) noexcept
{
    output[offset] = static_cast<std::uint8_t>(value >> 24U);
    output[offset + 1U] = static_cast<std::uint8_t>(value >> 16U);
    output[offset + 2U] = static_cast<std::uint8_t>(value >> 8U);
    output[offset + 3U] = static_cast<std::uint8_t>(value);
}

std::size_t Packetizer::next(std::span<std::uint8_t> packet) noexcept
{
    if ((offset_ >= jpeg_.scan.size()) || (packet.size() < capacity)) { return 0U; }
    const auto header = (offset_ == 0U) ? 152U : 20U;
    const auto bytes = std::min(jpeg_.scan.size() - offset_, capacity - header);
    packet[0U] = 0x80U;
    packet[1U] = ((offset_ + bytes) == jpeg_.scan.size()) ? 0x9AU : 0x1AU;
    put16(packet, 2U, sequence_++);
    put32(packet, 4U, timestamp_);
    put32(packet, 8U, ssrc_);
    packet[12U] = 0U;
    packet[13U] = static_cast<std::uint8_t>(offset_ >> 16U);
    packet[14U] = static_cast<std::uint8_t>(offset_ >> 8U);
    packet[15U] = static_cast<std::uint8_t>(offset_);
    packet[16U] = jpeg_.type;
    packet[17U] = 255U; // Explicit, potentially changing quantizers on every frame.
    packet[18U] = jpeg_.widthBlocks;
    packet[19U] = jpeg_.heightBlocks;
    if (offset_ == 0U) {
        packet[20U] = 0U;
        packet[21U] = 0U;
        put16(packet, 22U, 128U);
        std::copy(jpeg_.quantization.begin(), jpeg_.quantization.end(), packet.begin() + 24U);
    }
    std::copy_n(jpeg_.scan.data() + offset_, bytes, packet.data() + header);
    offset_ += bytes;
    return header + bytes;
}
} // namespace cam::rtsp
