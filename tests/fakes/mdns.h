/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef TEST_MDNS_H
#define TEST_MDNS_H
#include <cstdint>
#include "esp_err.h"
struct esp_ip4_addr_t { std::uint32_t addr; };
esp_err_t mdns_init();
void mdns_free();
esp_err_t mdns_query_a(const char*, std::uint32_t, esp_ip4_addr_t*);
char* esp_ip4addr_ntoa(const esp_ip4_addr_t*, char*, int);
#endif
