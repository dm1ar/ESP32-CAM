/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "webrtc_server.hpp"
#include "webrtc_protocol.hpp"
#include "webrtc_sdp.hpp"
#include "mdns.h"
#include <cstring>
#include <string_view>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"

namespace cam {
esp_err_t WebRtcServer::start() noexcept
{
    if (queue_ != nullptr) { return ESP_ERR_INVALID_STATE; }
    queue_ = xQueueCreateStatic(2U, sizeof(Request*), queueBytes_.data(), &queueStorage_);
    if (queue_ == nullptr) { return ESP_ERR_NO_MEM; }
    if (xTaskCreate(&entry, "webrtc", 12288U, this, 5U, nullptr) != pdPASS) {
        vQueueDelete(queue_); queue_ = nullptr; return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
const char* WebRtcServer::phase() const noexcept
{
    switch (phase_.load()) {
    case Phase::idle: return "idle";
    case Phase::negotiating: return "negotiating";
    case Phase::connecting: return "connecting";
    case Phase::streaming: return "streaming";
    case Phase::error: return "error";
    }
    return "error";
}
esp_err_t WebRtcServer::submit(bool stopRequest, std::span<char> sdp) noexcept
{
    if (queue_ == nullptr) { return ESP_ERR_INVALID_STATE; }
    StaticSemaphore_t storage{};
    const auto done = xSemaphoreCreateBinaryStatic(&storage);
    if (done == nullptr) { return ESP_ERR_NO_MEM; }
    Request request{stopRequest, sdp, done};
    auto* pointer = &request;
    const auto sent = xQueueSend(queue_, &pointer, portMAX_DELAY);
    const auto completed = (sent == pdTRUE) ? xSemaphoreTake(done, portMAX_DELAY) : pdFALSE;
    vSemaphoreDelete(done);
    return (completed == pdTRUE) ? request.result : ESP_FAIL;
}
esp_err_t WebRtcServer::offer(std::span<char> sdp) noexcept { return submit(false, sdp); }
esp_err_t WebRtcServer::stop() noexcept { return submit(true, {}); }
int WebRtcServer::onState(esp_peer_state_t state, void* context) noexcept
{
    if (context == nullptr) { return -1; }
    auto& self = *static_cast<WebRtcServer*>(context);
    ESP_LOGI("webrtc", "Peer state %d", static_cast<int>(state));
    if ((state == ESP_PEER_STATE_CONNECT_FAILED)
        || ((state == ESP_PEER_STATE_DISCONNECTED) && (self.phase_ != Phase::negotiating))
        || (state == ESP_PEER_STATE_DATA_CHANNEL_CLOSED)
        || (state == ESP_PEER_STATE_DATA_CHANNEL_DISCONNECTED)) { self.failed_ = true; }
    return 0;
}
int WebRtcServer::onMessage(esp_peer_msg_t* message, void* context) noexcept
{
    if ((context == nullptr) || (message == nullptr)) { return -1; }
    auto& self = *static_cast<WebRtcServer*>(context);
    if (message->type != ESP_PEER_MSG_TYPE_SDP) { return 0; }
    if ((message->data == nullptr) || (message->size <= 0)
        || (static_cast<std::size_t>(message->size) >= self.answer_.size())) { self.failed_ = true; return -1; }
    const CriticalSection guard{self.answerLock_};
    if (!self.negotiating_) { return 0; }
    auto size = static_cast<std::size_t>(message->size);
    if (message->data[size - 1U] == 0U) { --size; }
    std::memcpy(self.answer_.data(), message->data, size);
    self.answer_[size] = '\0';
    self.answerSize_ = size;
    self.answerReady_ = true;
    return 0;
}
int WebRtcServer::onOpen(esp_peer_data_channel_info_t* channel, void* context) noexcept
{
    if ((context == nullptr) || (channel == nullptr)) { return -1; }
    auto& self = *static_cast<WebRtcServer*>(context);
    self.channel_ = channel->stream_id;
    self.connected_ = true;
    return 0;
}
int WebRtcServer::onData(esp_peer_data_frame_t* frame, void* context) noexcept
{
    if ((context == nullptr) || (frame == nullptr) || (frame->data == nullptr)) { return -1; }
    auto& self = *static_cast<WebRtcServer*>(context);
    if ((frame->size == 4) && (std::memcmp(frame->data, "next", 4U) == 0)) { self.requested_ = true; self.receivedAck_ = true; }
    return 0;
}
void WebRtcServer::close(bool failed) noexcept
{
    { const CriticalSection guard{answerLock_}; negotiating_ = false; }
    if (peer_ != nullptr) { static_cast<void>(esp_peer_close(peer_)); peer_ = nullptr; }
    if (ownsVideo_) {
        const auto result = frames_.finish(failed && (state_.snapshot().cameraError != 0));
        if (result != ESP_OK) { ESP_LOGW("webrtc", "Camera cleanup failed"); }
        ownsVideo_ = false;
    }
    current_ = {}; offset_ = 0U;
    connected_ = false; requested_ = false; receivedAck_ = false; failed_ = false;
    phase_ = failed ? Phase::error : Phase::idle;
}
esp_err_t WebRtcServer::connect(std::span<char> sdp) noexcept
{
    if (peer_ != nullptr) { return ESP_ERR_INVALID_STATE; }
    if (sdp.empty() || (sdp.size() >= sdpCapacity)) { return ESP_ERR_INVALID_SIZE; }
    const std::string_view text{sdp.data(), sdp.size()};
    if (!text.starts_with("v=0") || (text.find("m=application") == std::string_view::npos)
        || (text.find('\0') != std::string_view::npos)) { return ESP_ERR_INVALID_ARG; }
    const auto claimed = frames_.begin();
    if (claimed != ESP_OK) { return claimed; }
    ownsVideo_ = true; failed_ = false; requested_ = false; connected_ = false;
    bool mdnsStarted{false};
    const auto remoteSize = webrtc::resolveSdp(text, remote_, [&mdnsStarted](std::string_view host, std::span<char> ip) {
        if (!mdnsStarted) {
            if (mdns_init() != ESP_OK) { return std::size_t{0U}; }
            mdnsStarted = true;
        }
        std::array<char, 64U> name{};
        std::memcpy(name.data(), host.data(), host.size());
        esp_ip4_addr_t address{};
        if (mdns_query_a(name.data(), 2000U, &address) != ESP_OK) {
            ESP_LOGW("webrtc", "Browser ICE mDNS lookup failed");
            return std::size_t{0U};
        }
        if (esp_ip4addr_ntoa(&address, ip.data(), static_cast<int>(ip.size())) == nullptr) { return std::size_t{0U}; }
        ESP_LOGI("webrtc", "Resolved browser ICE mDNS address");
        return std::strlen(ip.data());
    });
    // Query-only mDNS lifetime ends before DTLS/SCTP allocation to preserve RAM.
    if (mdnsStarted) { mdns_free(); }
    if (remoteSize == 0U) {
        ESP_LOGE("webrtc", "No usable browser ICE address after SDP resolution");
        close(true); return ESP_ERR_INVALID_RESPONSE;
    }
    answerReady_ = false; answerSize_ = 0U;
    phase_ = Phase::negotiating;
    { const CriticalSection guard{answerLock_}; negotiating_ = false; }
    extra_ = {};
    extra_.agent_recv_timeout = 20U;
    extra_.max_candidates = 2U;
    extra_.keep_role = true;
    extra_.ice_use_lite_mode = true;
    extra_.data_ch_cfg.send_cache_size = 32768U;
    extra_.data_ch_cfg.recv_cache_size = 32768U;
    extra_.rtp_cfg.send_pool_size = 4096U;
    extra_.rtp_cfg.send_queue_num = 8U;
    esp_peer_cfg_t config{};
    config.role = ESP_PEER_ROLE_CONTROLLED;
    config.no_auto_reconnect = true;
    config.enable_data_channel = true;
    config.manual_ch_create = true;
    config.ctx = this;
    config.on_state = &onState;
    config.on_msg = &onMessage;
    config.on_channel_open = &onOpen;
    config.on_data = &onData;
    config.extra_cfg = &extra_;
    config.extra_size = static_cast<int>(sizeof(extra_));
    auto result = esp_peer_open(&config, esp_peer_get_default_impl(), &peer_);
    if (result == 0) { result = esp_peer_new_connection(peer_); }
    if (result == 0) {
        { const CriticalSection guard{answerLock_}; negotiating_ = true; }
        // The resolved SDP has persistent storage and a trailing NUL.
        esp_peer_msg_t message{};
        message.type = ESP_PEER_MSG_TYPE_SDP;
        message.data = reinterpret_cast<std::uint8_t*>(remote_.data());
        message.size = static_cast<int>(remoteSize);
        result = esp_peer_send_msg(peer_, &message);
    }
    const auto deadline = esp_timer_get_time() + 10'000'000;
    while ((result == 0) && !answerReady_ && !failed_ && (esp_timer_get_time() < deadline)) {
        result = esp_peer_main_loop(peer_);
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
    { const CriticalSection guard{answerLock_}; negotiating_ = false; }
    if ((result != 0) || failed_ || !answerReady_) {
        ESP_LOGE("webrtc", "Negotiation failed: peer=%d, answer=%d", result, answerReady_.load());
        close(true);
        return ESP_FAIL;
    }
    phase_ = Phase::connecting;
    lastProgressUs_ = esp_timer_get_time(); nextFrameUs_ = lastProgressUs_;
    meter_.reset(lastProgressUs_);
    return ESP_OK;
}
void WebRtcServer::sendChunk() noexcept
{
    const auto now = esp_timer_get_time();
    if ((offset_ >= current_.size) && (now < nextFrameUs_)) { return; }
    if (!requested_.exchange(false)) { return; }
    if (receivedAck_.exchange(false)) { lastProgressUs_ = now; }
    if ((current_.size != 0U) && (offset_ >= current_.size)) {
        state_.sent(current_.width, current_.height, current_.size, now, meter_.sent(now, current_.size));
        current_ = {};
    }
    if (current_.size == 0U) {
        const auto result = frames_.capture(false);
        if (result != ESP_OK) { close(true); return; }
        if (state_.shouldStop(esp_timer_get_time())) { close(false); return; }
        current_ = frames_.streamView(); offset_ = 0U; ++frameId_;
        nextFrameUs_ = now + 200'000; // Conservative maximum of five frames/s.
    }
    std::array<std::uint8_t, webrtc::headerSize + webrtc::payloadSize> packet{};
    const auto length = webrtc::chunk({current_.data, current_.size}, frameId_, offset_, packet);
    esp_peer_data_frame_t data{};
    data.type = ESP_PEER_DATA_CHANNEL_DATA;
    data.stream_id = channel_.load();
    data.data = packet.data();
    data.size = static_cast<int>(length);
    const auto result = (length == 0U) ? ESP_PEER_ERR_BAD_DATA : esp_peer_send_data(peer_, &data);
    if (result == ESP_PEER_ERR_WOULD_BLOCK) { requested_ = true; return; }
    if (result != 0) { ESP_LOGE("webrtc", "Data send failed: %d", result); close(true); return; }
    offset_ += length - webrtc::headerSize;
}
void WebRtcServer::entry(void* context) noexcept
{
    if (context != nullptr) { static_cast<WebRtcServer*>(context)->run(); }
    vTaskDelete(nullptr);
}
void WebRtcServer::run() noexcept
{
    for (;;) {
        Request* request{nullptr};
        if (xQueueReceive(queue_, &request, peer_ == nullptr ? portMAX_DELAY : 0U) == pdTRUE) {
            if (request != nullptr) {
                request->result = request->stop ? (close(false), ESP_OK) : connect(request->sdp);
                const auto done = request->done;
                static_cast<void>(xSemaphoreGive(done));
            }
        }
        if (peer_ != nullptr) {
            const auto result = esp_peer_main_loop(peer_);
            const auto timeout = connected_ ? 10'000'000 : 30'000'000;
            if (failed_ || (result != 0) || state_.shouldStop(esp_timer_get_time())
                || ((esp_timer_get_time() - lastProgressUs_) >= timeout)) { close(failed_ || (result != 0)); }
            else if (connected_) { phase_ = Phase::streaming; sendChunk(); }
            vTaskDelay(pdMS_TO_TICKS(10U));
        }
    }
}
} // namespace cam
