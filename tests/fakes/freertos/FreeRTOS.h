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
#endif
