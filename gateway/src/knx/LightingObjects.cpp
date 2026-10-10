#include "knx/LightingObjects.hpp"

#include "knx/BrightnessScale.hpp"
#include "knx/ObjectMap.hpp"

namespace gateway::knx {

std::optional<control::Command> command_for_switch(std::uint16_t object, bool on) noexcept {
    const auto target = object_target(object);
    if (!target || target->function != ObjectFunction::Switch) {
        return std::nullopt;
    }
    if (on) {
        return control::Command{control::SwitchOn{target->address}};
    }
    return control::Command{control::SwitchOff{target->address}};
}

std::optional<control::Command> command_for_scaling(std::uint16_t object, std::uint8_t raw) noexcept {
    const auto target = object_target(object);
    if (!target || target->function != ObjectFunction::Brightness) {
        return std::nullopt;
    }
    return control::Command{control::SetLevel{target->address, arc_from_scaling(raw)}};
}

std::optional<StatusObjects> status_objects(const control::StatusUpdate& update) noexcept {
    if (!update.state.level) {
        return std::nullopt;
    }
    const auto switch_object = object_number(update.address, ObjectFunction::SwitchStatus);
    const auto brightness_object = object_number(update.address, ObjectFunction::BrightnessStatus);
    if (switch_object == 0 || brightness_object == 0) {
        return std::nullopt;
    }
    const auto level = update.state.level->value();
    return StatusObjects{
        switch_object,
        level > 0,
        brightness_object,
        scaling_from_arc(*update.state.level),
    };
}

}
