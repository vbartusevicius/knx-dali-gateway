#include "config/Configuration.hpp"

#include "knx/BrightnessScale.hpp"
#include "knx/generated/KnxDaliGateway.h"

namespace gateway::config {

// Layout contract with knxprod/src/*.xml; a regenerated header that violates
// these fails the build instead of silently shifting the parser.
static_assert(CH_ParamBlockOffset == 3, "channel parameters start after the shared parameters");
static_assert(CH_ParamBlockSize == 4, "channel parameter block must stay 4 bytes");
static_assert(CH_fSwitchOnMode == 0 && CH_fFixedLevel == 1 && CH_fGroup01 == 2,
              "channel parameter layout changed");
static_assert(CH_fGroup16 == CH_fGroup01 + 1, "group mask must occupy two consecutive bytes");
static_assert(CH_fGroup01Mask == 0x01 && CH_fGroup16Mask == 0x80,
              "group mask byte order must be little-endian, bit g = DALI group g");
static_assert(GR_ParamBlockOffset == CH_ParamBlockOffset + CH_ChannelCount * CH_ParamBlockSize,
              "group parameters follow the channel blocks");
static_assert(GR_ParamBlockSize == 2, "group parameter block must stay 2 bytes");
static_assert(GR_fSwitchOnMode == 0 && GR_fFixedLevel == 1, "group parameter layout changed");
static_assert(CH_BcSwitchOnMode == 1 && CH_BcFixedLevel == 2, "broadcast parameter layout changed");
static_assert(MAIN_ParameterSize == GR_ParamBlockOffset + GR_ChannelCount * GR_ParamBlockSize,
              "parameter memory must end after the group blocks");

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
        const auto block = parameters + CH_ParamBlockOffset + channel * CH_ParamBlockSize;
        config.channels[channel] = parse_switch_on(block[CH_fSwitchOnMode], block[CH_fFixedLevel]);
        config.group_masks[channel] =
            static_cast<std::uint16_t>(block[CH_fGroup01] | (block[CH_fGroup16] << 8));
    }
    for (unsigned group = 0; group < dali::Group::capacity; ++group) {
        const auto block = parameters + GR_ParamBlockOffset + group * GR_ParamBlockSize;
        config.groups[group] = parse_switch_on(block[GR_fSwitchOnMode], block[GR_fFixedLevel]);
    }
    config.broadcast = parse_switch_on(parameters[CH_BcSwitchOnMode], parameters[CH_BcFixedLevel]);
    return config;
}

}
