#pragma once

// Minimal host stand-in for the ESP-IDF esp_err.h header: only declarations our
// adapters use. Native tests only; firmware uses the real SDK header.

using esp_err_t = int;

constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_FAIL = -1;
constexpr esp_err_t ESP_ERR_TIMEOUT = 0x107;
constexpr esp_err_t ESP_ERR_NOT_SUPPORTED = 0x106;
