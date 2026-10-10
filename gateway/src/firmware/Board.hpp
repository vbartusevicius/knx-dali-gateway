#pragma once

#include <cstdint>

#include <driver/gpio.h>
#include <hal/uart_types.h>

// Prototype board wiring: ESP32-C6-DevKitC-1 + MikroE DALI 2 Click + NCN5121 TP-UART module.
namespace gateway::firmware::board {

constexpr gpio_num_t dali_tx = GPIO_NUM_18;
constexpr gpio_num_t dali_rx = GPIO_NUM_19;
// The DALI 2 Click optocouplers invert both directions; the click's jumper must select 3.3 V.
constexpr bool dali_invert_tx = true;
constexpr bool dali_invert_rx = true;
constexpr uart_port_t knx_uart = UART_NUM_1;
constexpr gpio_num_t knx_tx = GPIO_NUM_22;  // to NCN5121 RXD
constexpr gpio_num_t knx_rx = GPIO_NUM_23;  // from NCN5121 TXD
constexpr std::uint32_t knx_baud = 19200;
constexpr gpio_num_t prog_button = GPIO_NUM_9;  // DevKit BOOT button, active low
constexpr gpio_num_t prog_led = GPIO_NUM_8;     // DevKit onboard WS2812 RGB LED

constexpr bool reserved_pin(gpio_num_t pin) {
    return pin == GPIO_NUM_12 || pin == GPIO_NUM_13 || pin == GPIO_NUM_16 || pin == GPIO_NUM_17;
}

constexpr bool pins_distinct() {
    const gpio_num_t pins[] = {dali_tx, dali_rx, knx_tx, knx_rx, prog_button, prog_led};
    for (std::size_t i = 0; i < 6; ++i) {
        if (reserved_pin(pins[i])) {
            return false;
        }
        for (std::size_t j = i + 1; j < 6; ++j) {
            if (pins[i] == pins[j]) {
                return false;
            }
        }
    }
    return true;
}

static_assert(pins_distinct(),
              "board pins must be distinct; GPIO12/13 are the USB pins, GPIO16/17 the UART0 bridge");

}
