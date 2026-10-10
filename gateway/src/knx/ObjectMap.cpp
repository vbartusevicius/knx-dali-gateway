#include "knx/ObjectMap.hpp"

namespace gateway::knx {

static_assert(CH_ChannelCount == 64, "knxprod must define 64 channels");
static_assert(CH_KoBlockSize == 5, "knxprod must define 5 group objects per channel");
static_assert(CH_KoBlockOffset == 1, "channel group objects are 1-based");
static_assert(GR_ChannelCount == 16, "knxprod must define 16 DALI groups");
static_assert(GR_KoBlockSize == 5, "knxprod must define 5 group objects per group");
static_assert(GR_KoBlockOffset == CH_KoBlockOffset + CH_ChannelCount * CH_KoBlockSize,
              "group objects must follow the channel objects");
static_assert(CH_KoSwitch == 0, "knxprod object 0 must be Switch");
static_assert(CH_KoSwitchStatus == 1, "knxprod object 1 must be Switch status");
static_assert(CH_KoBrightness == 2, "knxprod object 2 must be Brightness");
static_assert(CH_KoBrightnessStatus == 3, "knxprod object 3 must be Brightness status");
static_assert(CH_KoRelativeDimming == 4, "knxprod object 4 must be Relative dimming");
static_assert(GR_KoSwitch == CH_KoSwitch && GR_KoSwitchStatus == CH_KoSwitchStatus &&
                  GR_KoBrightness == CH_KoBrightness && GR_KoBrightnessStatus == CH_KoBrightnessStatus &&
                  GR_KoRelativeDimming == CH_KoRelativeDimming,
              "channel and group objects must share the function order");
static_assert(CH_KoBcSwitch == GR_KoBlockOffset + GR_ChannelCount * GR_KoBlockSize,
              "broadcast objects must follow the group objects");
static_assert(CH_KoBcSwitch + 1 == CH_KoBcBrightness && CH_KoBcSwitch + 2 == CH_KoBcRelativeDimming &&
                  CH_KoBcSwitch + 3 == CH_KoBcScene,
              "broadcast object order must be Switch, Brightness, Relative dimming, Scene");
static_assert(object_count == CH_KoBcScene, "broadcast scene must be the last group object");

namespace {

constexpr std::uint16_t block_offset(ObjectFunction function) noexcept {
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
        case ObjectFunction::Scene:
            break;
    }
    return 0;
}

std::optional<ObjectFunction> function_at(unsigned index) noexcept {
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
    }
    return std::nullopt;
}

constexpr std::uint16_t channel_objects_end = CH_KoBlockOffset + CH_ChannelCount * CH_KoBlockSize;
constexpr std::uint16_t group_objects_end = GR_KoBlockOffset + GR_ChannelCount * GR_KoBlockSize;

}

std::uint16_t object_number(dali::Address address, ObjectFunction function) noexcept {
    switch (address.kind()) {
        case dali::AddressKind::Short:
            if (function == ObjectFunction::Scene) {
                return 0;
            }
            return static_cast<std::uint16_t>(CH_KoBlockOffset + CH_KoBlockSize * address.as_short()->value() +
                                              block_offset(function));
        case dali::AddressKind::Group:
            if (function == ObjectFunction::Scene) {
                return 0;
            }
            return static_cast<std::uint16_t>(GR_KoBlockOffset + GR_KoBlockSize * address.as_group()->value() +
                                              block_offset(function));
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
                default:
                    return 0;
            }
    }
    return 0;
}

std::optional<ObjectTarget> object_target(std::uint16_t number) noexcept {
    if (number >= CH_KoBlockOffset && number < channel_objects_end) {
        const auto index = static_cast<unsigned>(number - CH_KoBlockOffset);
        const auto function = function_at(index % CH_KoBlockSize);
        const auto address = dali::ShortAddress::from(index / CH_KoBlockSize);
        if (function && address) {
            return ObjectTarget{dali::Address::short_address(*address), *function};
        }
        return std::nullopt;
    }
    if (number >= GR_KoBlockOffset && number < group_objects_end) {
        const auto index = static_cast<unsigned>(number - GR_KoBlockOffset);
        const auto function = function_at(index % GR_KoBlockSize);
        const auto group = dali::Group::from(index / GR_KoBlockSize);
        if (function && group) {
            return ObjectTarget{dali::Address::group(*group), *function};
        }
        return std::nullopt;
    }
    if (number == CH_KoBcSwitch) {
        return ObjectTarget{dali::Address::broadcast(), ObjectFunction::Switch};
    }
    if (number == CH_KoBcBrightness) {
        return ObjectTarget{dali::Address::broadcast(), ObjectFunction::Brightness};
    }
    if (number == CH_KoBcRelativeDimming) {
        return ObjectTarget{dali::Address::broadcast(), ObjectFunction::RelativeDimming};
    }
    if (number == CH_KoBcScene) {
        return ObjectTarget{dali::Address::broadcast(), ObjectFunction::Scene};
    }
    return std::nullopt;
}

}
