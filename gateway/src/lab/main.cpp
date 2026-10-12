// Integration-lab gateway: the real application modules (LightingService,
// RelativeDimmer, Commissioning, control::execute, knxstack::KnxApplication)
// on the thelsing stack's LinuxPlatform with KNXnet/IP (mask 0x57B0) instead
// of TP-UART, and a TCP socket to the Python DALI bus simulator instead of
// the UART driver. Linux only; runs inside the simulation container.
//
// Environment: DALI_SIM_HOST / DALI_SIM_PORT (default 127.0.0.1:5555),
// KNX_FLASH_FILE (default lab-flash.bin), DALI_LEVELS_FILE (default
// lab-levels.txt).

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <thread>

#include <knx/bau57B0.h>
#include <knx_facade.h>
#include <linux_platform.h>

#include "commissioning/Commissioning.hpp"
#include "config/Configuration.hpp"
#include "control/Command.hpp"
#include "control/Publish.hpp"
#include "control/WorkerWait.hpp"
#include "dali/RetryingTransport.hpp"
#include "dali/ShortAddress.hpp"
#include "dali/Transport.hpp"
#include "dali/devices/EventDecoder.hpp"
#include "inputs/InputMapper.hpp"
#include "knxstack/KnxApplication.hpp"
#include "lab/FileLevelStore.hpp"
#include "lab/SocketDaliTransport.hpp"
#include "lab/StdQueue.hpp"
#include "lighting/LightingService.hpp"
#include "lighting/RelativeDimmer.hpp"

namespace {

std::atomic<bool> active{true};

void on_signal(int) {
    active = false;
}

std::uint32_t now_ms() {
    return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

constexpr std::uint32_t fault_poll_interval_ms = 1000;
constexpr std::uint32_t max_idle_wait_ms = 1000;
constexpr std::uint32_t configuration_wait_ms = 3000;
constexpr std::uint32_t status_send_wait_ms = 50;

const char* env_or(const char* name, const char* fallback) {
    const auto* value = std::getenv(name);
    return value ? value : fallback;
}

const char* transport_name(gateway::dali::TransportStatus status) {
    using gateway::dali::TransportStatus;
    switch (status) {
    case TransportStatus::Ok:
        return "ok";
    case TransportStatus::Unconfigured:
        return "unconfigured";
    case TransportStatus::Timeout:
        return "timeout";
    case TransportStatus::Collision:
        return "collision";
    case TransportStatus::InvalidResponse:
        return "invalid-response";
    case TransportStatus::Unsupported:
        return "unsupported";
    case TransportStatus::Failure:
        return "failure";
    }
    return "?";
}

const char* error_name(gateway::lighting::ControlError error) {
    using gateway::lighting::ControlError;
    switch (error) {
    case ControlError::None:
        return "none";
    case ControlError::TransportFailure:
        return "transport";
    case ControlError::NoSavedTarget:
        return "no-saved-target";
    case ControlError::StorageFailure:
        return "storage";
    case ControlError::Unsupported:
        return "unsupported";
    case ControlError::InvalidValue:
        return "invalid-value";
    }
    return "?";
}

struct Wiring {
    gateway::lighting::LightingService& service;
    gateway::dali::Transport& transport;
    gateway::commissioning::Commissioning& commissioning;
    gateway::inputs::InputMapper& inputs;
    gateway::lab::StdQueue<gateway::control::Command>& commands;
    gateway::lab::StdQueue<gateway::control::StatusEvent>& statuses;
    gateway::lab::StdQueue<gateway::config::Configuration>& configuration;
    gateway::lab::StdQueue<gateway::commissioning::Snapshot>& snapshots;
    gateway::lab::StdQueue<gateway::commissioning::InputSnapshot>& input_snapshots;
    gateway::commissioning::Snapshot latest{};
    gateway::commissioning::InputSnapshot latest_input{};
};

bool input_instance_bound(std::uint8_t device, std::uint8_t instance, void* context) {
    return static_cast<gateway::inputs::InputMapper*>(context)->bound(device, instance);
}

void log_line(const char* tag, const char* message) {
    std::fprintf(stderr, "[%s] %s\n", tag, message);
    std::fflush(stderr);
}

void publish_status(Wiring& wiring, const gateway::control::StatusEvent& event) {
    if (!wiring.statuses.push(event, status_send_wait_ms)) {
        log_line("worker", "status queue full; dropped an update");
    }
}

void publish_job_result(Wiring& wiring, const gateway::commissioning::JobResult& result) {
    const auto push = [&wiring](const gateway::control::StatusEvent& event) {
        publish_status(wiring, event);
        return true;
    };
    gateway::control::publish_job_result(push, wiring.service, wiring.commissioning, result);
    wiring.input_snapshots.overwrite(wiring.commissioning.input_snapshot());
}

void apply_configuration(Wiring& wiring, const gateway::config::Configuration& configuration) {
    wiring.service.configure(configuration);
    wiring.inputs.configure(configuration);
    unsigned reconciled = 0;
    unsigned failed = 0;
    for (unsigned channel = 0; channel < gateway::dali::ShortAddress::capacity; ++channel) {
        const auto result = wiring.service.reconcile_groups(*gateway::dali::ShortAddress::from(channel));
        if (result.transport_status == gateway::dali::TransportStatus::Ok) {
            ++reconciled;
        } else {
            ++failed;
        }
        const auto push = [&wiring](const gateway::control::StatusEvent& event) {
            publish_status(wiring, event);
            return true;
        };
        gateway::control::publish_changes(push, wiring.service, result);
    }
    std::fprintf(stderr, "[worker] applied ETS configuration; reconciled %u channels, %u failed\n",
                 reconciled, failed);
}

class SnapshotForwarder final : public gateway::commissioning::SnapshotListener {
public:
    explicit SnapshotForwarder(Wiring& wiring) : wiring_(wiring) {}

    void on_snapshot(const gateway::commissioning::Snapshot& snapshot) override {
        wiring_.snapshots.overwrite(snapshot);
    }

private:
    Wiring& wiring_;
};

void worker(Wiring& wiring) {
    SnapshotForwarder snapshot_forwarder{wiring};
    wiring.commissioning.set_listener(&snapshot_forwarder);
    gateway::lighting::RelativeDimmer dimmer;
    gateway::lighting::RelativeDimmer colour_dimmer;

    // Wait briefly for the KNX task to deliver ETS parameters, then scan and
    // recover present gear — same sequence as the firmware.
    if (const auto configuration = wiring.configuration.pop(configuration_wait_ms)) {
        apply_configuration(wiring, *configuration);
    }
    log_line("worker", "startup: scanning the DALI bus");
    publish_job_result(wiring, wiring.commissioning.startup(&input_instance_bound, &wiring.inputs));
    wiring.inputs.refresh_resolutions(wiring.commissioning.devices());
    std::fprintf(stderr, "[worker] startup: %u present, %u faulty\n",
                 static_cast<unsigned>(wiring.commissioning.present().count()),
                 static_cast<unsigned>(wiring.commissioning.faulty().count()));

    auto next_poll = now_ms() + fault_poll_interval_ms;
    while (active) {
        auto deadline = dimmer.next_deadline();
        if (const auto colour = colour_dimmer.next_deadline();
            colour && (!deadline || static_cast<std::int32_t>(*colour - *deadline) < 0)) {
            deadline = colour;
        }
        const auto command = wiring.commands.pop(
            gateway::control::worker_wait_ms(now_ms(), deadline, next_poll, max_idle_wait_ms));
        const auto now = now_ms();
        if (command) {
            if (const auto configuration = wiring.configuration.pop(0)) {
                apply_configuration(wiring, *configuration);
            }
            const auto result = gateway::control::execute(wiring.service, dimmer, colour_dimmer,
                                                          wiring.commissioning, wiring.inputs, now, *command);
            if (result.result.error != gateway::lighting::ControlError::None) {
                std::fprintf(stderr, "[worker] command failed: %s (transport %s)\n",
                             error_name(result.result.error), transport_name(result.result.transport_status));
            }
            publish_job_result(wiring, result);
        } else if (static_cast<std::int32_t>(now - next_poll) >= 0) {
            next_poll = now + fault_poll_interval_ms;
            if (const auto changed = wiring.commissioning.poll_next()) {
                std::fprintf(stderr, "[worker] channel %u fault -> %u\n",
                             static_cast<unsigned>(changed->value()),
                             static_cast<unsigned>(wiring.commissioning.faulty().test(changed->value())));
                publish_status(wiring, gateway::control::FaultUpdate{
                                           *changed, wiring.commissioning.faulty().test(changed->value())});
                snapshot_forwarder.on_snapshot(wiring.commissioning.snapshot());
            }
        }
        dimmer.poll(
            now,
            [&wiring](gateway::lighting::DimStep step) {
                const auto step_result = wiring.service.dim_step(step.address, step.direction, step.first);
                if (step_result.error != gateway::lighting::ControlError::None) {
                    log_line("worker", "dim step failed");
                }
            },
            [&wiring](gateway::lighting::DimExpiry expiry) {
                log_line("worker", "dim hit the safety limit; reading back");
                const auto result = wiring.service.finish_dimming(expiry.address);
                const auto push = [&wiring](const gateway::control::StatusEvent& event) {
                    publish_status(wiring, event);
                    return true;
                };
                gateway::control::publish_changes(push, wiring.service, result);
                gateway::control::publish_colour_changes(push, wiring.service, result);
            });
        colour_dimmer.poll(
            now,
            [&wiring](gateway::lighting::DimStep step) {
                const auto step_result = wiring.service.colour_temperature_step(step.address, step.direction);
                if (step_result.error != gateway::lighting::ControlError::None) {
                    log_line("worker", "colour temperature step failed");
                }
            },
            [&wiring](gateway::lighting::DimExpiry expiry) {
                log_line("worker", "colour dimming hit the safety limit; reading back");
                const auto result = wiring.service.finish_colour_temperature(expiry.address);
                const auto push = [&wiring](const gateway::control::StatusEvent& event) {
                    publish_status(wiring, event);
                    return true;
                };
                gateway::control::publish_colour_changes(push, wiring.service, result);
            });
        while (const auto event = wiring.transport.poll_event()) {
            if (event->bits != 24) {
                std::fprintf(stderr, "[worker] dali frame from another master: 0x%06X (%u bits)\n",
                             static_cast<unsigned>(event->value), static_cast<unsigned>(event->bits));
                continue;
            }
            const auto decoded = gateway::dali::device::decode_event(*event);
            if (!decoded) {
                std::fprintf(stderr, "[worker] dali event dropped: 0x%06X\n",
                             static_cast<unsigned>(event->value));
                continue;
            }
            std::array<gateway::inputs::Action, gateway::inputs::InputMapper::max_actions> actions;
            const auto count = wiring.inputs.map(
                *decoded, wiring.commissioning.devices().instance_type(decoded->address, decoded->instance),
                actions);
            for (std::uint8_t index = 0; index < count; ++index) {
                const auto& action = actions[index];
                publish_status(wiring,
                               gateway::control::InputAction{action.object, action.data, action.length});
            }
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    LinuxPlatform platform;
    platform.flashFilePath(env_or("KNX_FLASH_FILE", "lab-flash.bin"));
    platform.cmdLineArgs(argc, argv);  // execv-based restart() needs argv

    Bau57B0 bau(platform);
    KnxFacade<LinuxPlatform, Bau57B0> knx(bau);

    gateway::lab::SocketDaliTransport socket_transport(env_or("DALI_SIM_HOST", "127.0.0.1"), 5555);
    for (unsigned attempt = 0; !socket_transport.connected() && attempt < 120; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        socket_transport.try_connect();
    }
    if (socket_transport.connected()) {
        log_line("lab", "connected to the DALI bus simulator");
    } else {
        log_line("lab", "DALI bus simulator unreachable; continuing unconfigured");
    }
    gateway::dali::RetryingTransport transport{socket_transport};
    gateway::lab::FileLevelStore store{env_or("DALI_LEVELS_FILE", "lab-levels.txt")};
    gateway::lighting::LightingService service{transport, store};
    service.configure(gateway::config::default_configuration());
    gateway::commissioning::Commissioning commissioning{transport, service};
    gateway::inputs::InputMapper inputs;
    inputs.configure(gateway::config::default_configuration());

    gateway::lab::StdQueue<gateway::control::Command> commands{64};
    gateway::lab::StdQueue<gateway::control::StatusEvent> statuses{96};
    gateway::lab::StdQueue<gateway::config::Configuration> configuration{1};
    gateway::lab::StdQueue<gateway::commissioning::Snapshot> snapshots{1};
    gateway::lab::StdQueue<gateway::commissioning::InputSnapshot> input_snapshots{1};
    Wiring wiring{service,       transport, commissioning,   inputs, commands, statuses,
                  configuration, snapshots, input_snapshots, {},     {}};

    using Facade = KnxFacade<LinuxPlatform, Bau57B0>;
    gateway::knxstack::KnxApplication<Facade>::Hooks hooks{
        [](void* ctx, const gateway::control::Command& command) {
            return static_cast<Wiring*>(ctx)->commands.try_push(command);
        },
        [](void* ctx) { return static_cast<Wiring*>(ctx)->statuses.pop(0); },
        [](void* ctx, const gateway::config::Configuration& config) {
            return static_cast<Wiring*>(ctx)->configuration.overwrite(config);
        },
        [](void* ctx) -> gateway::commissioning::Snapshot {
            auto& w = *static_cast<Wiring*>(ctx);
            while (const auto snapshot = w.snapshots.pop(0)) {
                w.latest = *snapshot;
            }
            return w.latest;
        },
        [](void* ctx) -> gateway::commissioning::InputSnapshot {
            auto& w = *static_cast<Wiring*>(ctx);
            while (const auto snapshot = w.input_snapshots.pop(0)) {
                w.latest_input = *snapshot;
            }
            return w.latest_input;
        },
        [](void*, const char* message) { log_line("knx", message); },
        &wiring,
    };
    gateway::knxstack::KnxApplication<Facade> app{knx, hooks};

    std::thread worker_thread(worker, std::ref(wiring));
    app.start();

    while (active) {
        app.loop();
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }

    log_line("lab", "shutting down");
    worker_thread.join();
    knx.writeMemory();
    return 0;
}
