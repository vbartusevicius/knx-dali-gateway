#pragma once

#include <type_traits>
#include <variant>

#include "dali/Address.hpp"
#include "dali/ArcLevel.hpp"
#include "dali/Scene.hpp"
#include "dali/ShortAddress.hpp"
#include "lighting/LightingService.hpp"
#include "lighting/RelativeDimmer.hpp"

namespace gateway::control {

// Requests that cross task boundaries (console, KNX) into the lighting service.
struct SetLevel {
    dali::Address address;
    dali::ArcLevel level;
};

struct SwitchOn {
    dali::Address address;
};

struct SwitchOff {
    dali::Address address;
};

struct Refresh {
    dali::ShortAddress address;
};

struct Recover {
    dali::ShortAddress address;
};

struct ReconcileGroups {
    dali::ShortAddress address;
};

struct DimStart {
    dali::Address address;
    lighting::DimDirection direction;
};

struct DimStop {
    dali::Address address;
};

struct RecallScene {
    dali::Scene scene;
};

struct StoreScene {
    dali::Scene scene;
};

using Command = std::variant<SetLevel, SwitchOn, SwitchOff, Refresh, Recover, ReconcileGroups, DimStart,
                             DimStop, RecallScene, StoreScene>;

static_assert(std::is_trivially_copyable_v<Command>, "Command travels through FreeRTOS queues");

struct StatusUpdate {
    dali::Address address;
    lighting::State state;
};

static_assert(std::is_trivially_copyable_v<StatusUpdate>, "StatusUpdate travels through FreeRTOS queues");

[[nodiscard]] dali::Address address_of(const Command& command) noexcept;

// Dispatches one command: dim start/stop go to the worker's dimmer (a stop
// finishes dimming in the service), everything else to the service.
[[nodiscard]] lighting::ControlResult execute(lighting::LightingService& service,
                                              lighting::RelativeDimmer& dimmer, std::uint32_t now_ms,
                                              const Command& command);

}
