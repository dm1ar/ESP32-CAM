/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef TEST_SEMPHR_H
#define TEST_SEMPHR_H
#include <condition_variable>
#include <mutex>
struct StaticSemaphore_t {
    std::mutex mutex;
    std::condition_variable changed;
    bool available{false};
};
using SemaphoreHandle_t = StaticSemaphore_t*;
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t* storage);
int xSemaphoreTake(SemaphoreHandle_t semaphore, std::uint32_t ticks);
int xSemaphoreGive(SemaphoreHandle_t semaphore);
void vSemaphoreDelete(SemaphoreHandle_t semaphore);
#endif
