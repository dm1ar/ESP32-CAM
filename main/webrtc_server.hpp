/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef ESP32_CAM_WEBRTC_SERVER_HPP
#define ESP32_CAM_WEBRTC_SERVER_HPP
#include <atomic>
#include <span>
#include "frame_service.hpp"
#include "esp_peer.h"
#include "esp_peer_default.h"

namespace cam {
// One worker owns all peer APIs. HTTP submits synchronous commands; peer
// callbacks only publish bounded SDP or atomic state. No camera API here.
class WebRtcServer final {
public:
    static constexpr std::size_t sdpCapacity{4096U};
    WebRtcServer(FrameService& frames, SharedState& state) noexcept : frames_{frames}, state_{state} {}
    WebRtcServer(const WebRtcServer&) = delete;
    WebRtcServer& operator=(const WebRtcServer&) = delete;
    [[nodiscard]] esp_err_t start() noexcept;
    [[nodiscard]] esp_err_t offer(std::span<char> sdp) noexcept;
    [[nodiscard]] esp_err_t stop() noexcept;
    [[nodiscard]] std::span<const char> answer() const noexcept { return {answer_.data(), answerSize_}; }
    [[nodiscard]] const char* phase() const noexcept;
private:
    enum class Phase : std::uint8_t { idle, negotiating, connecting, streaming, error };
    struct Request final {
        bool stop;
        std::span<char> sdp;
        SemaphoreHandle_t done;
        esp_err_t result{ESP_FAIL};
    };
    [[nodiscard]] esp_err_t submit(bool stop, std::span<char> sdp) noexcept;
    [[nodiscard]] esp_err_t connect(std::span<char> sdp) noexcept;
    void close(bool failed) noexcept;
    void sendChunk() noexcept;
    static void entry(void* context) noexcept;
    void run() noexcept;
    static int onState(esp_peer_state_t state, void* context) noexcept;
    static int onMessage(esp_peer_msg_t* message, void* context) noexcept;
    static int onOpen(esp_peer_data_channel_info_t* channel, void* context) noexcept;
    static int onData(esp_peer_data_frame_t* frame, void* context) noexcept;
    FrameService& frames_;
    SharedState& state_;
    esp_peer_handle_t peer_{nullptr};
    esp_peer_default_cfg_t extra_{};
    StaticQueue_t queueStorage_{};
    std::array<std::uint8_t, 2U * sizeof(Request*)> queueBytes_{};
    QueueHandle_t queue_{nullptr};
    portMUX_TYPE answerLock_ = portMUX_INITIALIZER_UNLOCKED;
    std::array<char, sdpCapacity> answer_{};
    std::array<char, sdpCapacity> remote_{};
    std::size_t answerSize_{0U};
    bool negotiating_{false}; // Protected by answerLock_.
    std::atomic<bool> answerReady_{false};
    std::atomic<bool> connected_{false};
    std::atomic<bool> failed_{false};
    std::atomic<bool> requested_{false};
    std::atomic<bool> receivedAck_{false};
    std::atomic<std::uint16_t> channel_{0U};
    std::atomic<Phase> phase_{Phase::idle};
    bool ownsVideo_{false};
    JpegView current_{};
    std::size_t offset_{0U};
    std::uint32_t frameId_{0U};
    std::int64_t lastProgressUs_{0};
    std::int64_t nextFrameUs_{0};
    RateMeter meter_{};
};
} // namespace cam
#endif
