#include "control/Command.hpp"

#include <cstdint>
#include <type_traits>
#include <variant>

#include "commissioning/Commissioning.hpp"
#include "dali/Address.hpp"
#include "dali/Transport.hpp"
#include "lighting/LightingService.hpp"
#include "lighting/RelativeDimmer.hpp"

namespace gateway::control {
namespace {

commissioning::JobResult wrap(lighting::ControlResult result) {
    return {result, {}};
}

bool instance_bound(std::uint8_t device, std::uint8_t instance, void* context) {
    return static_cast<inputs::InputMapper*>(context)->bound(device, instance);
}

// After a fresh input inventory, light channels pick up their reported
// resolution (lux step) from it.
void refresh_input_resolutions(commissioning::Commissioning& commissioning, inputs::InputMapper& inputs) {
    inputs.refresh_resolutions(commissioning.devices());
}

}

dali::Address address_of(const Command& command) noexcept {
    return std::visit(
        [](const auto& alternative) -> dali::Address {
            using Request = std::decay_t<decltype(alternative)>;
            if constexpr (std::is_same_v<Request, SetLevel> || std::is_same_v<Request, SwitchOn> ||
                          std::is_same_v<Request, SwitchOff> || std::is_same_v<Request, DimStart> ||
                          std::is_same_v<Request, DimStop> || std::is_same_v<Request, SetColourTemperature> ||
                          std::is_same_v<Request, SetRgb> || std::is_same_v<Request, SetRgbw> ||
                          std::is_same_v<Request, SetXy> || std::is_same_v<Request, ColourDimStart> ||
                          std::is_same_v<Request, ColourDimStop>) {
                return alternative.address;
            } else if constexpr (std::is_same_v<Request, RecallScene> ||
                                 std::is_same_v<Request, StoreScene> || std::is_same_v<Request, ScanBus> ||
                                 std::is_same_v<Request, AssignAddresses> ||
                                 std::is_same_v<Request, ResetAddresses> ||
                                 std::is_same_v<Request, ScanInputDevices> ||
                                 std::is_same_v<Request, AssignInputDevices> ||
                                 std::is_same_v<Request, InputFeedback>) {
                return dali::Address::broadcast();
            } else {
                return dali::Address::short_address(alternative.address);
            }
        },
        command);
}

commissioning::JobResult execute(lighting::LightingService& service, lighting::RelativeDimmer& dimmer,
                                 lighting::RelativeDimmer& colour_dimmer,
                                 commissioning::Commissioning& commissioning, inputs::InputMapper& inputs,
                                 std::uint32_t now_ms, const Command& command) {
    return std::visit(
        [&](const auto& alternative) -> commissioning::JobResult {
            using Request = std::decay_t<decltype(alternative)>;
            if constexpr (std::is_same_v<Request, SetLevel>) {
                return wrap(service.set_level(alternative.address, alternative.level));
            } else if constexpr (std::is_same_v<Request, SwitchOn>) {
                return wrap(service.switch_on(alternative.address));
            } else if constexpr (std::is_same_v<Request, SwitchOff>) {
                return wrap(service.switch_off(alternative.address));
            } else if constexpr (std::is_same_v<Request, Refresh>) {
                return wrap(service.refresh(alternative.address));
            } else if constexpr (std::is_same_v<Request, Recover>) {
                return wrap(service.recover(alternative.address));
            } else if constexpr (std::is_same_v<Request, ReconcileGroups>) {
                return wrap(service.reconcile_groups(alternative.address));
            } else if constexpr (std::is_same_v<Request, RecallScene>) {
                return wrap(service.recall_scene(alternative.scene));
            } else if constexpr (std::is_same_v<Request, StoreScene>) {
                return wrap(service.store_scene(alternative.scene));
            } else if constexpr (std::is_same_v<Request, ScanBus>) {
                return commissioning.scan();
            } else if constexpr (std::is_same_v<Request, AssignAddresses>) {
                return commissioning.assign(alternative.mode);
            } else if constexpr (std::is_same_v<Request, ReplaceDevice>) {
                return commissioning.replace(alternative.address);
            } else if constexpr (std::is_same_v<Request, IdentifyDevice>) {
                return commissioning.identify(alternative.address);
            } else if constexpr (std::is_same_v<Request, ResetAddresses>) {
                return commissioning.reset_addresses();
            } else if constexpr (std::is_same_v<Request, ScanInputDevices>) {
                auto result = commissioning.scan_inputs(&instance_bound, &inputs);
                refresh_input_resolutions(commissioning, inputs);
                return result;
            } else if constexpr (std::is_same_v<Request, AssignInputDevices>) {
                auto result = commissioning.assign_inputs(alternative.mode, &instance_bound, &inputs);
                refresh_input_resolutions(commissioning, inputs);
                return result;
            } else if constexpr (std::is_same_v<Request, InputFeedback>) {
                inputs.switch_feedback(alternative.channel.value(), alternative.on);
                return wrap({lighting::ControlError::None, dali::TransportStatus::Ok, {}, {}, {}});
            } else if constexpr (std::is_same_v<Request, SetColourTemperature>) {
                return wrap(service.set_colour_temperature(alternative.address, alternative.kelvin));
            } else if constexpr (std::is_same_v<Request, SetRgb>) {
                return wrap(service.set_rgb(alternative.address, alternative.red, alternative.green,
                                            alternative.blue));
            } else if constexpr (std::is_same_v<Request, SetRgbw>) {
                return wrap(service.set_rgbw(alternative.address, alternative.red, alternative.green,
                                             alternative.blue, alternative.white, alternative.validity));
            } else if constexpr (std::is_same_v<Request, SetXy>) {
                return wrap(service.set_xy(alternative.address, alternative.x, alternative.y,
                                           alternative.xy_valid, alternative.brightness,
                                           alternative.brightness_valid));
            } else if constexpr (std::is_same_v<Request, ColourDimStart>) {
                colour_dimmer.start(alternative.address, alternative.direction, now_ms);
                return wrap({lighting::ControlError::None, dali::TransportStatus::Ok, {}, {}, {}});
            } else if constexpr (std::is_same_v<Request, ColourDimStop>) {
                if (colour_dimmer.stop(alternative.address)) {
                    return wrap(service.finish_colour_temperature(alternative.address));
                }
                return wrap({lighting::ControlError::None, dali::TransportStatus::Ok, {}, {}, {}});
            } else if constexpr (std::is_same_v<Request, DimStart>) {
                dimmer.start(alternative.address, alternative.direction, now_ms);
                return wrap({lighting::ControlError::None, dali::TransportStatus::Ok, {}, {}, {}});
            } else {
                // DimStop: only a running dim needs a readback afterwards.
                if (dimmer.stop(alternative.address)) {
                    return wrap(service.finish_dimming(alternative.address));
                }
                return wrap({lighting::ControlError::None, dali::TransportStatus::Ok, {}, {}, {}});
            }
        },
        command);
}

}
