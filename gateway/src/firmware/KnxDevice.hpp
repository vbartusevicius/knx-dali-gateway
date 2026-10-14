#pragma once

#include <esp32_idf_platform.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_oneshot.h>
#include <knx/bau07B0.h>
#include <knx_facade.h>

#include "commissioning/Commissioning.hpp"
#include "config/Configuration.hpp"
#include "control/Command.hpp"
#include "device/FrontPanel.hpp"
#include "device/StatusLeds.hpp"
#include "firmware/Board.hpp"
#include "firmware/Queue.hpp"
#include "knxstack/KnxApplication.hpp"

namespace gateway::firmware {

// ESP-IDF half of the KNX device: UART pins to the NCN5130, 3-LED SK6805-EC15
// status chain (D1 STATUS, D2 KNX, D3 DALI), ADC button ladder sampling
// (SW1 PRG/RST, SW2 FUNC) and the task loop. Everything that talks to the
// thelsing stack lives in the portable knxstack::KnxApplication.
class KnxDevice final {
public:
    KnxDevice(Queue<control::Command>& commands, Queue<control::StatusEvent>& statuses,
              Queue<config::Configuration>& configuration, Queue<commissioning::Snapshot>& snapshots,
              Queue<commissioning::InputSnapshot>& input_snapshots) noexcept;

    void run();

private:
    void init_button_adc();
    device::Buttons read_buttons();
    void handle_button(std::uint32_t now_ms);
    void update_led(std::uint32_t now_ms);
    commissioning::Snapshot drain_snapshots();
    commissioning::InputSnapshot drain_input_snapshots();

    using Facade = KnxFacade<Esp32IdfPlatform, Bau07B0>;

    Esp32IdfPlatform platform_{board::knx_uart};
    Bau07B0 bau_{platform_};
    Facade knx_{bau_};
    Queue<control::Command>& commands_;
    Queue<control::StatusEvent>& statuses_;
    Queue<config::Configuration>& configuration_;
    Queue<commissioning::Snapshot>& snapshots_;
    Queue<commissioning::InputSnapshot>& input_snapshots_;
    commissioning::Snapshot latest_snapshot_{};
    commissioning::InputSnapshot latest_input_snapshot_{};
    knxstack::KnxApplication<Facade>::Hooks hooks_{};
    knxstack::KnxApplication<Facade> app_;
    adc_oneshot_unit_handle_t adc_ = nullptr;
    adc_cali_handle_t adc_cali_ = nullptr;
    device::FrontPanel panel_;
    device::LedColours shown_{};
    bool led_written_ = false;
};

}
