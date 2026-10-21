/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef TEST_ESP_PEER_DEFAULT_H
#define TEST_ESP_PEER_DEFAULT_H
#include "esp_peer.h"
struct esp_peer_default_cfg_t {
    std::uint16_t agent_recv_timeout{};
    std::uint8_t max_candidates{};
    bool keep_role{}, ice_use_lite_mode{};
    struct { std::uint32_t send_cache_size{}, recv_cache_size{}; } data_ch_cfg;
    struct { std::uint32_t send_pool_size{}, send_queue_num{}; } rtp_cfg;
};
const esp_peer_ops_t* esp_peer_get_default_impl();
#endif
