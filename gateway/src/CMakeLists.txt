file(GLOB_RECURSE gateway_sources "*.cpp")
# src/lab is the Linux integration-lab build (sockets, std::thread); it has no
# place in the ESP-IDF component.
list(FILTER gateway_sources EXCLUDE REGEX "/lab/")

idf_component_register(
    SRCS ${gateway_sources}
    INCLUDE_DIRS "."
    REQUIRES console nvs_flash freertos esp_app_format esp_driver_gpio esp_driver_i2c esp_driver_uart esp_driver_rmt esp_adc esp_netif esp_wifi esp_event esp_http_server app_update mqtt lwip esp_timer esp_hw_support espressif__led_strip
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
