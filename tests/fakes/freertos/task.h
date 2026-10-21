#ifndef TEST_TASK_H
#define TEST_TASK_H
#include <cstdint>
void vTaskDelay(std::uint32_t ticks);
int xTaskCreate(void (*entry)(void*), const char* name, std::uint32_t stack,
                void* context, unsigned priority, void* handle);
void vTaskDelete(void* task);
#endif
