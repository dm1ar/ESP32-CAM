#ifndef TEST_ESP_HEAP_CAPS_H
#define TEST_ESP_HEAP_CAPS_H
#include <cstddef>
#include <cstdint>
inline constexpr std::uint32_t MALLOC_CAP_SPIRAM{1U};
inline constexpr std::uint32_t MALLOC_CAP_8BIT{2U};
void* heap_caps_malloc(std::size_t size, std::uint32_t caps);
void heap_caps_free(void* pointer);
#endif
