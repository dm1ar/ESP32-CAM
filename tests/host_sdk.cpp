/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

// Host adapters run the actual RTSP server, packetizer, FrameService queue,
// FrameStore and Camera against synthetic JPEGs; never in firmware paths.
#include "rtsp_server.hpp"
#include "rtsp_protocol.hpp"
#include "network.hpp"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include <atomic>
#include <cerrno>
#include <sys/socket.h>
#include <chrono>
#include <cstdio>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <iterator>
#include <thread>
#include <vector>
namespace {
std::vector<std::uint8_t> fixture;
bool initialized{false};
bool borrowed{false};
camera_fb_t frame{};
sensor_t sensor{{0x26U}};
}
ssize_t test_sendto(int socket, const void* data, std::size_t length, int flags,
                    const sockaddr* destination, socklen_t size)
{
    static std::atomic<unsigned> calls{0U};
    if ((calls.fetch_add(1U) % 17U) == 0U) { errno = ENOMEM; return -1; }
    return ::sendto(socket, data, length, flags, destination, size);
}
void* heap_caps_malloc(std::size_t size, std::uint32_t) { return std::malloc(size); }
void heap_caps_free(void* pointer) { std::free(pointer); }
const char* esp_err_to_name(esp_err_t) { return "host error"; }
esp_err_t gpio_set_direction(gpio_num_t, gpio_mode_t) { return ESP_OK; }
esp_err_t gpio_set_level(gpio_num_t, std::uint32_t) { return ESP_OK; }
esp_err_t esp_camera_init(const camera_config_t*) { if (borrowed) { std::abort(); } initialized = true; return ESP_OK; }
esp_err_t esp_camera_deinit() { if (borrowed) { std::abort(); } initialized = false; return ESP_OK; }
camera_fb_t* esp_camera_fb_get() { if (!initialized || borrowed) { std::abort(); } borrowed = true; return &frame; }
void esp_camera_fb_return(camera_fb_t*) { if (!borrowed) { std::abort(); } borrowed = false; }
sensor_t* esp_camera_sensor_get() { return &sensor; }
std::int64_t esp_timer_get_time()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::uint32_t esp_random()
{
    static std::atomic<std::uint32_t> counter{0x12345678U};
    return counter.fetch_add(17U);
}
void vTaskDelay(std::uint32_t ticks) { std::this_thread::sleep_for(std::chrono::milliseconds{ticks}); }
int xTaskCreate(void (*entry)(void*), const char*, std::uint32_t, void* context, unsigned, void*)
{
    std::thread{[entry, context] { entry(context); }}.detach(); return pdPASS;
}
void vTaskDelete(void*) {}
QueueHandle_t xQueueCreateStatic(unsigned count, unsigned size, std::uint8_t*, StaticQueue_t* storage)
{
    storage->capacity = count; storage->itemSize = size; return storage;
}
int xQueueSend(QueueHandle_t queue, const void* item, std::uint32_t)
{
    std::unique_lock guard{queue->mutex};
    queue->changed.wait(guard, [queue] { return queue->items.size() < queue->capacity; });
    const auto* bytes = static_cast<const std::uint8_t*>(item);
    queue->items.emplace_back(bytes, bytes + queue->itemSize);
    queue->changed.notify_all(); return pdTRUE;
}
int xQueueReceive(QueueHandle_t queue, void* item, std::uint32_t ticks)
{
    std::unique_lock guard{queue->mutex};
    if ((ticks == 0U) && queue->items.empty()) { return pdFALSE; }
    queue->changed.wait(guard, [queue] { return !queue->items.empty(); });
    std::memcpy(item, queue->items.front().data(), queue->itemSize);
    queue->items.pop_front(); queue->changed.notify_all(); return pdTRUE;
}
void vQueueDelete(QueueHandle_t) {}
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t* storage) { return storage; }
int xSemaphoreTake(SemaphoreHandle_t semaphore, std::uint32_t)
{
    std::unique_lock guard{semaphore->mutex};
    semaphore->changed.wait(guard, [semaphore] { return semaphore->available; });
    semaphore->available = false; return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    const std::lock_guard guard{semaphore->mutex};
    semaphore->available = true; semaphore->changed.notify_one(); return pdTRUE;
}
void vSemaphoreDelete(SemaphoreHandle_t) {}
esp_err_t cam::Network::setStreamingPower(bool) noexcept { return ESP_OK; }

bool loadCameraFixture(const char* path)
{
    std::ifstream file{path, std::ios::binary};
    fixture.assign(std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{});
    frame = {fixture.data(), fixture.size(), 640U, 480U, PIXFORMAT_JPEG};
    return !fixture.empty();
}
