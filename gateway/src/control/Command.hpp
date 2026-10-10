#pragma once

#include <type_traits>
#include <variant>

#include "dali/Address.hpp"
#include "dali/ArcLevel.hpp"
#include "dali/ShortAddress.hpp"
#include "lighting/LightingService.hpp"

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

using Command = std::variant<SetLevel, SwitchOn, SwitchOff, Refresh, Recover, ReconcileGroups>;

static_assert(std::is_trivially_copyable_v<Command>, "Command travels through FreeRTOS queues");

struct StatusUpdate {
    dali::Address address;
    lighting::State state;
};

static_assert(std::is_trivially_copyable_v<StatusUpdate>, "StatusUpdate travels through FreeRTOS queues");

[[nodiscard]] dali::Address address_of(const Command& command) noexcept;
[[nodiscard]] lighting::ControlResult execute(lighting::LightingService& service, const Command& command);

}
