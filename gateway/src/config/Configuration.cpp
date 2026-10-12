#include "config/Configuration.hpp"

#include <cstddef>
#include <cstdint>

#include "dali/Group.hpp"
#include "dali/ShortAddress.hpp"
#include "knx/BrightnessScale.hpp"
#include "knx/generated/KnxDaliGateway.h"

namespace gateway::config {

// Layout contract with knxprod/src/*.xml; a regenerated header that violates
// these fails the build instead of silently shifting the parser.
static_assert(CH_ParamBlockOffset == 4, "channel parameters start after the shared parameters");
static_assert(CH_ParamBlockSize == 5, "channel parameter block must stay 5 bytes");
static_assert(CH_fSwitchOnMode == 0 && CH_fFixedLevel == 1 && CH_fGroup01 == 2,
              "channel parameter layout changed");
static_assert(CH_fGroup16 == CH_fGroup01 + 1, "group mask must occupy two consecutive bytes");
static_assert(CH_fGroup01Mask == 0x01 && CH_fGroup16Mask == 0x80,
              "group mask byte order must be little-endian, bit g = DALI group g");
static_assert(GR_ParamBlockOffset == CH_ParamBlockOffset + CH_ChannelCount * CH_ParamBlockSize,
              "group parameters follow the channel blocks");
static_assert(GR_ParamBlockSize == 3, "group parameter block must stay 3 bytes");
static_assert(GR_fSwitchOnMode == 0 && GR_fFixedLevel == 1, "group parameter layout changed");
// CH_fColourControl/GR_fColourControl only steer ETS object visibility; the
// firmware derives colour behaviour from the probed gear capabilities.
static_assert(CH_BcSwitchOnMode == 1 && CH_BcFixedLevel == 2, "broadcast parameter layout changed");
static_assert(IN_ParamBlockOffset == GR_ParamBlockOffset + GR_ChannelCount * GR_ParamBlockSize,
              "input parameters must follow the group blocks");
static_assert(IN_ChannelCount == input_count, "knxprod must define 32 input channels");
static_assert(IN_ParamBlockSize == 6, "input parameter block must stay 6 bytes");
static_assert(IN_fDeviceAddress == 0 && IN_fInstance == 1 && IN_fInputType == 2 && IN_fFunction == 3 &&
                  IN_fScene == 4 && IN_fThreshold == 5,
              "input parameter layout changed");
static_assert(MAIN_ParameterSize == IN_ParamBlockOffset + IN_ChannelCount * IN_ParamBlockSize,
              "parameter memory must end after the input blocks");

namespace {

SwitchOn parse_switch_on(std::uint8_t mode, std::uint8_t percent) {
    // Undefined enum values and out-of-range levels fall back to the default
    // of that field independently.
    SwitchOn result{};
    if (mode <= static_cast<std::uint8_t>(SwitchOnPolicy::FixedLevel)) {
        result.policy = static_cast<SwitchOnPolicy>(mode);
    }
    if (percent >= 1 && percent <= 100) {
        const auto raw = static_cast<std::uint8_t>((static_cast<unsigned>(percent) * 255 + 50) / 100);
        result.fixed = knx::arc_from_scaling(raw);
    }
    return result;
}

}

Configuration default_configuration() noexcept {
    return Configuration{};
}

Configuration parse_configuration(const std::uint8_t* parameters, std::size_t size) noexcept {
    Configuration config = default_configuration();
    if (parameters == nullptr || size < MAIN_ParameterSize) {
        return config;
    }
    for (unsigned channel = 0; channel < dali::ShortAddress::capacity; ++channel) {
        const auto block = parameters + CH_ParamBlockOffset + std::size_t{channel} * CH_ParamBlockSize;
        config.channels[channel] = parse_switch_on(block[CH_fSwitchOnMode], block[CH_fFixedLevel]);
        config.group_masks[channel] =
            static_cast<std::uint16_t>(block[CH_fGroup01] | (block[CH_fGroup16] << 8));
    }
    for (unsigned group = 0; group < dali::Group::capacity; ++group) {
        const auto block = parameters + GR_ParamBlockOffset + std::size_t{group} * GR_ParamBlockSize;
        config.groups[group] = parse_switch_on(block[GR_fSwitchOnMode], block[GR_fFixedLevel]);
    }
    config.broadcast = parse_switch_on(parameters[CH_BcSwitchOnMode], parameters[CH_BcFixedLevel]);
    for (unsigned index = 0; index < IN_ChannelCount; ++index) {
        const auto block = parameters + IN_ParamBlockOffset + std::size_t{index} * IN_ParamBlockSize;
        auto& input = config.inputs[index];
        if (block[IN_fDeviceAddress] <= 63) {
            input.device = block[IN_fDeviceAddress];
        }
        if (block[IN_fInstance] <= 31) {
            input.instance = block[IN_fInstance];
        }
        if (block[IN_fInputType] <= static_cast<std::uint8_t>(InputType::Light)) {
            input.type = static_cast<InputType>(block[IN_fInputType]);
        }
        if (block[IN_fFunction] <= static_cast<std::uint8_t>(InputFunction::Scene)) {
            input.function = static_cast<InputFunction>(block[IN_fFunction]);
        }
        if (block[IN_fScene] >= 1 && block[IN_fScene] <= 64) {
            input.scene = block[IN_fScene];
        }
        if (block[IN_fThreshold] <= 100) {
            input.threshold = block[IN_fThreshold];
        }
    }
    return config;
}

}
