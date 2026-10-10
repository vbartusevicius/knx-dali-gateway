#pragma once

// Minimal host stand-in for the ESP-IDF nvs.h header: only declarations our
// adapters use. Native tests only; firmware uses the real SDK header.

#include <cstdint>
#include <esp_err.h>

using nvs_handle_t = std::uint32_t;

constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND = 0x1102;

extern "C" {
esp_err_t nvs_get_u8(nvs_handle_t handle, const char* key, std::uint8_t* value);
esp_err_t nvs_set_u8(nvs_handle_t handle, const char* key, std::uint8_t value);
esp_err_t nvs_commit(nvs_handle_t handle);
void nvs_close(nvs_handle_t handle);
}
