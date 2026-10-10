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

std::optional<control::Command> command_for_relative_dimming(std::uint16_t object,
                                                           std::uint8_t raw) noexcept {
    const auto target = object_target(object);
    if (!target || target->function != ObjectFunction::RelativeDimming) {
        return std::nullopt;
    }
    const auto direction = (raw & 0x08) != 0 ? lighting::DimDirection::Up : lighting::DimDirection::Down;
    if ((raw & 0x07) == 0) {
        return control::Command{control::DimStop{target->address}};
    }
    return control::Command{control::DimStart{target->address, direction}};
}

std::optional<control::Command> command_for_scene(std::uint16_t object, std::uint8_t raw) noexcept {
    const auto target = object_target(object);
    if (!target || target->function != ObjectFunction::Scene) {
        return std::nullopt;
    }
    const auto number = raw & 0x3F;
    const auto scene = dali::Scene::from(number);
    if (!scene) {
        return std::nullopt;
    }
    if ((raw & 0x80) != 0) {
        return control::Command{control::StoreScene{*scene}};
    }
    return control::Command{control::RecallScene{*scene}};
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
