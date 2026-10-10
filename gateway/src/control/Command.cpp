#include "control/Command.hpp"

namespace gateway::control {

dali::Address address_of(const Command& command) noexcept {
    return std::visit(
        [](const auto& alternative) -> dali::Address {
            using Request = std::decay_t<decltype(alternative)>;
            if constexpr (std::is_same_v<Request, SetLevel> || std::is_same_v<Request, SwitchOn> ||
                          std::is_same_v<Request, SwitchOff>) {
                return alternative.address;
            } else {
                return dali::Address::short_address(alternative.address);
            }
        },
        command);
}

lighting::ControlResult execute(lighting::LightingService& service, const Command& command) {
    return std::visit(
        [&service](const auto& alternative) -> lighting::ControlResult {
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
            } else {
                return service.reconcile_groups(alternative.address);
            }
        },
        command);
}

}
