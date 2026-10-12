#include "firmware/Application.hpp"

#include <array>
#include <cstddef>

#include <esp_app_desc.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>
#include <nvs_flash.h>

#include "commissioning/Commissioning.hpp"
#include "config/Configuration.hpp"
#include "control/Command.hpp"
#include "control/Publish.hpp"
#include "control/WorkerWait.hpp"
#include "dali/BusTransport.hpp"
#include "dali/RetryingTransport.hpp"
#include "dali/devices/EventDecoder.hpp"
#include "espidf/NvsLevelStore.hpp"
#include "espidf/RmtDaliBus.hpp"
#include "firmware/Board.hpp"
#include "firmware/KnxDevice.hpp"
#include "firmware/Queue.hpp"
#include "firmware/UsbConsole.hpp"
#include "inputs/InputMapper.hpp"
#include "lighting/RelativeDimmer.hpp"

namespace gateway::firmware {
namespace {

constexpr const char* log_tag = "gateway";

const char* source_name(lighting::StateSource source) {
    switch (source) {
    case lighting::StateSource::Readback:
        return "readback";
    case lighting::StateSource::Commanded:
        return "commanded";
    case lighting::StateSource::RestoredCommand:
        return "restored-command";
    case lighting::StateSource::Unknown:
        return "unknown";
    }
    return "unknown";
}

const char* error_name(lighting::ControlError error) {
    switch (error) {
    case lighting::ControlError::None:
        return "none";
    case lighting::ControlError::TransportFailure:
        return "transport-failure";
    case lighting::ControlError::NoSavedTarget:
        return "no-saved-target";
    case lighting::ControlError::StorageFailure:
        return "storage-failure";
    case lighting::ControlError::Unsupported:
        return "unsupported";
    case lighting::ControlError::InvalidValue:
        return "invalid-value";
    }
    return "unknown";
}

const char* transport_name(dali::TransportStatus status) {
    switch (status) {
    case dali::TransportStatus::Ok:
        return "ok";
    case dali::TransportStatus::Unconfigured:
        return "unconfigured";
    case dali::TransportStatus::Timeout:
        return "timeout";
    case dali::TransportStatus::Collision:
        return "collision";
    case dali::TransportStatus::InvalidResponse:
        return "invalid-response";
    case dali::TransportStatus::Unsupported:
        return "unsupported";
    case dali::TransportStatus::Failure:
        return "failure";
    }
    return "unknown";
}

const char* address_name(dali::Address address) {
    switch (address.kind()) {
    case dali::AddressKind::Short:
        return "channel";
    case dali::AddressKind::Group:
        return "group";
    case dali::AddressKind::Broadcast:
        return "broadcast";
    }
    return "unknown";
}

void log_result(dali::Address address, const lighting::ControlResult& result) {
    if (result.state.level) {
        ESP_LOGI(log_tag, "address=%s%u source=%s error=%s transport=%s arc-level=%u", address_name(address),
                 address.as_short()   ? static_cast<unsigned>(address.as_short()->value())
                 : address.as_group() ? static_cast<unsigned>(address.as_group()->value())
                                      : 0,
                 source_name(result.state.source), error_name(result.error),
                 transport_name(result.transport_status), static_cast<unsigned>(result.state.level->value()));
    } else {
        ESP_LOGI(log_tag, "address=%s source=%s error=%s transport=%s arc-level=unknown",
                 address_name(address), source_name(result.state.source), error_name(result.error),
                 transport_name(result.transport_status));
    }
}

nvs_handle_t open_targets() {
    auto error = nvs_flash_init();
    if (error != ESP_OK) {
        ESP_LOGE(log_tag, "NVS unavailable (%s); configuration will not be erased automatically",
                 esp_err_to_name(error));
        return 0;
    }
    nvs_handle_t handle = 0;
    error = nvs_open("targets_v1", NVS_READWRITE, &handle);
    if (error != ESP_OK) {
        ESP_LOGE(log_tag, "Target store unavailable (%s)", esp_err_to_name(error));
        return 0;
    }
    return handle;
}

bool open_dali(espidf::RmtDaliBus& bus) {
    if (!bus.start()) {
        ESP_LOGE(log_tag, "DALI bus initialization failed");
        return false;
    }
    return true;
}

// Yields the worker while gear generate random addresses during
// commissioning; also feeds the task watchdog the worker is subscribed to.
void bus_delay_ms(std::uint32_t ms) {
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(ms));
}

struct Wiring {
    lighting::LightingService& lighting;
    dali::Transport& transport;
    commissioning::Commissioning& commissioning;
    inputs::InputMapper& inputs;
    Queue<control::Command>& commands;
    Queue<control::StatusEvent>& statuses;
    Queue<config::Configuration>& configuration;
    Queue<commissioning::Snapshot>& snapshots;
    Queue<commissioning::Snapshot>& console_snapshots;
    Queue<commissioning::InputSnapshot>& input_snapshots;
    Queue<commissioning::InputSnapshot>& console_input_snapshots;
};

bool input_instance_bound(std::uint8_t device, std::uint8_t instance, void* context) {
    return static_cast<inputs::InputMapper*>(context)->bound(device, instance);
}

constexpr TickType_t status_send_wait = pdMS_TO_TICKS(50);
constexpr TickType_t configuration_wait = pdMS_TO_TICKS(3000);
constexpr std::uint32_t fault_poll_interval_ms = 1000;
// The task watchdog (30 s timeout) is also fed while the worker idles.
constexpr std::uint32_t watchdog_feed_ms = 1000;

void publish_status(Queue<control::StatusEvent>& statuses, control::StatusEvent event) {
    if (!statuses.push(event, status_send_wait)) {
        ESP_LOGW(log_tag, "status queue full; dropped an update");
    }
}

void publish_job_result(Wiring& wiring, const commissioning::JobResult& result) {
    const auto push = [&wiring](const control::StatusEvent& event) {
        publish_status(wiring.statuses, event);
        return true;
    };
    control::publish_job_result(push, wiring.lighting, wiring.commissioning, result);
    const auto input = wiring.commissioning.input_snapshot();
    wiring.input_snapshots.overwrite(input);
    wiring.console_input_snapshots.overwrite(input);
}

void apply_configuration_values(Wiring& wiring, const config::Configuration& configuration) {
    wiring.lighting.configure(configuration);
    wiring.inputs.configure(configuration);
    unsigned reconciled = 0;
    unsigned failed = 0;
    for (unsigned channel = 0; channel < dali::ShortAddress::capacity; ++channel) {
        const auto result = wiring.lighting.reconcile_groups(*dali::ShortAddress::from(channel));
        if (result.transport_status == dali::TransportStatus::Ok) {
            ++reconciled;
        } else {
            ++failed;
        }
        const auto push = [&wiring](const control::StatusEvent& event) {
            publish_status(wiring.statuses, event);
            return true;
        };
        control::publish_changes(push, wiring.lighting, result);
    }
    ESP_LOGI(log_tag, "applied ETS configuration; reconciled %u channels, %u failed", reconciled, failed);
}

std::uint32_t now_ms() {
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
}

// Ticks until the next dimmer deadline or fault poll, capped so the task
// watchdog stays fed while idle.
TickType_t worker_wait(const lighting::RelativeDimmer& dimmer, const lighting::RelativeDimmer& colour_dimmer,
                       std::uint32_t next_poll_ms) {
    auto deadline = dimmer.next_deadline();
    if (const auto colour = colour_dimmer.next_deadline();
        colour && (!deadline || static_cast<std::int32_t>(*colour - *deadline) < 0)) {
        deadline = colour;
    }
    return pdMS_TO_TICKS(control::worker_wait_ms(now_ms(), deadline, next_poll_ms, watchdog_feed_ms));
}

void handle_step(Wiring& wiring, lighting::DimStep step) {
    const auto result = wiring.lighting.dim_step(step.address, step.direction, step.first);
    if (result.error != lighting::ControlError::None) {
        ESP_LOGW(log_tag, "dim step on %s failed: transport=%s", address_name(step.address),
                 transport_name(result.transport_status));
    }
}

void handle_expiry(Wiring& wiring, lighting::DimExpiry expiry) {
    ESP_LOGW(log_tag, "relative dimming on %s hit the safety limit; reading back",
             address_name(expiry.address));
    const auto result = wiring.lighting.finish_dimming(expiry.address);
    const auto push = [&wiring](const control::StatusEvent& event) {
        publish_status(wiring.statuses, event);
        return true;
    };
    control::publish_changes(push, wiring.lighting, result);
    control::publish_colour_changes(push, wiring.lighting, result);
}

void handle_colour_step(Wiring& wiring, lighting::DimStep step) {
    const auto result = wiring.lighting.colour_temperature_step(step.address, step.direction);
    if (result.error != lighting::ControlError::None) {
        ESP_LOGW(log_tag, "colour temperature step on %s failed: transport=%s", address_name(step.address),
                 transport_name(result.transport_status));
    }
}

void handle_colour_expiry(Wiring& wiring, lighting::DimExpiry expiry) {
    ESP_LOGW(log_tag, "relative colour dimming on %s hit the safety limit; reading back",
             address_name(expiry.address));
    const auto result = wiring.lighting.finish_colour_temperature(expiry.address);
    const auto push = [&wiring](const control::StatusEvent& event) {
        publish_status(wiring.statuses, event);
        return true;
    };
    control::publish_colour_changes(push, wiring.lighting, result);
}

// Emitted once per probed address during scans and several times per
// commissioning job: also the worker's watchdog heartbeat while a long
// job (e.g. dali_commission) blocks the loop.
class SnapshotForwarder final : public commissioning::SnapshotListener {
public:
    explicit SnapshotForwarder(Wiring& wiring) : wiring_(wiring) {}

    void on_snapshot(const commissioning::Snapshot& snapshot) override {
        esp_task_wdt_reset();
        wiring_.snapshots.overwrite(snapshot);
        wiring_.console_snapshots.overwrite(snapshot);
    }

private:
    Wiring& wiring_;
};

// Applies a queued ETS configuration, if one arrived since the last command.
void apply_configuration(Wiring& wiring) {
    if (const auto configuration = wiring.configuration.pop(0)) {
        apply_configuration_values(wiring, *configuration);
    }
}

void startup(Wiring& wiring) {
    // Give the KNX task a moment to hand over the ETS configuration; on an
    // unconfigured device the defaults stay in effect.
    if (const auto configuration = wiring.configuration.pop(configuration_wait)) {
        apply_configuration_values(wiring, *configuration);
    }
    ESP_LOGI(log_tag, "startup: scanning the DALI bus and recovering present gear");
    const auto result = wiring.commissioning.startup(&input_instance_bound, &wiring.inputs);
    wiring.inputs.refresh_resolutions(wiring.commissioning.devices());
    publish_job_result(wiring, result);
    ESP_LOGI(log_tag, "startup: %u present, %u faulty",
             static_cast<unsigned>(wiring.commissioning.present().count()),
             static_cast<unsigned>(wiring.commissioning.faulty().count()));
}

void dali_task(void* arg) {
    auto& wiring = *static_cast<Wiring*>(arg);
    esp_task_wdt_add(nullptr);
    SnapshotForwarder snapshot_forwarder{wiring};
    wiring.commissioning.set_listener(&snapshot_forwarder);
    lighting::RelativeDimmer dimmer;
    lighting::RelativeDimmer colour_dimmer;
    startup(wiring);
    auto next_poll = now_ms() + fault_poll_interval_ms;
    for (;;) {
        esp_task_wdt_reset();
        const auto command = wiring.commands.pop(worker_wait(dimmer, colour_dimmer, next_poll));
        const auto now = now_ms();
        if (command) {
            apply_configuration(wiring);
            if (std::holds_alternative<control::ScanBus>(*command) ||
                std::holds_alternative<control::AssignAddresses>(*command) ||
                std::holds_alternative<control::ReplaceDevice>(*command) ||
                std::holds_alternative<control::IdentifyDevice>(*command) ||
                std::holds_alternative<control::ResetAddresses>(*command) ||
                std::holds_alternative<control::ScanInputDevices>(*command) ||
                std::holds_alternative<control::AssignInputDevices>(*command)) {
                ESP_LOGI(log_tag, "commissioning job started");
            }
            const auto result = control::execute(wiring.lighting, dimmer, colour_dimmer, wiring.commissioning,
                                                 wiring.inputs, now, *command);
            log_result(control::address_of(*command), result.result);
            publish_job_result(wiring, result);
        } else if (static_cast<std::int32_t>(now - next_poll) >= 0) {
            next_poll = now + fault_poll_interval_ms;
            if (const auto changed = wiring.commissioning.poll_next()) {
                ESP_LOGW(log_tag, "channel %u fault state changed to %u", changed->value(),
                         static_cast<unsigned>(wiring.commissioning.faulty().test(changed->value())));
                publish_status(
                    wiring.statuses,
                    control::FaultUpdate{*changed, wiring.commissioning.faulty().test(changed->value())});
                snapshot_forwarder.on_snapshot(wiring.commissioning.snapshot());
            }
        }
        dimmer.poll(
            now, [&wiring](lighting::DimStep step) { handle_step(wiring, step); },
            [&wiring](lighting::DimExpiry expiry) { handle_expiry(wiring, expiry); });
        while (const auto event = wiring.transport.poll_event()) {
            if (event->bits != 24) {
                ESP_LOGD(log_tag, "dali bus frame from another master: 0x%06X (%u bits)",
                         static_cast<unsigned>(event->value), static_cast<unsigned>(event->bits));
                continue;
            }
            const auto decoded = dali::device::decode_event(*event);
            if (!decoded) {
                ESP_LOGD(log_tag, "dali event dropped: 0x%06X", static_cast<unsigned>(event->value));
                continue;
            }
            std::array<inputs::Action, inputs::InputMapper::max_actions> actions;
            const auto count = wiring.inputs.map(
                *decoded, wiring.commissioning.devices().instance_type(decoded->address, decoded->instance),
                actions);
            for (std::uint8_t index = 0; index < count; ++index) {
                const auto& action = actions[index];
                publish_status(wiring.statuses,
                               control::InputAction{action.object, action.data, action.length});
            }
        }
        colour_dimmer.poll(
            now, [&wiring](lighting::DimStep step) { handle_colour_step(wiring, step); },
            [&wiring](lighting::DimExpiry expiry) { handle_colour_expiry(wiring, expiry); });
    }
}

void knx_task(void* arg) {
    auto& wiring = *static_cast<Wiring*>(arg);
    KnxDevice device{wiring.commands, wiring.statuses, wiring.configuration, wiring.snapshots,
                     wiring.input_snapshots};
    device.run();
}

}

void factory_reset() {
    for (const char* name : {"eeprom", "targets_v1"}) {
        nvs_handle_t handle = 0;
        if (nvs_open(name, NVS_READWRITE, &handle) == ESP_OK) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_erase_all(handle));
            ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_commit(handle));
            nvs_close(handle);
            ESP_LOGW(log_tag, "factory reset: erased NVS namespace %s", name);
        } else {
            ESP_LOGW(log_tag, "factory reset: NVS namespace %s missing, skipped", name);
        }
    }
    ESP_LOGW(log_tag, "factory reset complete; restarting");
    esp_restart();
}

void run() {
    const auto* description = esp_app_get_description();
    ESP_LOGI(log_tag, "%s %s: development slice, not a commissioned gateway", description->project_name,
             description->version);
    espidf::NvsLevelStore targets{open_targets()};
    espidf::RmtDaliBus bus{{.tx_gpio = board::dali_tx,
                            .rx_gpio = board::dali_rx,
                            .invert_tx = board::dali_invert_tx,
                            .invert_rx = board::dali_invert_rx}};
    open_dali(bus);
    dali::BusTransport bus_transport{bus, &bus_delay_ms};
    dali::RetryingTransport retrying{bus_transport};
    lighting::LightingService lighting{retrying, targets};
    lighting.configure(config::default_configuration());
    commissioning::Commissioning commissioning{retrying, lighting};
    inputs::InputMapper inputs;
    inputs.configure(config::default_configuration());
    Queue<control::Command> commands{64};
    Queue<control::StatusEvent> statuses{96};
    Queue<config::Configuration> configuration{1};
    Queue<commissioning::Snapshot> snapshots{1};
    Queue<commissioning::Snapshot> console_snapshots{1};
    Queue<commissioning::InputSnapshot> input_snapshots{1};
    Queue<commissioning::InputSnapshot> console_input_snapshots{1};
    Wiring wiring{lighting,          retrying,        commissioning,          inputs,
                  commands,          statuses,        configuration,          snapshots,
                  console_snapshots, input_snapshots, console_input_snapshots};
    xTaskCreate(dali_task, "dali", 4096, &wiring, 5, nullptr);
    xTaskCreate(knx_task, "knx", 12288, &wiring, 6, nullptr);
    UsbConsole console{commands, console_snapshots, console_input_snapshots, bus.configured(),
                       targets.available()};
    const auto error = console.start();
    if (error != ESP_OK) {
        ESP_LOGE(log_tag, "USB console could not start (%s)", esp_err_to_name(error));
    }
    // Never returns: the tasks reference the objects living in this frame.
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

}
