#include "application.hpp"
#include "esp_err.h"

// The only C-linkage application entry point is required by ESP-IDF. Static
// storage outlives app_main's task and every network/HTTP callback it registers.
extern "C" void app_main(void);
extern "C" void app_main(void)
{
    static cam::Application application{};
    // Failure to establish the application is fatal; request/camera failures
    // are recoverable error returns and never use ESP_ERROR_CHECK.
    ESP_ERROR_CHECK(application.run());
}
