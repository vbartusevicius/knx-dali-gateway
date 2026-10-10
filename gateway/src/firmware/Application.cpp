#include "firmware/Application.hpp"

#include <cstddef>

#include <esp_app_desc.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#include "config/Configuration.hpp"
#include "control/Command.hpp"
#include "espidf/DaliTransport.hpp"
#include "espidf/NvsLevelStore.hpp"
#include "firmware/Board.hpp"
#include "firmware/KnxDevice.hpp"
#include "firmware/Queue.hpp"
#include "firmware/UsbConsole.hpp"

namespace gateway::firmware {
namespace {

constexpr const char* log_tag = "gateway";

const char* source_name(lighting::StateSource source) {
    switch (source) {
    case lighting::StateSource::Readback: return "readback";
    case lighting::StateSource::Commanded: return "commanded";
    case lighting::StateSource::RestoredCommand: return "restored-command";
    case lighting::StateSource::Unknown: return "unknown";
    }
    return "unknown";
}

const char* error_name(lighting::ControlError error) {
    switch (error) {
    case lighting::ControlError::None: return "none";
    case lighting::ControlError::TransportFailure: return "transport-failure";
    case lighting::ControlError::NoSavedTarget: return "no-saved-target";
    case lighting::ControlError::StorageFailure: return "storage-failure";
    }
    return "unknown";
}

const char* transport_name(dali::TransportStatus status) {
    switch (status) {
    case dali::TransportStatus::Ok: return "ok";
    case dali::TransportStatus::Unconfigured: return "unconfigured";
    case dali::TransportStatus::Timeout: return "timeout";
    case dali::TransportStatus::Collision: return "collision";
    case dali::TransportStatus::InvalidResponse: return "invalid-response";
    case dali::TransportStatus::Unsupported: return "unsupported";
    case dali::TransportStatus::Failure: return "failure";
    }
    return "unknown";
}

const char* address_name(dali::Address address) {
    switch (address.kind()) {
    case dali::AddressKind::Short: return "channel";
    case dali::AddressKind::Group: return "group";
    case dali::AddressKind::Broadcast: return "broadcast";
    }
    return "unknown";
}

void log_result(dali::Address address, const lighting::ControlResult& result) {
    if (result.state.level) {
        ESP_LOGI(log_tag, "address=%s%u source=%s error=%s transport=%s arc-level=%u",
                 address_name(address),
                 address.as_short() ? static_cast<unsigned>(address.as_short()->value())
                     : address.as_group() ? static_cast<unsigned>(address.as_group()->value()) : 0,
                 source_name(result.state.source), error_name(result.error),
                 transport_name(result.transport_status),
                 static_cast<unsigned>(result.state.level->value()));
    } else {
        ESP_LOGI(log_tag, "address=%s source=%s error=%s transport=%s arc-level=unknown",
                 address_name(address), source_name(result.state.source), error_name(result.error),
                 transport_name(result.transport_status));
    }
}

nvs_handle_t open_targets() {
    auto error = nvs_flash_init();
    if (error != ESP_OK) {
        ESP_LOGE(log_tag, "NVS unavailable (%s); configuration will not be erased automatically", esp_err_to_name(error));
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

dali_master_handle_t open_dali() {
    dali_master_config_t config{};
    config.tx_gpio = board::dali_tx;
    config.rx_gpio = board::dali_rx;
    config.invert_tx = board::dali_invert_tx;
    config.invert_rx = board::dali_invert_rx;
    dali_master_rmt_config_t rmt{};
    dali_master_handle_t handle = nullptr;
    const auto error = dali_new_master_rmt(&config, &rmt, &handle);
    if (error != ESP_OK) {
        ESP_LOGE(log_tag, "DALI driver initialization failed (%s)", esp_err_to_name(error));
        return nullptr;
    }
    return handle;
}

struct Wiring {
    lighting::LightingService& lighting;
    Queue<control::Command>& commands;
    Queue<control::StatusUpdate>& statuses;
    Queue<config::Configuration>& configuration;
};

constexpr TickType_t status_send_wait = pdMS_TO_TICKS(50);

void publish_status(Queue<control::StatusUpdate>& statuses, dali::Address address, lighting::State state) {
    if (!statuses.push({address, state}, status_send_wait)) {
        ESP_LOGW(log_tag, "status queue full; dropped update for a %s", address_name(address));
    }
}

void publish_changes(Queue<control::StatusUpdate>& statuses, const lighting::LightingService& lighting,
                     const lighting::ControlResult& result) {
    for (std::size_t channel = 0; channel < dali::ShortAddress::capacity; ++channel) {
        if (result.changed_channels.test(channel)) {
            publish_status(statuses, dali::Address::short_address(*dali::ShortAddress::from(channel)),
                           lighting.state(*dali::ShortAddress::from(channel)));
        }
    }
    for (std::size_t group = 0; group < dali::Group::capacity; ++group) {
        if (result.changed_groups.test(group)) {
            publish_status(statuses, dali::Address::group(*dali::Group::from(group)),
                           lighting.state(*dali::Group::from(group)));
        }
    }
}

void apply_configuration(Wiring& wiring) {
    const auto configuration = wiring.configuration.pop(0);
    if (!configuration) {
        return;
    }
    wiring.lighting.configure(*configuration);
    unsigned reconciled = 0;
    unsigned failed = 0;
    for (unsigned channel = 0; channel < dali::ShortAddress::capacity; ++channel) {
        const auto result = wiring.lighting.reconcile_groups(*dali::ShortAddress::from(channel));
        if (result.transport_status == dali::TransportStatus::Ok) {
            ++reconciled;
        } else {
            ++failed;
        }
        publish_changes(wiring.statuses, wiring.lighting, result);
    }
    ESP_LOGI(log_tag, "applied ETS configuration; reconciled %u channels, %u failed", reconciled, failed);
}

void dali_task(void* arg) {
    auto& wiring = *static_cast<Wiring*>(arg);
    for (;;) {
        const auto command = wiring.commands.pop(portMAX_DELAY);
        if (!command) {
            continue;
        }
        apply_configuration(wiring);
        const auto result = control::execute(wiring.lighting, *command);
        log_result(control::address_of(*command), result);
        publish_changes(wiring.statuses, wiring.lighting, result);
    }
}

void knx_task(void* arg) {
    auto& wiring = *static_cast<Wiring*>(arg);
    KnxDevice device{wiring.commands, wiring.statuses, wiring.configuration};
    device.run();
}

}

void run() {
    const auto* description = esp_app_get_description();
    ESP_LOGI(log_tag, "%s %s: development slice, not a commissioned gateway", description->project_name, description->version);
    espidf::NvsLevelStore targets{open_targets()};
    espidf::DaliTransport transport{open_dali()};
    lighting::LightingService lighting{transport, targets};
    lighting.configure(config::default_configuration());
    Queue<control::Command> commands{64};
    Queue<control::StatusUpdate> statuses{96};
    Queue<config::Configuration> configuration{1};
    Wiring wiring{lighting, commands, statuses, configuration};
    xTaskCreate(dali_task, "dali", 4096, &wiring, 5, nullptr);
    xTaskCreate(knx_task, "knx", 12288, &wiring, 6, nullptr);
    UsbConsole console{commands, transport.configured(), targets.available()};
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
