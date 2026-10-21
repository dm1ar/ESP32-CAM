/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "webrtc_server.hpp"
#include "webrtc_protocol.hpp"
#include "host_sdk.hpp"
#include "mdns.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
namespace {
std::mutex mutex;
esp_peer_cfg_t callbacks{};
std::vector<std::uint8_t> packet;
unsigned sent{0U};
unsigned mdnsQueries{0U}, mdnsStarts{0U}, mdnsStops{0U};
bool opened{false}, blocked{false};
void require(bool value, const char* message) { if (!value) { std::fprintf(stderr,"FAIL WebRTC: %s\n",message); std::abort(); } }
template<class Predicate> void wait(Predicate predicate)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{4};
    while (!predicate()) {
        require(std::chrono::steady_clock::now() < end, "worker completion timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
}
void next()
{
    std::uint8_t text[]{'n','e','x','t'};
    esp_peer_data_frame_t data{}; data.data = text; data.size = 4;
    esp_peer_cfg_t cfg{}; { const std::lock_guard guard{mutex}; cfg = callbacks; }
    require(cfg.on_data(&data,cfg.ctx) == 0,"ack callback");
}
}
esp_err_t mdns_init() { ++mdnsStarts; return ESP_OK; }
void mdns_free() { ++mdnsStops; }
esp_err_t mdns_query_a(const char* name, std::uint32_t timeout, esp_ip4_addr_t* address)
{
    require(timeout == 2000U, "bounded mDNS hostname lookup");
    ++mdnsQueries;
    if (std::strcmp(name, "missing") == 0) { return ESP_FAIL; }
    require(std::strcmp(name, "browser") == 0, "mDNS hostname without .local suffix");
    address->addr = 1U; return ESP_OK;
}
char* esp_ip4addr_ntoa(const esp_ip4_addr_t*, char* output, int size)
{
    require(size >= 16, "IPv4 output buffer"); std::strcpy(output, "192.168.1.126"); return output;
}
const esp_peer_ops_t* esp_peer_get_default_impl() { static const esp_peer_ops_t operations{}; return &operations; }
int esp_peer_open(esp_peer_cfg_t* cfg, const esp_peer_ops_t*, esp_peer_handle_t* handle)
{
    const std::lock_guard guard{mutex}; callbacks = *cfg; opened = false;
    require(mdnsStarts == mdnsStops, "mDNS task freed before peer allocation");
    require(cfg->role == ESP_PEER_ROLE_CONTROLLED && cfg->enable_data_channel && cfg->manual_ch_create,
            "data-only controlled peer config");
    const auto& extra = *static_cast<esp_peer_default_cfg_t*>(cfg->extra_cfg);
    require(extra.ice_use_lite_mode && extra.data_ch_cfg.send_cache_size == 32768U,"bounded LAN ICE config");
    *handle = &callbacks; return 0;
}
int esp_peer_new_connection(esp_peer_handle_t) { return 0; }
int esp_peer_send_msg(esp_peer_handle_t, esp_peer_msg_t* remote)
{
    const std::string_view sdp{reinterpret_cast<const char*>(remote->data), static_cast<std::size_t>(remote->size)};
    require(sdp.find(".local") == std::string_view::npos, "peer never receives unresolved mDNS candidates");
    std::uint8_t answer[]{"v=0\r\na=setup:passive\r\nm=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"};
    esp_peer_msg_t message{};message.type=ESP_PEER_MSG_TYPE_SDP;message.data=answer;message.size=sizeof(answer)-1;
    return callbacks.on_msg(&message,callbacks.ctx);
}
int esp_peer_main_loop(esp_peer_handle_t)
{
    if (!opened) { opened = true; esp_peer_data_channel_info_t channel{"jpeg", 7U}; callbacks.on_channel_open(&channel,callbacks.ctx); }
    return 0;
}
int esp_peer_send_data(esp_peer_handle_t, esp_peer_data_frame_t* data)
{
    const std::lock_guard guard{mutex};
    if (!blocked) { blocked = true; return ESP_PEER_ERR_WOULD_BLOCK; }
    require(data->stream_id == 7U && data->size > 16 && data->size <= 4112,"chunk size and stream id");
    packet.assign(data->data,data->data+data->size); ++sent; return 0;
}
int esp_peer_close(esp_peer_handle_t) { callbacks.on_state(ESP_PEER_STATE_DISCONNECTED, callbacks.ctx); return 0; }
int main(int argc, char** argv)
{
    require(argc == 2 && loadCameraFixture(argv[1]),"fixture");
    static cam::Camera camera{};
    static cam::SharedState state{cam::StreamPolicy{true,2U}};
    static cam::FrameService frames{camera,state};
    static cam::WebRtcServer server{frames,state};
    require(frames.start() == ESP_OK && server.start() == ESP_OK,"start workers");
    char bad[]{"invalid"};require(server.offer({bad,sizeof(bad)-1}) == ESP_ERR_INVALID_ARG,"reject bad SDP");
    char offer[]{"v=0\r\nm=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
        "a=candidate:1 1 udp 123 browser.local 12345 typ host\r\n"};
    require(frames.begin() == ESP_OK,"claim alternate video");
    require(server.offer({offer,sizeof(offer)-1}) == ESP_ERR_INVALID_STATE,"reject competing video");
    require(state.snapshot().streaming,"rejection preserves other owner");
    require(server.stop() == ESP_OK && state.snapshot().streaming,"idle WebRTC Stop preserves other owner");
    require(frames.finish(false) == ESP_OK,"release other owner");
    require(server.offer({offer,sizeof(offer)-1}) == ESP_OK,"negotiate through worker");
    require(!server.answer().empty(),"completed SDP response");
    require(mdnsQueries == 1U && mdnsStarts == 1U && mdnsStops == 1U, "browser mDNS resolved with scoped lifetime");
    std::size_t offset{0U}, total{0U}; unsigned expected{0U};
    do {
        next(); ++expected;
        wait([expected] { const std::lock_guard guard{mutex}; return sent >= expected; });
        const std::lock_guard guard{mutex};
        const auto value=[&](std::size_t at) { return (static_cast<std::uint32_t>(packet[at])<<24U)
            | (static_cast<std::uint32_t>(packet[at+1])<<16U) | (static_cast<std::uint32_t>(packet[at+2])<<8U) | packet[at+3]; };
        total=value(8U);require(value(12U)==offset,"ack advances offset once, retry cannot skip bytes");
        offset+=packet.size()-16U;
    } while (offset<total);
    next();wait([&] { return state.snapshot().framesSent >= 1U; });
    require(frames.capture(true) == ESP_OK,"snapshot coexists with WebRTC stream");
    require(server.stop() == ESP_OK && !state.snapshot().streaming,"explicit Stop cleans up camera");
    require(server.offer({offer,sizeof(offer)-1}) == ESP_OK,"reconnect");
    state.requestStop();wait([&] { return !state.snapshot().streaming; });
    require(server.offer({offer,sizeof(offer)-1}) == ESP_OK,"new stream after HTTP Stop");
    wait([&] { return state.snapshot().streamTimedOut && !state.snapshot().streaming; });
    char missing[]{"v=0\r\nm=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
        "a=candidate:1 1 udp 123 missing.local 12345 typ host\r\n"};
    require(server.offer({missing,sizeof(missing)-1}) == ESP_ERR_INVALID_RESPONSE,"unresolved browser rejected explicitly");
    require(!state.snapshot().streaming && state.snapshot().cameraError == 0,"lookup failure releases camera without hardware error");
    require(mdnsStarts == mdnsStops,"lookup failure frees resolver");
    std::puts("PASS: WebRTC worker negotiation, exclusive ownership, ACK pacing, retry, snapshots, Stop and device deadline (peer API mocked)");
    std::fflush(stdout);std::_Exit(0);
}
