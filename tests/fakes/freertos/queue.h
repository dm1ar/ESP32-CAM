#ifndef TEST_QUEUE_H
#define TEST_QUEUE_H
#include <condition_variable>
#include <deque>
#include <mutex>
#include <vector>
#include "freertos/FreeRTOS.h"
struct StaticQueue_t {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::vector<std::uint8_t>> items;
    std::size_t capacity{}, itemSize{};
};
using QueueHandle_t = StaticQueue_t*;
QueueHandle_t xQueueCreateStatic(unsigned count, unsigned size, std::uint8_t* bytes, StaticQueue_t* storage);
int xQueueSend(QueueHandle_t queue, const void* item, std::uint32_t ticks);
int xQueueReceive(QueueHandle_t queue, void* item, std::uint32_t ticks);
void vQueueDelete(QueueHandle_t queue);
#endif
