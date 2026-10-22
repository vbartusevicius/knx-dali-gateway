#include "firmware/KnxDevice.hpp"

#include <driver/gpio.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <led_strip.h>

#include "device/ButtonLadder.hpp"
#include "device/StatusLeds.hpp"
#include "firmware/Application.hpp"
#include "firmware/NetworkServices.hpp"
#include "firmware/RuntimeState.hpp"

namespace gateway::firmware {
namespace {

constexpr const char* log_tag = "knx";

std::uint32_t now_ms() {
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
}

// 3-pixel SK6805-EC15 chain on D0/GPIO0: pixel 0 = D1 (STATUS), pixel 1 = D2
// (KNX), pixel 2 = D3 (DALI).
led_strip_handle_t status_strip = nullptr;

void show(const device::LedColours& colours) {
    if (!status_strip) {
        return;
    }
    for (std::size_t i = 0; i < colours.size(); ++i) {
        led_strip_set_pixel(status_strip, i, colours[i].r, colours[i].g, colours[i].b);
    }
    led_strip_refresh(status_strip);
}

}

KnxDevice::KnxDevice(Queue<control::Command>& commands, Queue<control::StatusEvent>& statuses,
                     Queue<config::Configuration>& configuration, Queue<commissioning::Snapshot>& snapshots,
                     Queue<commissioning::InputSnapshot>& input_snapshots, NetworkServices* net) noexcept
    : commands_(commands),
      statuses_(statuses),
      configuration_(configuration),
      snapshots_(snapshots),
      input_snapshots_(input_snapshots),
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
      app_(knx_, hooks_),
      net_(net) {
    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = board::led_data;
    strip_config.max_leds = board::led_count;
    strip_config.led_model = LED_MODEL_WS2812;
    strip_config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    led_strip_rmt_config_t rmt_config = {};
    rmt_config.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt_config.resolution_hz = 10 * 1000 * 1000;
    rmt_config.mem_block_symbols = 48;
    if (led_strip_new_rmt_device(&strip_config, &rmt_config, &status_strip) != ESP_OK) {
        ESP_LOGW(log_tag, "status LED strip unavailable");
    }
}

void KnxDevice::init_button_adc() {
    const adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = board::button_adc_unit,
        .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    if (adc_oneshot_new_unit(&unit_cfg, &adc_) != ESP_OK) {
        ESP_LOGW(log_tag, "button ladder ADC unavailable");
        return;
    }
    const adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_oneshot_config_channel(adc_, board::button_adc_channel, &chan_cfg);
    const adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = board::button_adc_unit,
        .chan = board::button_adc_channel,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_cali_create_scheme_curve_fitting(&cali_cfg, &adc_cali_);
}

device::Buttons KnxDevice::read_buttons() {
    if (!adc_) {
        return {};
    }
    int raw = 0;
    if (adc_oneshot_read(adc_, board::button_adc_channel, &raw) != ESP_OK) {
        return {};
    }
    int mv = (raw * 3300) / 4095;
    if (adc_cali_) {
        adc_cali_raw_to_voltage(adc_cali_, raw, &mv);
    }
    return device::classify_ladder(static_cast<std::uint16_t>(mv < 0 ? 0 : mv));
}

void KnxDevice::run() {
    const gpio_config_t supply_ctrl = {
        .pin_bit_mask = 1ULL << board::dali_supply_off,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&supply_ctrl);
    gpio_set_level(board::dali_supply_off, 0);

    const gpio_config_t saveb_in = {
        .pin_bit_mask = 1ULL << board::ncn_saveb,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&saveb_in);

    init_button_adc();
    platform_.knxUartPins(static_cast<std::int8_t>(board::knx_rx), static_cast<std::int8_t>(board::knx_tx));
    platform_.knxUartBaudRate(board::knx_baud);
    app_.start();
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
    switch (panel_.sample(read_buttons(), now)) {
    case device::Gesture::Reboot:
        ESP_LOGW(log_tag, "PRG/RST tapped: reboot");
        esp_restart();
    case device::Gesture::ToggleProgMode:
        knx_.toggleProgMode();
        break;
    case device::Gesture::FactoryReset:
        ESP_LOGW(log_tag, "both buttons held: factory reset");
        factory_reset();
    case device::Gesture::NextPage:
        runtime_state().next_oled_page(now);
        break;
    case device::Gesture::WifiSetup:
        ESP_LOGI(log_tag, "FUNC held 5s: starting WiFi SoftAP setup portal");
        if (net_) {
            net_->start_wifi_setup_portal();
        }
        break;
    case device::Gesture::None:
        break;
    }
}

void KnxDevice::update_led(std::uint32_t now) {
    device::DeviceStatus status;
    status.knx_bus_ok = gpio_get_level(board::ncn_saveb) != 0;
    if (!status.knx_bus_ok && net_) {
        net_->emergency_power_down();
    } else {
        if (runtime_state().consume_psu_reset_request()) {
            psu_monitor_.reset_trip();
        }
        psu_monitor_.set_internal_psu_enabled(runtime_state().network_config().internal_dali_psu_enabled);
        const bool rx_pin_high = gpio_get_level(board::dali_rx) != 0;
        const bool bus_idle_high = board::dali_invert_rx ? !rx_pin_high : rx_pin_high;
        const auto psu_res = psu_monitor_.sample(bus_idle_high, now);
        gpio_set_level(board::dali_supply_off, psu_res.supply_off_gpio ? 1 : 0);
        status.dali = psu_res.dali_led_state;
        if (psu_res.just_tripped_short) {
            runtime_state().log().record(diagnostics::LogSource::Dali, diagnostics::LogSeverity::Critical,
                                         diagnostics::ErrorCode::DaliBusShort, diagnostics::no_address,
                                         "DALI bus short >=80ms; internal supply latched OFF", now);
        } else if (psu_res.just_lost_external_power) {
            runtime_state().log().record(diagnostics::LogSource::Dali, diagnostics::LogSeverity::Error,
                                         diagnostics::ErrorCode::DaliNoPower, diagnostics::no_address,
                                         "External DALI bus power absent", now);
        }
    }
    status.knx_prog_mode = knx_.progMode();
    if (status.dali == device::DaliState::Ok) {
        for (const auto byte : latest_snapshot_.faulty) {
            if (byte != 0) {
                status.dali = device::DaliState::GearFault;
                break;
            }
        }
    }
    status.wifi = runtime_state().oled_state().wifi_state;
    status.hint = panel_.hint();
    runtime_state().update_knx_state(knx_.individualAddress(), knx_.configured(), status.knx_prog_mode,
                                     status.knx_bus_ok, status.hint, now);
    const auto colours = device::led_colours(status, now);
    if (!led_written_ || !(colours == shown_)) {
        led_written_ = true;
        shown_ = colours;
        show(colours);
    }
}

}
