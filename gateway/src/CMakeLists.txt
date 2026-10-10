file(GLOB_RECURSE gateway_sources "*.cpp")

idf_component_register(
    SRCS ${gateway_sources}
    INCLUDE_DIRS "."
    REQUIRES console nvs_flash freertos esp_app_format esp_driver_gpio esp_driver_uart esp_driver_rmt esp_netif lwip esp_timer esp_hw_support espressif__dali espressif__led_strip
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
