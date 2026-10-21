#include "rtsp_protocol.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "FAIL RTSP: %s\n", message); std::exit(EXIT_FAILURE); }
}
void requests()
{
    cam::rtsp::Request request{};
    require(cam::rtsp::parseRequest("OPTIONS * RTSP/1.0\r\nCSeq: 1\r\n\r\n", request)
            && request.cseq == 1U && request.method == "OPTIONS", "OPTIONS parse");
    require(cam::rtsp::parseRequest("SETUP rtsp://192.0.2.1/stream/trackID=0 RTSP/1.0\r\n"
        "cseq: 23\r\nTransport: RTP/AVP/TCP;unicast;interleaved=0-1\r\nSession: abc;timeout=60\r\n\r\n", request)
        && request.session == "abc" && request.cseq == 23U, "case-insensitive headers and session options");
    constexpr std::array invalid{
        "", "OPTIONS * RTSP/1.0\r\n\r\n", "OPTIONS * RTSP/2.0\r\nCSeq: 1\r\n\r\n",
        "OPTIONS * RTSP/1.0\r\nCSeq: -1\r\n\r\n", "OPTIONS * RTSP/1.0\r\nCSeq: 4294967296\r\n\r\n",
        "OPTIONS * RTSP/1.0\r\nCSeq: 1x\r\n\r\n", "OPTIONS * RTSP/1.0\r\nCSeq: 1\r\nCSeq: 2\r\n\r\n",
        "PLAY  RTSP/1.0\r\nCSeq: 1\r\n\r\n", "PLAY /stream RTSP/1.0\r\nCSeq: 1\r\nContent-Length: 1\r\n\r\n",
        "PLAY /stream RTSP/1.0\r\nCSeq: 1\r\nContent-Length: 1025\r\n\r\n",
        "OPTIONS * RTSP/1.0\r\nCSeq: 1\r\nContent-Length: 0\r\nContent-Length: 0\r\n\r\n",
        "OPTIONS * RTSP/1.0\r\nCSeq: 1\r\nbadheader\r\n\r\n"};
    for (const auto* input : invalid) { require(!cam::rtsp::parseRequest(input, request), "malformed request rejected"); }
    require(cam::rtsp::validUri("rtsp://192.0.2.1:554/stream", false), "aggregate URI");
    require(cam::rtsp::validUri("/stream/trackID=0", true), "track URI");
    require(!cam::rtsp::validUri("/stream/trackID=1", true)
        && !cam::rtsp::validUri("/stream?extra", false), "unknown URI rejected");
    cam::rtsp::Transport transport{};
    require(cam::rtsp::parseTransport("RTP/AVP/TCP;unicast;interleaved=4-5", transport)
        && transport.tcp && transport.first == 4U, "TCP channels");
    require(cam::rtsp::parseTransport("RTP/AVP;client_port=54000-54001;unicast;mode=\"PLAY\"", transport)
        && !transport.tcp && transport.second == 54001U, "UDP ports and reordered fields");
    constexpr std::array badTransports{
        "RTP/AVP;multicast;client_port=1234-1235", "RTP/AVP;unicast;client_port=0-1",
        "RTP/AVP;unicast;client_port=65535-65536", "RTP/AVP/TCP;unicast;interleaved=255-256",
        "RTP/AVP/TCP;unicast;interleaved=0-2", "RTP/AVP/TCP;unicast;interleaved=-1-0",
        "RTP/AVP;unicast;client_port=1234-1235;destination=192.0.2.9",
        "RTP/AVP;unicast;client_port=1234-1235;client_port=2345-2346", "RTP/AVP;unicast", "RTP/AVP/TCP;unicast;interleaved=4294967295-0"};
    for (const auto* input : badTransports) { require(!cam::rtsp::parseTransport(input, transport), "unsupported transport rejected"); }
}
void jpeg(const std::vector<std::uint8_t>& data)
{
    cam::rtsp::Jpeg parsed{};
    const cam::JpegView view{data.data(), data.size(), 640U, 480U};
    require(cam::rtsp::parseJpeg(view, parsed), "parse baseline fixture");
    require(parsed.widthBlocks == 80U && parsed.heightBlocks == 60U && !parsed.scan.empty(), "JPEG dimensions");
    for (std::size_t length{0U}; length < std::min(data.size(), std::size_t{1024U}); ++length) {
        require(!cam::rtsp::parseJpeg({data.data(), length, 640U, 480U}, parsed), "reject truncated JPEG headers");
    }
    require(!cam::rtsp::parseJpeg({data.data(), data.size(), 320U, 240U}, parsed), "metadata mismatch rejected");
    auto broken = data;
    for (std::size_t i{0U}; i + 1U < broken.size(); ++i) {
        if ((broken[i] == 0xFFU) && (broken[i + 1U] == 0xDAU)) { broken[i + 1U] = 0xD2U; break; }
    }
    require(!cam::rtsp::parseJpeg({broken.data(), broken.size(), 640U, 480U}, parsed), "malformed original-camera SOS rejected");
    require(cam::rtsp::parseJpeg(view, parsed), "restore valid parse");
    cam::rtsp::Packetizer packets{parsed, 65535U, 0x12345678U, 0xABCDEF01U};
    std::array<std::uint8_t, cam::rtsp::Packetizer::capacity> packet{};
    std::vector<std::uint8_t> scan{};
    std::uint16_t sequence{65535U};
    bool marker{false};
    for (;;) {
        const auto size = packets.next(packet);
        if (size == 0U) { break; }
        require(!marker && size <= 1400U, "only final packet carries marker");
        require(packet[0U] == 0x80U && (packet[1U] & 0x7FU) == 26U, "RTP version and JPEG payload type");
        require(((static_cast<unsigned>(packet[2U]) << 8U) | packet[3U]) == sequence++, "sequence and rollover");
        require(packet[4U] == 0x12U && packet[7U] == 0x78U && packet[8U] == 0xABU, "timestamp and SSRC byte order");
        const auto offset = (static_cast<std::size_t>(packet[13U]) << 16U)
            | (static_cast<std::size_t>(packet[14U]) << 8U) | packet[15U];
        require(offset == scan.size() && packet[17U] == 255U, "scan-relative offset and explicit quantizers");
        const auto header = scan.empty() ? 152U : 20U;
        if (scan.empty()) {
            require(packet[22U] == 0U && packet[23U] == 128U, "quantizer size");
            require(std::equal(parsed.quantization.begin(), parsed.quantization.end(), packet.begin() + 24U), "quantizers unchanged");
        }
        scan.insert(scan.end(), packet.begin() + header, packet.begin() + static_cast<std::ptrdiff_t>(size));
        marker = (packet[1U] & 0x80U) != 0U;
    }
    require(marker && scan.size() == parsed.scan.size()
        && std::equal(scan.begin(), scan.end(), parsed.scan.begin()), "lossless packet reassembly");
    require(packets.sequence() == sequence, "next sequence follows last packet");
    // ASan/UBSan exercise header corruption, marker scanning and table lengths.
    for (std::size_t i{0U}; i < std::min(data.size(), std::size_t{1024U}); ++i) {
        broken = data;
        broken[i] ^= 0xFFU;
        static_cast<void>(cam::rtsp::parseJpeg({broken.data(), broken.size(), 640U, 480U}, parsed));
    }
}
} // namespace
int main(int argc, char** argv)
{
    require(argc == 2, "fixture argument required");
    std::ifstream file{argv[1], std::ios::binary};
    const std::vector<std::uint8_t> data{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    require(!data.empty(), "read generated fixture");
    requests();
    jpeg(data);
    std::puts("PASS: RTSP parsing, transports, JPEG validation and RTP/JPEG packet reassembly");
}
