#pragma once
#include "../diagnostics_stubs/esp_system.h"
#include <cstddef>
#include <cstdint>
inline esp_reset_reason_t mockReset = ESP_RST_POWERON;
inline esp_reset_reason_t esp_reset_reason() { return mockReset; }
inline constexpr int ESP_OK = 0;
inline int esp_register_shutdown_handler(void (*)()) { return ESP_OK; }
inline unsigned randomGeneration = 0;
inline void esp_fill_random(void* out, size_t size) { auto bytes = static_cast<uint8_t*>(out); ++randomGeneration; for (size_t i = 0; i < size; ++i) bytes[i] = randomGeneration + i; }
