#ifndef TEST_FREERTOS_H
#define TEST_FREERTOS_H
#include <cstdint>
#include <mutex>
// A real mutex permits a host concurrency test of SharedState's lock coverage.
using portMUX_TYPE = std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(mutex) (mutex)->lock()
#define portEXIT_CRITICAL(mutex) (mutex)->unlock()
#define pdMS_TO_TICKS(milliseconds) (milliseconds)
inline constexpr int pdTRUE{1}, pdFALSE{0}, pdPASS{1};
inline constexpr std::uint32_t portMAX_DELAY{0xFFFFFFFFU};
#endif
