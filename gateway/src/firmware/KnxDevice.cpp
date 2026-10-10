#include "firmware/KnxDevice.hpp"

#include <driver/gpio.h>
#include <esp_log.h>
#include <led_strip.h>

#include "config/Configuration.hpp"
#include "knx/LightingObjects.hpp"
#include "knx/ObjectMap.hpp"
#include "knx/generated/KnxDaliGateway.h"

namespace gateway::firmware {
namespace {

constexpr const char* log_tag = "knx";
constexpr TickType_t debounce_ticks = pdMS_TO_TICKS(200);

// The facade's prog-LED callbacks are plain C function pointers, so the strip
// handle lives at file scope.
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

KnxDevice::KnxDevice(Queue<control::Command>& commands, Queue<control::StatusUpdate>& statuses,
                     Queue<config::Configuration>& configuration) noexcept
    : commands_(commands), statuses_(statuses), configuration_(configuration) {
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
    knx_.setProgLedOnCallback(&prog_led_on);
    knx_.setProgLedOffCallback(&prog_led_off);
    knx_.readMemory();
    knx_.start();

    const gpio_config_t button = {
        .pin_bit_mask = 1ULL << board::prog_button,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&button);
    const auto service_error = gpio_install_isr_service(0);
    if (service_error == ESP_OK || service_error == ESP_ERR_INVALID_STATE) {
        gpio_isr_handler_add(board::prog_button, &prog_button_isr, this);
    } else {
        ESP_LOGW(log_tag, "GPIO ISR service unavailable (%s)", esp_err_to_name(service_error));
    }


    for (;;) {
        knx_.loop();
        handle_button();
        if (knx_.configured()) {
            attach_objects();
            publish_statuses();
        }
        vTaskDelay(1);
    }
}

void KnxDevice::prog_button_isr(void* arg) {
    static_cast<KnxDevice*>(arg)->note_button_edge();
}

void KnxDevice::note_button_edge() {
    last_button_edge_ = xTaskGetTickCountFromISR();
    button_pending_ = true;
}

void KnxDevice::handle_button() {
    if (button_pending_ && xTaskGetTickCount() - last_button_edge_ >= debounce_ticks) {
        button_pending_ = false;
        knx_.toggleProgMode();
    }
}

void KnxDevice::attach_objects() {
    if (objects_attached_) {
        return;
    }
    objects_attached_ = true;
    const auto count = knx_.bau().groupObjectTable().entryCount();
    if (count < knx::object_count) {
        ESP_LOGW(log_tag, "group object table has %u entries; objects above it are ignored",
                 static_cast<unsigned>(count));
    }
    const auto last = count < knx::object_count ? count : knx::object_count;
    for (std::uint16_t number = 1; number <= last; ++number) {
        knx_.getGroupObject(number).callback([this](GroupObject& object) { on_group_object(object); });
    }
    const auto* parameters = knx_.paramData(0);
    if (!configuration_.overwrite(config::parse_configuration(parameters, MAIN_ParameterSize))) {
        ESP_LOGW(log_tag, "configuration queue unavailable; worker keeps the defaults");
    }
}

void KnxDevice::on_group_object(GroupObject& object) {
    const std::uint16_t number = object.asap();
    const std::uint8_t raw = object.valueRef()[0];
    const auto target = knx::object_target(number);
    if (!target) {
        return;
    }
    std::optional<control::Command> command;
    switch (target->function) {
    case knx::ObjectFunction::Switch:
        command = knx::command_for_switch(number, (raw & 1) != 0);
        break;
    case knx::ObjectFunction::Brightness:
        command = knx::command_for_scaling(number, raw);
        break;
    case knx::ObjectFunction::RelativeDimming:
        command = knx::command_for_relative_dimming(number, raw);
        break;
    case knx::ObjectFunction::Scene:
        command = knx::command_for_scene(number, raw);
        break;
    default:
        break;
    }
    if (command && !commands_.try_push(*command)) {
        ESP_LOGW(log_tag, "command queue full; dropped write on object %u", number);
    }
}

void KnxDevice::publish_statuses() {
    const auto object_count = knx_.bau().groupObjectTable().entryCount();
    while (const auto update = statuses_.pop(0)) {
        const auto status = knx::status_objects(*update);
        if (!status) {
            continue;
        }
        if (status->switch_object <= object_count) {
            auto& switch_object = knx_.getGroupObject(status->switch_object);
            switch_object.valueRef()[0] = status->on ? 1 : 0;
            switch_object.objectWritten();
        }
        if (status->brightness_object <= object_count) {
            auto& brightness_object = knx_.getGroupObject(status->brightness_object);
            brightness_object.valueRef()[0] = status->scaling;
            brightness_object.objectWritten();
        }
    }
}

}
