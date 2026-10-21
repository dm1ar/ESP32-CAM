#include "rtsp_server.hpp"
#include "rtsp_protocol.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <sys/time.h>
#include "lwip/sockets.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "network.hpp"

namespace cam {
namespace {
constexpr char tag[]{"rtsp"};
class Socket final {
public:
    explicit Socket(int descriptor = -1) noexcept : descriptor_{descriptor} {}
    ~Socket() noexcept { if (descriptor_ >= 0) { static_cast<void>(close(descriptor_)); } }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    [[nodiscard]] int get() const noexcept { return descriptor_; }
    void set(int descriptor) noexcept { descriptor_ = descriptor; } // Only once after construction.
private:
    int descriptor_;
};
[[nodiscard]] bool sendAll(int socket, const void* data, std::size_t length) noexcept
{
    const auto* cursor = static_cast<const std::uint8_t*>(data);
    while (length != 0U) {
        const auto sent = send(socket, cursor, length, 0);
        if ((sent < 0) && (errno == EINTR)) { continue; }
        if (sent <= 0) { return false; }
        const auto count = static_cast<std::size_t>(sent);
        cursor += count;
        length -= count;
    }
    return true;
}
[[nodiscard]] bool timeouts(int socket) noexcept
{
    const timeval timeout{5, 0};
    return (setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0)
        && (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
}

class RtspSession final {
public:
    RtspSession(int client, sockaddr_in peer, FrameService& frames, SharedState& state) noexcept
        : client_{client}, peer_{peer}, frames_{frames}, state_{state} {}
    ~RtspSession() noexcept { end(false); }
    RtspSession(const RtspSession&) = delete;
    RtspSession& operator=(const RtspSession&) = delete;
    void run() noexcept;
private:
    [[nodiscard]] bool respond(const rtsp::Request& request, const char* status,
                               const char* extra = "", const char* body = "") noexcept;
    [[nodiscard]] bool handle(const rtsp::Request& request) noexcept;
    [[nodiscard]] bool consume() noexcept;
    [[nodiscard]] bool setup(const rtsp::Request& request) noexcept;
    [[nodiscard]] bool packet(std::span<const std::uint8_t> data, bool control) noexcept;
    [[nodiscard]] bool frame() noexcept;
    [[nodiscard]] bool report() noexcept;
    void end(bool failed, bool bye = true) noexcept;
    Socket client_;
    sockaddr_in peer_{};
    FrameService& frames_;
    SharedState& state_;
    Socket rtp_{};
    Socket rtcp_{};
    rtsp::Transport transport_{};
    bool configured_{false};
    bool playing_{false};
    bool closing_{false};
    std::array<char, 9U> session_{};
    std::array<char, 80U> base_{};
    std::array<std::uint8_t, 4096U> input_{};
    std::size_t used_{0U};
    std::uint32_t ssrc_{0U};
    std::uint16_t sequence_{0U};
    std::uint32_t baseTimestamp_{0U};
    std::uint32_t packets_{0U};
    std::uint32_t octets_{0U};
    std::int64_t startedUs_{0};
    std::int64_t lastControlUs_{0};
    std::int64_t nextFrameUs_{0};
    std::int64_t lastReportUs_{0};
    RateMeter meter_{};
};

bool RtspSession::respond(const rtsp::Request& request, const char* status,
                          const char* extra, const char* body) noexcept
{
    std::array<char, 1600U> response{};
    std::array<char, 64U> sessionHeader{};
    if (configured_) {
        static_cast<void>(std::snprintf(sessionHeader.data(), sessionHeader.size(),
            "Session: %s;timeout=60\r\n", session_.data()));
    }
    const auto length = std::snprintf(response.data(), response.size(),
        "RTSP/1.0 %s\r\nCSeq: %" PRIu32 "\r\nServer: ESP32-CAM\r\n%s%sContent-Length: %zu\r\n\r\n%s",
        status, request.cseq, sessionHeader.data(), extra, std::strlen(body), body);
    return (length > 0) && (static_cast<std::size_t>(length) < response.size())
        && sendAll(client_.get(), response.data(), static_cast<std::size_t>(length));
}

bool RtspSession::setup(const rtsp::Request& request) noexcept
{
    if (configured_) { return respond(request, "455 Method Not Valid in This State"); }
    rtsp::Transport selected{};
    if (!rtsp::parseTransport(request.transport, selected)) {
        return respond(request, "461 Unsupported Transport");
    }
    transport_ = selected;
    if (!transport_.tcp) {
        // One session, fixed adjacent local RTP/RTCP ports; destination is
        // always the control connection peer, never a request-supplied host.
        rtp_.set(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
        rtcp_.set(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        local.sin_port = htons(5004U);
        if ((rtp_.get() < 0) || (rtcp_.get() < 0)) {
            ESP_LOGE(tag, "UDP socket allocation failed (errno %d); check socket budget", errno);
            closing_ = true;
            return respond(request, "500 Internal Server Error");
        }
        if (!timeouts(rtp_.get()) || !timeouts(rtcp_.get())
            || (bind(rtp_.get(), reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0)) {
            ESP_LOGE(tag, "RTP socket configuration failed (errno %d)", errno);
            closing_ = true;
            return respond(request, "500 Internal Server Error");
        }
        local.sin_port = htons(5005U);
        if (bind(rtcp_.get(), reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
            ESP_LOGE(tag, "RTCP bind failed (errno %d)", errno);
            closing_ = true;
            return respond(request, "500 Internal Server Error");
        }
    }
    configured_ = true;
    std::array<char, 160U> header{};
    if (transport_.tcp) {
        static_cast<void>(std::snprintf(header.data(), header.size(),
            "Transport: RTP/AVP/TCP;unicast;interleaved=%u-%u;ssrc=%08" PRIX32 "\r\n",
            static_cast<unsigned>(transport_.first), static_cast<unsigned>(transport_.second), ssrc_));
    } else {
        static_cast<void>(std::snprintf(header.data(), header.size(),
            "Transport: RTP/AVP;unicast;client_port=%u-%u;server_port=5004-5005;ssrc=%08" PRIX32 "\r\n",
            static_cast<unsigned>(transport_.first), static_cast<unsigned>(transport_.second), ssrc_));
    }
    return respond(request, "200 OK", header.data());
}

bool RtspSession::handle(const rtsp::Request& request) noexcept
{
    if (request.method == "OPTIONS") {
        return respond(request, "200 OK",
            "Public: OPTIONS, DESCRIBE, SETUP, PLAY, PAUSE, TEARDOWN, GET_PARAMETER\r\n");
    }
    if (!rtsp::validUri(request.uri, request.method == "SETUP")) {
        return respond(request, "404 Not Found");
    }
    if (request.method == "DESCRIBE") {
        std::array<char, 512U> sdp{};
        const auto length = std::snprintf(sdp.data(), sdp.size(),
            "v=0\r\no=- %" PRIu32 " 1 IN IP4 0.0.0.0\r\ns=%s\r\nc=IN IP4 0.0.0.0\r\n"
            "t=0 0\r\na=control:*\r\nm=video 0 RTP/AVP 26\r\na=rtpmap:26 JPEG/90000\r\n"
            "a=framerate:10\r\na=control:trackID=0\r\n", ssrc_, Network::nodeName());
        if ((length <= 0) || (static_cast<std::size_t>(length) >= sdp.size())) { return false; }
        std::array<char, 160U> header{};
        static_cast<void>(std::snprintf(header.data(), header.size(),
            "Content-Type: application/sdp\r\nContent-Base: %s\r\n", base_.data()));
        return respond(request, "200 OK", header.data(), sdp.data());
    }
    if (request.method == "SETUP") { return setup(request); }
    if (!configured_ || (request.session != session_.data())) {
        return respond(request, "454 Session Not Found");
    }
    if (request.method == "GET_PARAMETER") { return respond(request, "200 OK"); }
    if (request.method == "PLAY") {
        if (!playing_) {
            const auto result = frames_.begin();
            if (result != ESP_OK) {
                return respond(request, (result == ESP_ERR_INVALID_STATE)
                    ? "453 Not Enough Bandwidth" : "500 Internal Server Error");
            }
            playing_ = true;
            startedUs_ = esp_timer_get_time();
            nextFrameUs_ = startedUs_;
            baseTimestamp_ = esp_random();
            meter_.reset(startedUs_);
        }
        std::array<char, 224U> header{};
        const auto timestamp = baseTimestamp_ + static_cast<std::uint32_t>(
            ((esp_timer_get_time() - startedUs_) * 9) / 100);
        static_cast<void>(std::snprintf(header.data(), header.size(),
            "Range: npt=0.000-\r\nRTP-Info: url=%strackID=0;seq=%u;rtptime=%" PRIu32 "\r\n",
            base_.data(), static_cast<unsigned>(sequence_), timestamp));
        return respond(request, "200 OK", header.data());
    }
    if (request.method == "PAUSE") {
        end(false, false);
        return respond(request, "200 OK");
    }
    if (request.method == "TEARDOWN") {
        closing_ = true;
        return respond(request, "200 OK");
    }
    return respond(request, "501 Not Implemented");
}

void RtspSession::end(bool failed, bool sendBye) noexcept
{
    if (playing_) {
        std::array<std::uint8_t, 8U> bye{0x81U, 203U, 0U, 1U};
        rtsp::put32(bye, 4U, ssrc_);
        if (sendBye) { static_cast<void>(packet(bye, true)); }
        const auto result = frames_.finish(failed);
        if (result != ESP_OK) { ESP_LOGW(tag, "Camera standby failed: %s", esp_err_to_name(result)); }
        playing_ = false;
    }
}

bool RtspSession::packet(std::span<const std::uint8_t> data, bool control) noexcept
{
    if (transport_.tcp) {
        std::array<std::uint8_t, 4U> header{0x24U,
            static_cast<std::uint8_t>(control ? transport_.second : transport_.first), 0U, 0U};
        rtsp::put16(header, 2U, static_cast<std::uint16_t>(data.size()));
        return sendAll(client_.get(), header.data(), header.size())
            && sendAll(client_.get(), data.data(), data.size());
    }
    auto destination = peer_;
    destination.sin_port = htons(control ? transport_.second : transport_.first);
    for (std::uint32_t attempt{0U}; attempt < 50U; ++attempt) {
        const auto sent = sendto(control ? rtcp_.get() : rtp_.get(), data.data(), data.size(), 0,
            reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
        if ((sent >= 0) && (static_cast<std::size_t>(sent) == data.size())) { return true; }
        const auto error = errno;
        if ((sent >= 0) || ((error != ENOMEM) && (error != ENOBUFS) && (error != EAGAIN)
            && (error != EWOULDBLOCK) && (error != EINTR))) {
            ESP_LOGE(tag, "UDP send failed (errno %d)", error);
            return false;
        }
        if (!control && state_.shouldStop(esp_timer_get_time())) { return false; }
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
    ESP_LOGE(tag, "UDP send remained blocked after bounded retries (errno %d)", errno);
    return false;
}

bool RtspSession::frame() noexcept
{
    const auto before = state_.snapshot();
    if (!before.hasApplied || (before.selected != before.applied)) { meter_.reset(esp_timer_get_time()); }
    const auto captured = frames_.capture(false);
    if (captured != ESP_OK) { end(true); return false; }
    if (state_.shouldStop(esp_timer_get_time())) { return false; }
    const auto view = frames_.streamView();
    rtsp::Jpeg jpeg{};
    if (!rtsp::parseJpeg(view, jpeg)) {
        ESP_LOGE(tag, "Camera JPEG is not supported by RFC 2435");
        state_.captureFailed();
        state_.cameraFailed(ESP_ERR_INVALID_RESPONSE);
        end(true);
        return false;
    }
    const auto timestamp = baseTimestamp_ + static_cast<std::uint32_t>(
        ((esp_timer_get_time() - startedUs_) * 9) / 100);
    rtsp::Packetizer sender{jpeg, sequence_, timestamp, ssrc_};
    std::array<std::uint8_t, rtsp::Packetizer::capacity> buffer{};
    std::uint32_t burst{0U};
    for (;;) {
        const auto length = sender.next(buffer);
        if (length == 0U) { break; }
        if (state_.shouldStop(esp_timer_get_time()) || !packet({buffer.data(), length}, false)) { return false; }
        ++packets_;
        octets_ += static_cast<std::uint32_t>(length - 12U);
        if (!transport_.tcp && (++burst % 8U == 0U)) { vTaskDelay(1U); }
    }
    sequence_ = sender.sequence();
    const auto now = esp_timer_get_time();
    state_.sent(view.width, view.height, view.size, now, meter_.sent(now, view.size));
    return true;
}

bool RtspSession::report() noexcept
{
    std::array<std::uint8_t, 80U> report{};
    report[0U] = 0x80U;
    report[1U] = 200U;
    rtsp::put16(report, 2U, 6U);
    rtsp::put32(report, 4U, ssrc_);
    timeval now{};
    static_cast<void>(gettimeofday(&now, nullptr));
    rtsp::put32(report, 8U, static_cast<std::uint32_t>(now.tv_sec + 2'208'988'800LL));
    rtsp::put32(report, 12U, static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(now.tv_usec) << 32U) / 1'000'000U));
    rtsp::put32(report, 16U, baseTimestamp_ + static_cast<std::uint32_t>(
        ((esp_timer_get_time() - startedUs_) * 9) / 100));
    rtsp::put32(report, 20U, packets_);
    rtsp::put32(report, 24U, octets_);
    const auto name = std::string_view{Network::nodeName()};
    const auto sdesLength = ((11U + name.size() + 3U) / 4U) * 4U;
    report[28U] = 0x81U;
    report[29U] = 202U;
    rtsp::put16(report, 30U, static_cast<std::uint16_t>((sdesLength / 4U) - 1U));
    rtsp::put32(report, 32U, ssrc_);
    report[36U] = 1U;
    report[37U] = static_cast<std::uint8_t>(name.size());
    std::copy(name.begin(), name.end(), report.begin() + 38U);
    return packet({report.data(), 28U + sdesLength}, true);
}

bool RtspSession::consume() noexcept
{
    while (used_ != 0U) {
        std::size_t consumed{0U};
        if (input_[0U] == 0x24U) {
            if (used_ < 4U) { return true; }
            const auto length = (static_cast<std::size_t>(input_[2U]) << 8U) | input_[3U];
            if (!configured_ || !transport_.tcp || (input_[1U] != transport_.second)
                || (length > 1500U)) { return false; }
            consumed = length + 4U;
            if (used_ < consumed) { return true; }
        } else {
            // D02: read byte representation as chars for bounded protocol parsing.
            const std::string_view input{reinterpret_cast<const char*>(input_.data()), used_};
            const auto end = input.find("\r\n\r\n");
            if (end == std::string_view::npos) { return used_ < input_.size(); }
            std::size_t body{0U};
            if (!rtsp::contentLength(input.substr(0U, end + 4U), body)) { return false; }
            consumed = end + 4U + body;
            if (consumed > input_.size()) { return false; }
            if (used_ < consumed) { return true; }
            rtsp::Request request{};
            if (!rtsp::parseRequest(input.substr(0U, consumed), request) || !handle(request)) { return false; }
        }
        used_ -= consumed;
        std::memmove(input_.data(), input_.data() + consumed, used_);
        lastControlUs_ = esp_timer_get_time();
        if (closing_) { return false; }
    }
    return true;
}

void RtspSession::run() noexcept
{
    if (!timeouts(client_.get())) { return; }
    const int noDelay{1};
    if (setsockopt(client_.get(), IPPROTO_TCP, TCP_NODELAY, &noDelay, sizeof(noDelay)) != 0) { return; }
    sockaddr_in local{};
    socklen_t size{sizeof(local)};
    if (getsockname(client_.get(), reinterpret_cast<sockaddr*>(&local), &size) != 0) { return; }
    std::array<char, INET_ADDRSTRLEN> address{};
    if (inet_ntop(AF_INET, &local.sin_addr, address.data(), address.size()) == nullptr) { return; }
    static_cast<void>(std::snprintf(base_.data(), base_.size(), "rtsp://%s:%u/stream/", address.data(),
        static_cast<unsigned>(ntohs(local.sin_port))));
    ssrc_ = esp_random();
    sequence_ = static_cast<std::uint16_t>(esp_random());
    static_cast<void>(std::snprintf(session_.data(), session_.size(), "%08" PRIx32, esp_random()));
    lastControlUs_ = esp_timer_get_time();
    ESP_LOGI(tag, "Client connected");
    while (!closing_) {
        if ((esp_timer_get_time() - lastControlUs_) >= 60'000'000) { break; }
        if (playing_ && state_.shouldStop(esp_timer_get_time())) { break; }
        std::array<pollfd, 2U> readSet{};
        readSet[0U] = {client_.get(), POLLIN, 0};
        readSet[1U] = {(configured_ && !transport_.tcp) ? rtcp_.get() : -1, POLLIN, 0};
        const auto ready = poll(readSet.data(), readSet.size(), playing_ ? 10 : 100);
        if ((ready < 0) && (errno == EINTR)) { continue; }
        if (ready < 0) { ESP_LOGE(tag, "Socket poll failed (errno %d)", errno); break; }
        if (((static_cast<unsigned>(readSet[0U].revents) & (POLLIN | POLLHUP | POLLERR)) != 0)) {
            if (used_ == input_.size()) { break; }
            const auto received = recv(client_.get(), input_.data() + used_, input_.size() - used_, 0);
            if (received <= 0) {
                if (received < 0) { ESP_LOGW(tag, "Control receive failed (errno %d)", errno); }
                break;
            }
            used_ += static_cast<std::size_t>(received);
            if (!consume()) { break; }
        }
        if (configured_ && !transport_.tcp && ((static_cast<unsigned>(readSet[1U].revents) & POLLIN) != 0)) {
            std::array<std::uint8_t, 1500U> feedback{};
            sockaddr_in source{};
            socklen_t sourceSize{sizeof(source)};
            const auto bytes = recvfrom(rtcp_.get(), feedback.data(), feedback.size(), 0,
                reinterpret_cast<sockaddr*>(&source), &sourceSize);
            if ((bytes >= 8) && (source.sin_addr.s_addr == peer_.sin_addr.s_addr)
                && (source.sin_port == htons(transport_.second)) && ((feedback[0U] >> 6U) == 2U)) {
                lastControlUs_ = esp_timer_get_time();
            }
        }
        const auto now = esp_timer_get_time();
        if (playing_ && (now >= nextFrameUs_)) {
            if (!frame()) { break; }
            nextFrameUs_ = now + 100'000; // At most 10 captures/s; status measures actual sends.
            if ((now - lastReportUs_) >= 5'000'000) {
                if (!report()) { break; }
                lastReportUs_ = now;
            }
        }
    }
    end(false);
    ESP_LOGI(tag, "Client disconnected; standby requested");
}
} // namespace

esp_err_t RtspServer::start() noexcept
{
    if (listener_ >= 0) { return ESP_ERR_INVALID_STATE; }
    listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener_ < 0) { return ESP_FAIL; }
    const int reuse{1};
    static_cast<void>(setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port_);
    if ((bind(listener_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
        || (listen(listener_, 1) != 0)
        || (xTaskCreate(&entry, "rtsp", 12288U, this, 5U, nullptr) != pdPASS)) {
        static_cast<void>(close(listener_));
        listener_ = -1;
        return ESP_FAIL;
    }
    ESP_LOGI(tag, "Ready on rtsp://<camera-ip>:%u/stream (JPEG, UDP or TCP)", static_cast<unsigned>(port_));
    return ESP_OK;
}
void RtspServer::entry(void* context) noexcept
{
    if (context != nullptr) { static_cast<RtspServer*>(context)->run(); }
    vTaskDelete(nullptr);
}
void RtspServer::run() noexcept
{
    for (;;) {
        sockaddr_in peer{};
        socklen_t size{sizeof(peer)};
        const auto client = accept(listener_, reinterpret_cast<sockaddr*>(&peer), &size);
        if (client < 0) { vTaskDelay(pdMS_TO_TICKS(100U)); continue; }
        RtspSession session{client, peer, frames_, state_};
        session.run();
    }
}
} // namespace cam
