/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef TEST_ESP_LOG_H
#define TEST_ESP_LOG_H
template<typename... Args> inline void testLog(const char*, const char*, Args...) {}
#define ESP_LOGI(...) testLog(__VA_ARGS__)
#define ESP_LOGW(...) testLog(__VA_ARGS__)
#define ESP_LOGE(...) testLog(__VA_ARGS__)
#endif
