/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef TEST_ESP_PEER_H
#define TEST_ESP_PEER_H
#include <cstdint>
using esp_peer_handle_t = void*;
enum esp_peer_state_t { ESP_PEER_STATE_DISCONNECTED=1, ESP_PEER_STATE_CONNECT_FAILED=8,
    ESP_PEER_STATE_DATA_CHANNEL_CLOSED=11, ESP_PEER_STATE_DATA_CHANNEL_DISCONNECTED=12 };
enum { ESP_PEER_ERR_NONE=0, ESP_PEER_ERR_BAD_DATA=-8, ESP_PEER_ERR_WOULD_BLOCK=-9 };
enum esp_peer_role_t { ESP_PEER_ROLE_CONTROLLED };
enum esp_peer_msg_type_t { ESP_PEER_MSG_TYPE_SDP, ESP_PEER_MSG_TYPE_CANDIDATE };
enum esp_peer_data_channel_type_t { ESP_PEER_DATA_CHANNEL_DATA, ESP_PEER_DATA_CHANNEL_STRING };
struct esp_peer_msg_t { esp_peer_msg_type_t type{}; std::uint8_t* data{}; int size{}; };
struct esp_peer_data_frame_t { esp_peer_data_channel_type_t type{}; std::uint16_t stream_id{}; std::uint8_t* data{}; int size{}; };
struct esp_peer_data_channel_info_t { const char* label{}; std::uint16_t stream_id{}; };
struct esp_peer_ops_t {};
struct esp_peer_cfg_t {
    esp_peer_role_t role{};
    bool no_auto_reconnect{}, enable_data_channel{}, manual_ch_create{};
    void* ctx{};
    int (*on_state)(esp_peer_state_t, void*){};
    int (*on_msg)(esp_peer_msg_t*, void*){};
    int (*on_channel_open)(esp_peer_data_channel_info_t*, void*){};
    int (*on_data)(esp_peer_data_frame_t*, void*){};
    void* extra_cfg{};
    int extra_size{};
};
int esp_peer_open(esp_peer_cfg_t* config, const esp_peer_ops_t* operations, esp_peer_handle_t* handle);
int esp_peer_new_connection(esp_peer_handle_t peer);
int esp_peer_send_msg(esp_peer_handle_t peer, esp_peer_msg_t* message);
int esp_peer_main_loop(esp_peer_handle_t peer);
int esp_peer_send_data(esp_peer_handle_t peer, esp_peer_data_frame_t* data);
int esp_peer_close(esp_peer_handle_t peer);
#endif
