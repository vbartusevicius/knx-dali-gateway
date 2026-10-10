#include "firmware/KnxDevice.hpp"

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <led_strip.h>

#include "firmware/Application.hpp"

namespace gateway::firmware {
namespace {

constexpr const char* log_tag = "knx";
constexpr std::uint32_t led_blink_interval_ms = 100;

std::uint32_t now_ms() {
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
}

// The LED driver handle lives at file scope; the strip is one pixel.
led_strip_handle_t prog_led_strip = nullptr;

void prog_led_on() {
    if (prog_led_strip) {
        led_strip_set_pixel(prog_led_strip, 0, 255, 255, 255);
        led_strip_refresh(prog_led_strip);
    }
}

void prog_led_off() {
    if (prog_led_strip) {
        led_strip_clear(prog_led_strip);
        led_strip_refresh(prog_led_strip);
    }
}

}

KnxDevice::KnxDevice(Queue<control::Command>& commands, Queue<control::StatusEvent>& statuses,
                     Queue<config::Configuration>& configuration,
                     Queue<commissioning::Snapshot>& snapshots,
                     Queue<commissioning::InputSnapshot>& input_snapshots) noexcept
    : commands_(commands), statuses_(statuses), configuration_(configuration),
      snapshots_(snapshots), input_snapshots_(input_snapshots),
      hooks_{
          [](void* ctx, const control::Command& command) {
              return static_cast<KnxDevice*>(ctx)->commands_.try_push(command);
          },
          [](void* ctx) { return static_cast<KnxDevice*>(ctx)->statuses_.pop(0); },
          [](void* ctx, const config::Configuration& config) {
              return static_cast<KnxDevice*>(ctx)->configuration_.overwrite(config);
          },
          [](void* ctx) { return static_cast<KnxDevice*>(ctx)->drain_snapshots(); },
          [](void* ctx) { return static_cast<KnxDevice*>(ctx)->drain_input_snapshots(); },
          [](void*, const char* message) { ESP_LOGW(log_tag, "%s", message); },
          this,
      },
      app_(knx_, hooks_) {
    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = board::prog_led;
    strip_config.max_leds = 1;
    strip_config.led_model = LED_MODEL_WS2812;
    strip_config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    led_strip_rmt_config_t rmt_config = {};
    rmt_config.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt_config.resolution_hz = 10 * 1000 * 1000;
    rmt_config.mem_block_symbols = 48;
    if (led_strip_new_rmt_device(&strip_config, &rmt_config, &prog_led_strip) != ESP_OK) {
        ESP_LOGW(log_tag, "prog LED unavailable; programming mode will not light the LED");
    }
}

void KnxDevice::run() {
    platform_.knxUartPins(static_cast<std::int8_t>(board::knx_rx), static_cast<std::int8_t>(board::knx_tx));
    platform_.knxUartBaudRate(board::knx_baud);
    app_.start();

    const gpio_config_t button = {
        .pin_bit_mask = 1ULL << board::prog_button,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&button);
    esp_task_wdt_add(nullptr);

    for (;;) {
        esp_task_wdt_reset();
        const auto now = now_ms();
        app_.loop();
        handle_button(now);
        update_led(now);
        vTaskDelay(1);
    }
}

commissioning::Snapshot KnxDevice::drain_snapshots() {
    while (const auto snapshot = snapshots_.pop(0)) {
        latest_snapshot_ = *snapshot;
    }
    return latest_snapshot_;
}

commissioning::InputSnapshot KnxDevice::drain_input_snapshots() {
    while (const auto snapshot = input_snapshots_.pop(0)) {
        latest_input_snapshot_ = *snapshot;
    }
    return latest_input_snapshot_;
}

void KnxDevice::handle_button(std::uint32_t now) {
    // BOOT button is active low with the pull-up enabled.
    const auto gesture = prog_button_.sample(gpio_get_level(board::prog_button) == 0, now);
    switch (gesture) {
    case device::Gesture::ToggleProgMode:
        knx_.toggleProgMode();
        break;
    case device::Gesture::FactoryReset:
        ESP_LOGW(log_tag, "prog button held: factory reset");
        factory_reset();
        break;
    case device::Gesture::None:
        break;
    }
}

void KnxDevice::update_led(std::uint32_t now) {
    const auto pattern = prog_button_.led_pattern(knx_.progMode());
    bool on;
    if (pattern == device::LedPattern::FastBlink) {
        if (static_cast<std::uint32_t>(now - last_blink_ms_) >= led_blink_interval_ms) {
            last_blink_ms_ = now;
            led_on_ = !led_on_;
        }
        on = led_on_;
    } else {
        on = pattern == device::LedPattern::On;
        led_on_ = on;
    }
    if (on != led_written_) {
        led_written_ = on;
        on ? prog_led_on() : prog_led_off();
    }
}

}
