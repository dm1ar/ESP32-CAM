#ifndef TEST_GPIO_H
#define TEST_GPIO_H
#include <cstdint>
#include "esp_err.h"
enum gpio_num_t { GPIO_NUM_4 = 4, GPIO_NUM_32 = 32 };
enum gpio_mode_t { GPIO_MODE_OUTPUT };
esp_err_t gpio_set_direction(gpio_num_t pin, gpio_mode_t mode);
esp_err_t gpio_set_level(gpio_num_t pin, std::uint32_t level);
#endif
