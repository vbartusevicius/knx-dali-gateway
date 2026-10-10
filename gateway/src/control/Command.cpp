#include "control/Command.hpp"

namespace gateway::control {

dali::Address address_of(const Command& command) noexcept {
    return std::visit(
        [](const auto& alternative) -> dali::Address {
            using Request = std::decay_t<decltype(alternative)>;
            if constexpr (std::is_same_v<Request, SetLevel> || std::is_same_v<Request, SwitchOn> ||
                          std::is_same_v<Request, SwitchOff> || std::is_same_v<Request, DimStart> ||
                          std::is_same_v<Request, DimStop>) {
                return alternative.address;
            } else if constexpr (std::is_same_v<Request, RecallScene> || std::is_same_v<Request, StoreScene>) {
                return dali::Address::broadcast();
            } else {
                return dali::Address::short_address(alternative.address);
            }
        },
        command);
}

lighting::ControlResult execute(lighting::LightingService& service, lighting::RelativeDimmer& dimmer,
                                std::uint32_t now_ms, const Command& command) {
    return std::visit(
        [&](const auto& alternative) -> lighting::ControlResult {
            using Request = std::decay_t<decltype(alternative)>;
            if constexpr (std::is_same_v<Request, SetLevel>) {
                return service.set_level(alternative.address, alternative.level);
            } else if constexpr (std::is_same_v<Request, SwitchOn>) {
                return service.switch_on(alternative.address);
            } else if constexpr (std::is_same_v<Request, SwitchOff>) {
                return service.switch_off(alternative.address);
            } else if constexpr (std::is_same_v<Request, Refresh>) {
                return service.refresh(alternative.address);
            } else if constexpr (std::is_same_v<Request, Recover>) {
                return service.recover(alternative.address);
            } else if constexpr (std::is_same_v<Request, ReconcileGroups>) {
                return service.reconcile_groups(alternative.address);
            } else if constexpr (std::is_same_v<Request, RecallScene>) {
                return service.recall_scene(alternative.scene);
            } else if constexpr (std::is_same_v<Request, StoreScene>) {
                return service.store_scene(alternative.scene);
            } else if constexpr (std::is_same_v<Request, DimStart>) {
                dimmer.start(alternative.address, alternative.direction, now_ms);
                return {lighting::ControlError::None, dali::TransportStatus::Ok, {}, {}, {}};
            } else {
                // DimStop: only a running dim needs a readback afterwards.
                if (dimmer.stop(alternative.address)) {
                    return service.finish_dimming(alternative.address);
                }
                return {lighting::ControlError::None, dali::TransportStatus::Ok, {}, {}, {}};
            }
        },
        command);
}

}
