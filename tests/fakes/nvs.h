/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef TEST_NVS_H
#define TEST_NVS_H
#include <cstddef>
#include <cstdint>
#include "esp_err.h"
using nvs_handle_t = std::uint32_t;
inline constexpr int NVS_READONLY = 0;
esp_err_t nvs_open_from_partition(const char*, const char*, int, nvs_handle_t*);
esp_err_t nvs_get_str(nvs_handle_t, const char*, char*, std::size_t*);
esp_err_t nvs_get_u32(nvs_handle_t, const char*, std::uint32_t*);
esp_err_t nvs_get_u8(nvs_handle_t, const char*, std::uint8_t*);
void nvs_close(nvs_handle_t);
#endif
