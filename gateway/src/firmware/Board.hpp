#pragma once

#include <cstddef>
#include <cstdint>

#include <driver/gpio.h>
#include <hal/adc_types.h>
#include <hal/uart_types.h>

// Unified XIAO ESP32-C6 pin map across V1 (3 DIN), V2 (2 DIN) and V3 (1 DIN):
//   D0  / GPIO0  = LED_DATA         (SK6805-EC15 RGB chain: index 0 D1 STATUS, 1 D2 KNX, 2 D3 DALI)
//   D1  / GPIO1  = DALI_SUPPLY_OFF  (high switches the isolated REM3-2415S/A/CTRL DALI supply off)
//   D2  / GPIO2  = BTN_LADDER       (ADC1_CH2: 10k pull-up to 3V3, SW1 PRG/RST 20k, SW2 FUNC 10k)
//   D3  / GPIO21 = NC               (unconnected spare GPIO)
//   D4  / GPIO22 = I2C_SDA          (SSD1306 OLED on UI board, I2C address 0x3C)
//   D5  / GPIO23 = I2C_SCL          (SSD1306 OLED on UI board)
//   D6  / GPIO16 = KNX_RXD          (MCU UART1 TX -> 1k series R -> NCN5130 SDI/RXD pin 27)
//   D7  / GPIO17 = KNX_TXD          (NCN5130 SDO/TXD pin 28 -> MCU UART1 RX)
//   D8  / GPIO19 = DALI_RX          (RX optocoupler collector; bus high = logic low)
//   D9  / GPIO20 = NCN_SAVEB        (NCN5130 SAVEB open-drain power-warning input, active low)
//   D10 / GPIO18 = DALI_TX          (TX optocoupler LED; high = bus low)
namespace gateway::firmware::board {

constexpr gpio_num_t led_data = GPIO_NUM_0;         // XIAO D0
constexpr std::size_t led_count = 3;                // D1 STATUS (0), D2 KNX (1), D3 DALI (2)
constexpr gpio_num_t dali_supply_off = GPIO_NUM_1;  // XIAO D1 (active high = supply off)
constexpr gpio_num_t button_ladder = GPIO_NUM_2;    // XIAO D2 (ADC1_CH2)
constexpr adc_unit_t button_adc_unit = ADC_UNIT_1;
constexpr adc_channel_t button_adc_channel = ADC_CHANNEL_2;
constexpr gpio_num_t i2c_sda = GPIO_NUM_22;  // XIAO D4
constexpr gpio_num_t i2c_scl = GPIO_NUM_23;  // XIAO D5
constexpr uart_port_t knx_uart = UART_NUM_1;
constexpr gpio_num_t knx_tx = GPIO_NUM_16;  // XIAO D6 -> NCN5130 SDI/RXD
constexpr gpio_num_t knx_rx = GPIO_NUM_17;  // XIAO D7 <- NCN5130 SDO/TXD
constexpr std::uint32_t knx_baud = 19200;
constexpr gpio_num_t dali_rx = GPIO_NUM_19;    // XIAO D8
constexpr gpio_num_t ncn_saveb = GPIO_NUM_20;  // XIAO D9
constexpr gpio_num_t dali_tx = GPIO_NUM_18;    // XIAO D10
// On the gateway PCB, DALI_TX high turns the TX optocoupler on (pulling the bus
// low), and bus idle high turns the RX optocoupler on (pulling DALI_RX low).
constexpr bool dali_invert_tx = false;
constexpr bool dali_invert_rx = true;

constexpr bool usb_pin(gpio_num_t pin) {
    return pin == GPIO_NUM_12 || pin == GPIO_NUM_13;
}

constexpr bool pins_distinct() {
    const gpio_num_t pins[] = {
        led_data, dali_supply_off, button_ladder, i2c_sda,   i2c_scl,
        knx_tx,   knx_rx,          dali_rx,       ncn_saveb, dali_tx,
    };
    for (std::size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        if (usb_pin(pins[i])) {
            return false;
        }
        for (std::size_t j = i + 1; j < sizeof(pins) / sizeof(pins[0]); ++j) {
            if (pins[i] == pins[j]) {
                return false;
            }
        }
    }
    return true;
}

static_assert(pins_distinct(), "board pins must be distinct and must not use USB D-/D+ (GPIO12/13)");

}
