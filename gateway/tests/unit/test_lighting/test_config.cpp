#include <array>
#include <type_traits>

#include <unity.h>

#include "config/Configuration.hpp"
#include "knx/BrightnessScale.hpp"
#include "knx/generated/KnxDaliGateway.h"

namespace {

using gateway::config::Configuration;
using gateway::config::SwitchOnPolicy;

static_assert(std::is_trivially_copyable_v<Configuration>);

std::array<std::uint8_t, MAIN_ParameterSize> parameters() {
    std::array<std::uint8_t, MAIN_ParameterSize> buffer{};
    // Valid defaults: mode 0 (last level), 100 %, no memberships, internal PSU enabled.
    buffer[CH_BcSwitchOnMode] = 0;
    buffer[CH_BcFixedLevel] = 100;
    buffer[CH_InternalDaliPsu] = 1;
    for (unsigned channel = 0; channel < CH_ChannelCount; ++channel) {
        const auto offset = CH_ParamBlockOffset + channel * CH_ParamBlockSize;
        buffer[offset + CH_fSwitchOnMode] = 0;
        buffer[offset + CH_fFixedLevel] = 100;
        buffer[offset + CH_fMinLevel] = 1;
        buffer[offset + CH_fMaxLevel] = 100;
        buffer[offset + CH_fPowerOnLevel] = 100;
        buffer[offset + CH_fSysFailLevel] = 100;
        buffer[offset + CH_fFadeTime] = 255;
        buffer[offset + CH_fFadeRate] = 255;
    }
    for (unsigned group = 0; group < GR_ChannelCount; ++group) {
        const auto offset = GR_ParamBlockOffset + group * GR_ParamBlockSize;
        buffer[offset + GR_fSwitchOnMode] = 0;
        buffer[offset + GR_fFixedLevel] = 100;
    }
    return buffer;
}

void test_default_configuration_is_last_level_full_output() {
    const auto config = gateway::config::default_configuration();
    for (const auto& channel : config.channels) {
        TEST_ASSERT_EQUAL(static_cast<int>(SwitchOnPolicy::LastLevel), static_cast<int>(channel.policy));
        TEST_ASSERT_EQUAL_UINT8(254, channel.fixed.value());
    }
    for (const auto mask : config.group_masks) {
        TEST_ASSERT_EQUAL_UINT16(0, mask);
    }
    for (const auto& group : config.groups) {
        TEST_ASSERT_EQUAL(static_cast<int>(SwitchOnPolicy::LastLevel), static_cast<int>(group.policy));
    }
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchOnPolicy::LastLevel), static_cast<int>(config.broadcast.policy));
}

void test_parse_null_or_short_memory_returns_defaults() {
    const auto buffer = parameters();
    const auto empty = gateway::config::parse_configuration(nullptr, buffer.size());
    TEST_ASSERT_EQUAL_UINT16(0, empty.group_masks[0]);
    const auto truncated = gateway::config::parse_configuration(buffer.data(), MAIN_ParameterSize - 1);
    TEST_ASSERT_EQUAL_UINT16(0, truncated.group_masks[0]);
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchOnPolicy::LastLevel),
                      static_cast<int>(truncated.channels[0].policy));
}

void test_parse_channel_policy_fixed_level_and_percent() {
    auto buffer = parameters();
    const auto offset = CH_ParamBlockOffset + 5 * CH_ParamBlockSize;
    buffer[offset + CH_fSwitchOnMode] = 1;
    buffer[offset + CH_fFixedLevel] = 50;
    const auto config = gateway::config::parse_configuration(buffer.data(), buffer.size());
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchOnPolicy::FixedLevel),
                      static_cast<int>(config.channels[5].policy));
    // 50 % -> raw 128 on the KNX scaling -> DALI arc for that scaling.
    const auto expected = gateway::knx::arc_from_scaling(128);
    TEST_ASSERT_EQUAL_UINT8(expected.value(), config.channels[5].fixed.value());
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchOnPolicy::LastLevel),
                      static_cast<int>(config.channels[4].policy));
}

void test_parse_group_mask_little_endian_two_bytes() {
    auto buffer = parameters();
    const auto offset = CH_ParamBlockOffset;  // channel 0
    buffer[offset + CH_fGroup01] = 0x21;      // groups 0 and 5
    buffer[offset + CH_fGroup16] = 0x80;      // group 15
    const auto config = gateway::config::parse_configuration(buffer.data(), buffer.size());
    TEST_ASSERT_EQUAL_UINT16(0x8021, config.group_masks[0]);
    TEST_ASSERT_EQUAL_UINT16(0, config.group_masks[1]);
}

void test_parse_group_and_broadcast_blocks() {
    auto buffer = parameters();
    const auto group_offset = GR_ParamBlockOffset + 3 * GR_ParamBlockSize;
    buffer[group_offset + GR_fSwitchOnMode] = 1;
    buffer[group_offset + GR_fFixedLevel] = 25;
    buffer[CH_BcSwitchOnMode] = 1;
    buffer[CH_BcFixedLevel] = 10;
    const auto config = gateway::config::parse_configuration(buffer.data(), buffer.size());
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchOnPolicy::FixedLevel),
                      static_cast<int>(config.groups[3].policy));
    TEST_ASSERT_EQUAL_UINT8(gateway::knx::arc_from_scaling(64).value(), config.groups[3].fixed.value());
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchOnPolicy::FixedLevel),
                      static_cast<int>(config.broadcast.policy));
    TEST_ASSERT_EQUAL_UINT8(gateway::knx::arc_from_scaling(26).value(), config.broadcast.fixed.value());
}

void test_invalid_enum_falls_back_to_last_level() {
    auto buffer = parameters();
    const auto offset = CH_ParamBlockOffset + 1 * CH_ParamBlockSize;
    buffer[offset + CH_fSwitchOnMode] = 0x7F;
    buffer[offset + CH_fFixedLevel] = 50;
    const auto config = gateway::config::parse_configuration(buffer.data(), buffer.size());
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchOnPolicy::LastLevel),
                      static_cast<int>(config.channels[1].policy));
    TEST_ASSERT_EQUAL_UINT8(gateway::knx::arc_from_scaling(128).value(), config.channels[1].fixed.value());
}

void test_default_input_channels_are_unused() {
    const auto config = gateway::config::default_configuration();
    for (const auto& input : config.inputs) {
        TEST_ASSERT_EQUAL(static_cast<int>(gateway::config::InputType::Unused), static_cast<int>(input.type));
        TEST_ASSERT_EQUAL(static_cast<int>(gateway::config::InputFunction::Toggle),
                          static_cast<int>(input.function));
        TEST_ASSERT_EQUAL_UINT8(1, input.scene);
        TEST_ASSERT_EQUAL_UINT8(0, input.threshold);
    }
}

std::size_t input_slot(std::size_t slot) {
    return IN_ParamBlockOffset + slot * IN_ParamBlockSize;
}

// field is the instance-0 offset (IN_fI0Type ...); instance i is i strides further.
std::size_t input_field(std::size_t slot, std::size_t instance, std::size_t field) {
    return input_slot(slot) + field + instance * (IN_fI1Type - IN_fI0Type);
}

void test_parse_input_device_slot() {
    auto buffer = parameters();
    buffer[input_slot(1) + IN_fDeviceAddress] = 5;
    buffer[input_slot(1) + IN_fFirstInstance] = 1;
    buffer[input_slot(1) + IN_fInstanceCount] = 2;
    buffer[input_field(1, 1, IN_fI0Type)] = 1;      // push button
    buffer[input_field(1, 1, IN_fI0Function)] = 3;  // toggle + dimming
    buffer[input_field(1, 1, IN_fI0Scene)] = 20;
    buffer[input_field(1, 1, IN_fI0Threshold)] = 10;
    buffer[input_field(1, 0, IN_fI0Type)] = 2;  // occupancy
    const auto config = gateway::config::parse_configuration(buffer.data(), buffer.size());
    // Slot 1 instance i is input channel 4 + i; its instance number is first + i.
    const auto& occupancy = config.inputs[4];
    TEST_ASSERT_EQUAL_UINT8(5, occupancy.device);
    TEST_ASSERT_EQUAL_UINT8(1, occupancy.instance);
    TEST_ASSERT_EQUAL(static_cast<int>(gateway::config::InputType::Occupancy),
                      static_cast<int>(occupancy.type));
    const auto& button = config.inputs[5];
    TEST_ASSERT_EQUAL_UINT8(5, button.device);
    TEST_ASSERT_EQUAL_UINT8(2, button.instance);
    TEST_ASSERT_EQUAL(static_cast<int>(gateway::config::InputType::PushButton),
                      static_cast<int>(button.type));
    TEST_ASSERT_EQUAL(static_cast<int>(gateway::config::InputFunction::ToggleDim),
                      static_cast<int>(button.function));
    TEST_ASSERT_EQUAL_UINT8(20, button.scene);
    TEST_ASSERT_EQUAL_UINT8(10, button.threshold);
    for (const auto channel : {0, 3, 6, 7, 8}) {
        TEST_ASSERT_EQUAL(static_cast<int>(gateway::config::InputType::Unused),
                          static_cast<int>(config.inputs[channel].type));
    }
}

void test_instances_beyond_the_slot_count_stay_unused() {
    auto buffer = parameters();
    buffer[input_slot(0) + IN_fDeviceAddress] = 7;
    buffer[input_slot(0) + IN_fInstanceCount] = 1;
    for (std::size_t i = 0; i < 4; ++i) {
        buffer[input_field(0, i, IN_fI0Type)] = 1;  // configured, but only instance 0 is counted
    }
    const auto config = gateway::config::parse_configuration(buffer.data(), buffer.size());
    TEST_ASSERT_EQUAL(static_cast<int>(gateway::config::InputType::PushButton),
                      static_cast<int>(config.inputs[0].type));
    for (const auto channel : {1, 2, 3}) {
        TEST_ASSERT_EQUAL(static_cast<int>(gateway::config::InputType::Unused),
                          static_cast<int>(config.inputs[channel].type));
    }
}

void test_invalid_input_values_fall_back_to_defaults() {
    auto buffer = parameters();
    buffer[input_slot(0) + IN_fDeviceAddress] = 3;
    buffer[input_slot(0) + IN_fInstanceCount] = 1;
    buffer[input_field(0, 0, IN_fI0Type)] = 4;
    buffer[input_field(0, 0, IN_fI0Function)] = 5;
    buffer[input_field(0, 0, IN_fI0Scene)] = 65;
    buffer[input_field(0, 0, IN_fI0Threshold)] = 101;
    buffer[input_slot(1) + IN_fDeviceAddress] = 64;  // invalid device: whole slot ignored
    buffer[input_slot(1) + IN_fInstanceCount] = 1;
    buffer[input_field(1, 0, IN_fI0Type)] = 1;
    buffer[input_slot(2) + IN_fDeviceAddress] = 4;  // invalid count: whole slot ignored
    buffer[input_slot(2) + IN_fInstanceCount] = 5;
    buffer[input_field(2, 0, IN_fI0Type)] = 1;
    buffer[input_slot(3) + IN_fDeviceAddress] = 6;  // instances past 31 are dropped
    buffer[input_slot(3) + IN_fFirstInstance] = 30;
    buffer[input_slot(3) + IN_fInstanceCount] = 4;
    for (std::size_t i = 0; i < 4; ++i) {
        buffer[input_field(3, i, IN_fI0Type)] = 1;
    }
    const auto config = gateway::config::parse_configuration(buffer.data(), buffer.size());
    const auto& input = config.inputs[0];
    TEST_ASSERT_EQUAL_UINT8(3, input.device);
    TEST_ASSERT_EQUAL(static_cast<int>(gateway::config::InputType::Unused), static_cast<int>(input.type));
    TEST_ASSERT_EQUAL(static_cast<int>(gateway::config::InputFunction::Toggle),
                      static_cast<int>(input.function));
    TEST_ASSERT_EQUAL_UINT8(1, input.scene);
    TEST_ASSERT_EQUAL_UINT8(0, input.threshold);
    TEST_ASSERT_EQUAL(static_cast<int>(gateway::config::InputType::Unused),
                      static_cast<int>(config.inputs[4].type));
    TEST_ASSERT_EQUAL(static_cast<int>(gateway::config::InputType::Unused),
                      static_cast<int>(config.inputs[8].type));
    TEST_ASSERT_EQUAL_UINT8(31, config.inputs[13].instance);
    TEST_ASSERT_EQUAL(static_cast<int>(gateway::config::InputType::PushButton),
                      static_cast<int>(config.inputs[13].type));
    TEST_ASSERT_EQUAL(static_cast<int>(gateway::config::InputType::Unused),
                      static_cast<int>(config.inputs[14].type));
}

void test_out_of_range_percent_falls_back_to_default_level() {
    auto buffer = parameters();
    const auto offset = CH_ParamBlockOffset + 2 * CH_ParamBlockSize;
    buffer[offset + CH_fSwitchOnMode] = 1;
    buffer[offset + CH_fFixedLevel] = 0;
    const auto config = gateway::config::parse_configuration(buffer.data(), buffer.size());
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchOnPolicy::FixedLevel),
                      static_cast<int>(config.channels[2].policy));
    TEST_ASSERT_EQUAL_UINT8(254, config.channels[2].fixed.value());
}

void test_parse_dt6_ballast_limits_fade_and_psu_mode() {
    auto buffer = parameters();
    buffer[CH_InternalDaliPsu] = 0;  // external DALI PSU
    const auto offset = CH_ParamBlockOffset + 3 * CH_ParamBlockSize;
    buffer[offset + CH_fMinLevel] = 10;
    buffer[offset + CH_fMaxLevel] = 80;
    buffer[offset + CH_fPowerOnLevel] = 101;  // 101 = MASK (last level)
    buffer[offset + CH_fSysFailLevel] = 0;    // 0 = Off
    buffer[offset + CH_fFadeTime] = 4;        // 2.0 s
    buffer[offset + CH_fFadeRate] = 7;        // 44.7 steps/s
    const auto config = gateway::config::parse_configuration(buffer.data(), buffer.size());
    TEST_ASSERT_FALSE(config.internal_dali_psu_enabled);
    TEST_ASSERT_EQUAL_UINT8(gateway::knx::arc_from_scaling(26).value(), config.ballasts[3].min_level);
    TEST_ASSERT_EQUAL_UINT8(gateway::knx::arc_from_scaling(204).value(), config.ballasts[3].max_level);
    TEST_ASSERT_EQUAL_UINT8(0xFF, config.ballasts[3].power_on_level);
    TEST_ASSERT_EQUAL_UINT8(0, config.ballasts[3].system_failure_level);
    TEST_ASSERT_EQUAL_UINT8(4, config.ballasts[3].fade_time);
    TEST_ASSERT_EQUAL_UINT8(7, config.ballasts[3].fade_rate);
}

}  // namespace

void run_config_tests() {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_default_configuration_is_last_level_full_output);
    RUN_TEST(test_parse_null_or_short_memory_returns_defaults);
    RUN_TEST(test_parse_channel_policy_fixed_level_and_percent);
    RUN_TEST(test_parse_group_mask_little_endian_two_bytes);
    RUN_TEST(test_parse_group_and_broadcast_blocks);
    RUN_TEST(test_invalid_enum_falls_back_to_last_level);
    RUN_TEST(test_default_input_channels_are_unused);
    RUN_TEST(test_parse_input_device_slot);
    RUN_TEST(test_instances_beyond_the_slot_count_stay_unused);
    RUN_TEST(test_invalid_input_values_fall_back_to_defaults);
    RUN_TEST(test_out_of_range_percent_falls_back_to_default_level);
    RUN_TEST(test_parse_dt6_ballast_limits_fade_and_psu_mode);
}
