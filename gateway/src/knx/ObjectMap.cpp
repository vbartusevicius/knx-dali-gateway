#include "knx/ObjectMap.hpp"

#include <cstdint>
#include <optional>

#include "dali/Address.hpp"

namespace gateway::knx {

static_assert(CH_ChannelCount == 64, "knxprod must define 64 channels");
static_assert(CH_KoBlockSize == 15, "knxprod must define 15 group objects per channel");
static_assert(CH_KoBlockOffset == 1, "channel group objects are 1-based");
static_assert(GR_ChannelCount == 16, "knxprod must define 16 DALI groups");
static_assert(GR_KoBlockSize == 14, "knxprod must define 14 group objects per group");
static_assert(GR_KoBlockOffset == CH_KoBlockOffset + CH_ChannelCount * CH_KoBlockSize,
              "group objects must follow the channel objects");
static_assert(CH_KoSwitch == 0, "knxprod object 0 must be Switch");
static_assert(CH_KoSwitchStatus == 1, "knxprod object 1 must be Switch status");
static_assert(CH_KoBrightness == 2, "knxprod object 2 must be Brightness");
static_assert(CH_KoBrightnessStatus == 3, "knxprod object 3 must be Brightness status");
static_assert(CH_KoRelativeDimming == 4, "knxprod object 4 must be Relative dimming");
static_assert(CH_KoFault == 5, "knxprod object 5 must be Fault");
static_assert(CH_KoColourTemperature == 6, "knxprod object 6 must be Colour temperature");
static_assert(CH_KoColourTemperatureStatus == 7, "object 7 must be Colour temperature status");
static_assert(CH_KoRelativeColourTemperature == 8, "object 8 must be Relative colour temperature");
static_assert(CH_KoRgb == 9, "knxprod object 9 must be RGB");
static_assert(CH_KoRgbStatus == 10, "knxprod object 10 must be RGB status");
static_assert(CH_KoRgbw == 11, "knxprod object 11 must be RGBW");
static_assert(CH_KoRgbwStatus == 12, "knxprod object 12 must be RGBW status");
static_assert(CH_KoXyY == 13, "knxprod object 13 must be xyY");
static_assert(CH_KoXyYStatus == 14, "knxprod object 14 must be xyY status");
static_assert(GR_KoSwitch == CH_KoSwitch && GR_KoSwitchStatus == CH_KoSwitchStatus &&
                  GR_KoBrightness == CH_KoBrightness && GR_KoBrightnessStatus == CH_KoBrightnessStatus &&
                  GR_KoRelativeDimming == CH_KoRelativeDimming &&
                  GR_KoColourTemperature == CH_KoColourTemperature - 1 &&
                  GR_KoColourTemperatureStatus == CH_KoColourTemperatureStatus - 1 &&
                  GR_KoRelativeColourTemperature == CH_KoRelativeColourTemperature - 1 &&
                  GR_KoRgb == CH_KoRgb - 1 && GR_KoRgbStatus == CH_KoRgbStatus - 1 &&
                  GR_KoRgbw == CH_KoRgbw - 1 && GR_KoRgbwStatus == CH_KoRgbwStatus - 1 &&
                  GR_KoXyY == CH_KoXyY - 1 && GR_KoXyYStatus == CH_KoXyYStatus - 1,
              "channel and group colour objects must share the function order");
static_assert(CH_KoBcSwitch == GR_KoBlockOffset + GR_ChannelCount * GR_KoBlockSize,
              "broadcast objects must follow the group objects");
static_assert(CH_KoBcSwitch + 1 == CH_KoBcBrightness && CH_KoBcSwitch + 2 == CH_KoBcRelativeDimming &&
                  CH_KoBcSwitch + 3 == CH_KoBcScene && CH_KoBcSwitch + 4 == CH_KoBcColourTemperature &&
                  CH_KoBcSwitch + 5 == CH_KoBcRelativeColourTemperature && CH_KoBcSwitch + 6 == CH_KoBcRgb &&
                  CH_KoBcSwitch + 7 == CH_KoBcRgbw && CH_KoBcSwitch + 8 == CH_KoBcXyY,
              "broadcast object order must be Switch, Brightness, Relative dimming, Scene, "
              "Colour temperature, Relative colour temperature, RGB, RGBW, xyY");
// knxprod input module: 8 device slots x 4 instances x 4 objects. Slot s,
// instance i is input channel s * 4 + i, so input objects stay numbered
// 1194 + channel * 4 + function.
constexpr unsigned input_object_stride = IN_KoI1Switch - IN_KoI0Switch;
constexpr unsigned input_channel_count = IN_ChannelCount * (IN_KoBlockSize / input_object_stride);
static_assert(input_object_stride == 4 && IN_KoBlockSize == 16,
              "knxprod must define 4 objects per input instance");
static_assert(input_channel_count == 32, "knxprod must define 8 device slots of 4 instances");
static_assert(IN_KoBlockOffset == CH_KoBcXyY + 1, "input objects must follow the broadcast objects");
static_assert(IN_KoI0Switch == 0 && IN_KoI0Dimming == 1 && IN_KoI0Illuminance == 2 && IN_KoI0Scene == 3 &&
                  IN_KoI3Scene == 15,
              "input object order must be Switch, Dimming, Illuminance, Scene per instance");
static_assert(object_count == IN_KoBlockOffset + IN_ChannelCount * IN_KoBlockSize - 1,
              "the last input object must be the last group object");
static_assert(object_count == 64 * 15 + 16 * 14 + 9 + 32 * 4, "object total must be 1321");

namespace {

constexpr std::uint16_t channel_offset(ObjectFunction function) noexcept {
    switch (function) {
    case ObjectFunction::Switch:
        return CH_KoSwitch;
    case ObjectFunction::SwitchStatus:
        return CH_KoSwitchStatus;
    case ObjectFunction::Brightness:
        return CH_KoBrightness;
    case ObjectFunction::BrightnessStatus:
        return CH_KoBrightnessStatus;
    case ObjectFunction::RelativeDimming:
        return CH_KoRelativeDimming;
    case ObjectFunction::Fault:
        return CH_KoFault;
    case ObjectFunction::ColourTemperature:
        return CH_KoColourTemperature;
    case ObjectFunction::ColourTemperatureStatus:
        return CH_KoColourTemperatureStatus;
    case ObjectFunction::RelativeColourTemperature:
        return CH_KoRelativeColourTemperature;
    case ObjectFunction::Rgb:
        return CH_KoRgb;
    case ObjectFunction::RgbStatus:
        return CH_KoRgbStatus;
    case ObjectFunction::Rgbw:
        return CH_KoRgbw;
    case ObjectFunction::RgbwStatus:
        return CH_KoRgbwStatus;
    case ObjectFunction::XyY:
        return CH_KoXyY;
    case ObjectFunction::XyYStatus:
        return CH_KoXyYStatus;
    case ObjectFunction::Scene:
        break;
    }
    return 0;
}

constexpr std::uint16_t group_offset(ObjectFunction function) noexcept {
    switch (function) {
    case ObjectFunction::Switch:
        return GR_KoSwitch;
    case ObjectFunction::SwitchStatus:
        return GR_KoSwitchStatus;
    case ObjectFunction::Brightness:
        return GR_KoBrightness;
    case ObjectFunction::BrightnessStatus:
        return GR_KoBrightnessStatus;
    case ObjectFunction::RelativeDimming:
        return GR_KoRelativeDimming;
    case ObjectFunction::ColourTemperature:
        return GR_KoColourTemperature;
    case ObjectFunction::ColourTemperatureStatus:
        return GR_KoColourTemperatureStatus;
    case ObjectFunction::RelativeColourTemperature:
        return GR_KoRelativeColourTemperature;
    case ObjectFunction::Rgb:
        return GR_KoRgb;
    case ObjectFunction::RgbStatus:
        return GR_KoRgbStatus;
    case ObjectFunction::Rgbw:
        return GR_KoRgbw;
    case ObjectFunction::RgbwStatus:
        return GR_KoRgbwStatus;
    case ObjectFunction::XyY:
        return GR_KoXyY;
    case ObjectFunction::XyYStatus:
        return GR_KoXyYStatus;
    case ObjectFunction::Fault:
    case ObjectFunction::Scene:
        break;
    }
    return 0;
}

std::optional<ObjectFunction> channel_function(unsigned index) noexcept {
    switch (index) {
    case CH_KoSwitch:
        return ObjectFunction::Switch;
    case CH_KoSwitchStatus:
        return ObjectFunction::SwitchStatus;
    case CH_KoBrightness:
        return ObjectFunction::Brightness;
    case CH_KoBrightnessStatus:
        return ObjectFunction::BrightnessStatus;
    case CH_KoRelativeDimming:
        return ObjectFunction::RelativeDimming;
    case CH_KoFault:
        return ObjectFunction::Fault;
    case CH_KoColourTemperature:
        return ObjectFunction::ColourTemperature;
    case CH_KoColourTemperatureStatus:
        return ObjectFunction::ColourTemperatureStatus;
    case CH_KoRelativeColourTemperature:
        return ObjectFunction::RelativeColourTemperature;
    case CH_KoRgb:
        return ObjectFunction::Rgb;
    case CH_KoRgbStatus:
        return ObjectFunction::RgbStatus;
    case CH_KoRgbw:
        return ObjectFunction::Rgbw;
    case CH_KoRgbwStatus:
        return ObjectFunction::RgbwStatus;
    case CH_KoXyY:
        return ObjectFunction::XyY;
    case CH_KoXyYStatus:
        return ObjectFunction::XyYStatus;
    default:
        return std::nullopt;
    }
}

std::optional<ObjectFunction> group_function(unsigned index) noexcept {
    switch (index) {
    case GR_KoSwitch:
        return ObjectFunction::Switch;
    case GR_KoSwitchStatus:
        return ObjectFunction::SwitchStatus;
    case GR_KoBrightness:
        return ObjectFunction::Brightness;
    case GR_KoBrightnessStatus:
        return ObjectFunction::BrightnessStatus;
    case GR_KoRelativeDimming:
        return ObjectFunction::RelativeDimming;
    case GR_KoColourTemperature:
        return ObjectFunction::ColourTemperature;
    case GR_KoColourTemperatureStatus:
        return ObjectFunction::ColourTemperatureStatus;
    case GR_KoRelativeColourTemperature:
        return ObjectFunction::RelativeColourTemperature;
    case GR_KoRgb:
        return ObjectFunction::Rgb;
    case GR_KoRgbStatus:
        return ObjectFunction::RgbStatus;
    case GR_KoRgbw:
        return ObjectFunction::Rgbw;
    case GR_KoRgbwStatus:
        return ObjectFunction::RgbwStatus;
    case GR_KoXyY:
        return ObjectFunction::XyY;
    case GR_KoXyYStatus:
        return ObjectFunction::XyYStatus;
    default:
        return std::nullopt;
    }
}

constexpr std::uint16_t channel_objects_end = CH_KoBlockOffset + CH_ChannelCount * CH_KoBlockSize;
constexpr std::uint16_t group_objects_end = GR_KoBlockOffset + GR_ChannelCount * GR_KoBlockSize;
constexpr std::uint16_t input_objects_end = IN_KoBlockOffset + IN_ChannelCount * IN_KoBlockSize;

constexpr std::uint16_t input_offset(InputObjectFunction function) noexcept {
    switch (function) {
    case InputObjectFunction::Switch:
        return IN_KoI0Switch;
    case InputObjectFunction::Dimming:
        return IN_KoI0Dimming;
    case InputObjectFunction::Illuminance:
        return IN_KoI0Illuminance;
    case InputObjectFunction::Scene:
        return IN_KoI0Scene;
    }
    return 0;
}

std::optional<InputObjectFunction> input_function(unsigned index) noexcept {
    switch (index) {
    case IN_KoI0Switch:
        return InputObjectFunction::Switch;
    case IN_KoI0Dimming:
        return InputObjectFunction::Dimming;
    case IN_KoI0Illuminance:
        return InputObjectFunction::Illuminance;
    case IN_KoI0Scene:
        return InputObjectFunction::Scene;
    default:
        return std::nullopt;
    }
}

}

std::uint16_t object_number(dali::Address address, ObjectFunction function) noexcept {
    switch (address.kind()) {
    case dali::AddressKind::Short:
        if (function == ObjectFunction::Scene) {
            return 0;
        }
        return static_cast<std::uint16_t>(CH_KoBlockOffset + CH_KoBlockSize * address.as_short()->value() +
                                          channel_offset(function));
    case dali::AddressKind::Group:
        if (function == ObjectFunction::Scene || function == ObjectFunction::Fault) {
            return 0;
        }
        return static_cast<std::uint16_t>(GR_KoBlockOffset + GR_KoBlockSize * address.as_group()->value() +
                                          group_offset(function));
    case dali::AddressKind::Broadcast:
        switch (function) {
        case ObjectFunction::Switch:
            return CH_KoBcSwitch;
        case ObjectFunction::Brightness:
            return CH_KoBcBrightness;
        case ObjectFunction::RelativeDimming:
            return CH_KoBcRelativeDimming;
        case ObjectFunction::Scene:
            return CH_KoBcScene;
        case ObjectFunction::ColourTemperature:
            return CH_KoBcColourTemperature;
        case ObjectFunction::RelativeColourTemperature:
            return CH_KoBcRelativeColourTemperature;
        case ObjectFunction::Rgb:
            return CH_KoBcRgb;
        case ObjectFunction::Rgbw:
            return CH_KoBcRgbw;
        case ObjectFunction::XyY:
            return CH_KoBcXyY;
        default:
            // Broadcast has no status or fault objects.
            return 0;
        }
    }
    return 0;
}

std::optional<ObjectTarget> object_target(std::uint16_t number) noexcept {
    if (number >= CH_KoBlockOffset && number < channel_objects_end) {
        const auto index = static_cast<unsigned>(number - CH_KoBlockOffset);
        const auto function = channel_function(index % CH_KoBlockSize);
        const auto address = dali::ShortAddress::from(index / CH_KoBlockSize);
        if (function && address) {
            return ObjectTarget{dali::Address::short_address(*address), *function};
        }
        return std::nullopt;
    }
    if (number >= GR_KoBlockOffset && number < group_objects_end) {
        const auto index = static_cast<unsigned>(number - GR_KoBlockOffset);
        const auto function = group_function(index % GR_KoBlockSize);
        const auto group = dali::Group::from(index / GR_KoBlockSize);
        if (function && group) {
            return ObjectTarget{dali::Address::group(*group), *function};
        }
        return std::nullopt;
    }
    switch (number) {
    case CH_KoBcSwitch:
        return ObjectTarget{dali::Address::broadcast(), ObjectFunction::Switch};
    case CH_KoBcBrightness:
        return ObjectTarget{dali::Address::broadcast(), ObjectFunction::Brightness};
    case CH_KoBcRelativeDimming:
        return ObjectTarget{dali::Address::broadcast(), ObjectFunction::RelativeDimming};
    case CH_KoBcScene:
        return ObjectTarget{dali::Address::broadcast(), ObjectFunction::Scene};
    case CH_KoBcColourTemperature:
        return ObjectTarget{dali::Address::broadcast(), ObjectFunction::ColourTemperature};
    case CH_KoBcRelativeColourTemperature:
        return ObjectTarget{dali::Address::broadcast(), ObjectFunction::RelativeColourTemperature};
    case CH_KoBcRgb:
        return ObjectTarget{dali::Address::broadcast(), ObjectFunction::Rgb};
    case CH_KoBcRgbw:
        return ObjectTarget{dali::Address::broadcast(), ObjectFunction::Rgbw};
    case CH_KoBcXyY:
        return ObjectTarget{dali::Address::broadcast(), ObjectFunction::XyY};
    default:
        return std::nullopt;
    }
}

std::uint16_t input_object_number(std::uint8_t channel, InputObjectFunction function) noexcept {
    if (channel >= input_channel_count) {
        return 0;
    }
    return static_cast<std::uint16_t>(IN_KoBlockOffset + input_object_stride * channel +
                                      input_offset(function));
}

std::optional<InputObjectTarget> input_object_target(std::uint16_t number) noexcept {
    if (number < IN_KoBlockOffset || number >= input_objects_end) {
        return std::nullopt;
    }
    const auto index = static_cast<unsigned>(number - IN_KoBlockOffset);
    const auto function = input_function(index % input_object_stride);
    if (!function) {
        return std::nullopt;
    }
    return InputObjectTarget{static_cast<std::uint8_t>(index / input_object_stride), *function};
}

}
