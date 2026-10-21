/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef TEST_ESP_ERR_H
#define TEST_ESP_ERR_H
// Minimal host-only ABI surface. Real firmware never includes tests/fakes.
using esp_err_t = int;
inline constexpr esp_err_t ESP_OK{0};
inline constexpr esp_err_t ESP_FAIL{-1};
inline constexpr esp_err_t ESP_ERR_NOT_FOUND{0x105};
inline constexpr esp_err_t ESP_ERR_INVALID_ARG{0x102};
inline constexpr esp_err_t ESP_ERR_NO_MEM{0x101};
inline constexpr esp_err_t ESP_ERR_INVALID_STATE{0x103};
inline constexpr esp_err_t ESP_ERR_INVALID_SIZE{0x104};
inline constexpr esp_err_t ESP_ERR_INVALID_RESPONSE{0x108};
const char* esp_err_to_name(esp_err_t result);
#endif
