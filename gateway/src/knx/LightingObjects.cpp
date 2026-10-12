#include "knx/LightingObjects.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "control/Command.hpp"
#include "dali/Scene.hpp"
#include "knx/BrightnessScale.hpp"
#include "knx/ColourDatapoints.hpp"
#include "knx/ObjectMap.hpp"
#include "lighting/LightingService.hpp"
#include "lighting/RelativeDimmer.hpp"

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

std::optional<control::Command> command_for_colour_temperature(std::uint16_t object, const std::uint8_t* data,
                                                               std::size_t length) noexcept {
    const auto target = object_target(object);
    if (!target || target->function != ObjectFunction::ColourTemperature) {
        return std::nullopt;
    }
    const auto value = decode_colour_temperature(data, length);
    if (!value) {
        return std::nullopt;
    }
    return control::Command{control::SetColourTemperature{target->address, value->kelvin}};
}

std::optional<control::Command> command_for_relative_colour_temperature(std::uint16_t object,
                                                                        std::uint8_t raw) noexcept {
    const auto target = object_target(object);
    if (!target || target->function != ObjectFunction::RelativeColourTemperature) {
        return std::nullopt;
    }
    const auto direction = (raw & 0x08) != 0 ? lighting::DimDirection::Up : lighting::DimDirection::Down;
    if ((raw & 0x07) == 0) {
        return control::Command{control::ColourDimStop{target->address}};
    }
    return control::Command{control::ColourDimStart{target->address, direction}};
}

std::optional<control::Command> command_for_rgb(std::uint16_t object, const std::uint8_t* data,
                                                std::size_t length) noexcept {
    const auto target = object_target(object);
    if (!target || target->function != ObjectFunction::Rgb) {
        return std::nullopt;
    }
    const auto value = decode_rgb(data, length);
    if (!value) {
        return std::nullopt;
    }
    return control::Command{control::SetRgb{target->address, value->red, value->green, value->blue}};
}

std::optional<control::Command> command_for_rgbw(std::uint16_t object, const std::uint8_t* data,
                                                 std::size_t length) noexcept {
    const auto target = object_target(object);
    if (!target || target->function != ObjectFunction::Rgbw) {
        return std::nullopt;
    }
    const auto value = decode_rgbw(data, length);
    if (!value) {
        return std::nullopt;
    }
    return control::Command{control::SetRgbw{target->address, value->red, value->green, value->blue,
                                             value->white, value->validity}};
}

std::optional<control::Command> command_for_xyy(std::uint16_t object, const std::uint8_t* data,
                                                std::size_t length) noexcept {
    const auto target = object_target(object);
    if (!target || target->function != ObjectFunction::XyY) {
        return std::nullopt;
    }
    const auto value = decode_xyy(data, length);
    if (!value) {
        return std::nullopt;
    }
    return control::Command{control::SetXy{target->address, value->x, value->y,
                                           (value->validity & 0x02U) != 0, value->brightness,
                                           (value->validity & 0x01U) != 0}};
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

std::optional<ColourStatusObject> colour_status_object(const control::ColourUpdate& update) noexcept {
    ColourStatusObject status{};
    const auto& report = update.report;
    switch (report.kind) {
    case lighting::ColourKind::Temperature: {
        status.object = object_number(update.address, ObjectFunction::ColourTemperatureStatus);
        const auto data = encode_colour_temperature({report.kelvin});
        std::copy(data.begin(), data.end(), status.data.begin());
        status.length = static_cast<std::uint8_t>(data.size());
        break;
    }
    case lighting::ColourKind::Rgb: {
        status.object = object_number(update.address, ObjectFunction::RgbStatus);
        const auto data = encode_rgb({report.r, report.g, report.b});
        std::copy(data.begin(), data.end(), status.data.begin());
        status.length = static_cast<std::uint8_t>(data.size());
        break;
    }
    case lighting::ColourKind::Rgbw: {
        status.object = object_number(update.address, ObjectFunction::RgbwStatus);
        const auto data = encode_rgbw({report.r, report.g, report.b, report.w, report.rgbw_validity});
        std::copy(data.begin(), data.end(), status.data.begin());
        status.length = static_cast<std::uint8_t>(data.size());
        break;
    }
    case lighting::ColourKind::XyY: {
        status.object = object_number(update.address, ObjectFunction::XyYStatus);
        // A status always reports both fields, so both validity bits are
        // set regardless of which fields a write had carried.
        const auto data = encode_xyy({report.x, report.y, report.brightness, 0b11});
        std::copy(data.begin(), data.end(), status.data.begin());
        status.length = static_cast<std::uint8_t>(data.size());
        break;
    }
    case lighting::ColourKind::None:
        return std::nullopt;
    }
    if (status.object == 0) {
        return std::nullopt;
    }
    return status;
}

}
