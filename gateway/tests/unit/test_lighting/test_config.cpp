#include <array>
#include <type_traits>

#include <unity.h>

#include "config/Configuration.hpp"
#include "knx/BrightnessScale.hpp"
#include "knx/generated/KnxDaliGateway.h"

namespace {

using gateway::config::Configuration;
using gateway::config::SwitchOnPolicy;
using gateway::dali::ArcLevel;

static_assert(std::is_trivially_copyable_v<Configuration>);

std::array<std::uint8_t, MAIN_ParameterSize> parameters() {
    std::array<std::uint8_t, MAIN_ParameterSize> buffer{};
    // Valid defaults: mode 0 (last level), 100 %, no memberships.
    buffer[CH_BcSwitchOnMode] = 0;
    buffer[CH_BcFixedLevel] = 100;
    for (unsigned channel = 0; channel < CH_ChannelCount; ++channel) {
        const auto offset = CH_ParamBlockOffset + channel * CH_ParamBlockSize;
        buffer[offset + CH_fSwitchOnMode] = 0;
        buffer[offset + CH_fFixedLevel] = 100;
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
        TEST_ASSERT_EQUAL(static_cast<int>(gateway::config::InputType::Unused),
                          static_cast<int>(input.type));
        TEST_ASSERT_EQUAL(static_cast<int>(gateway::config::InputFunction::Toggle),
                          static_cast<int>(input.function));
        TEST_ASSERT_EQUAL_UINT8(1, input.scene);
        TEST_ASSERT_EQUAL_UINT8(0, input.threshold);
    }
}

void test_parse_input_channel_block() {
    auto buffer = parameters();
    const auto offset = IN_ParamBlockOffset + 2 * IN_ParamBlockSize;
    buffer[offset + IN_fDeviceAddress] = 5;
    buffer[offset + IN_fInstance] = 1;
    buffer[offset + IN_fInputType] = 1;   // push button
    buffer[offset + IN_fFunction] = 3;    // toggle + dimming
    buffer[offset + IN_fScene] = 20;
    buffer[offset + IN_fThreshold] = 10;
    const auto config = gateway::config::parse_configuration(buffer.data(), buffer.size());
    const auto& input = config.inputs[2];
    TEST_ASSERT_EQUAL_UINT8(5, input.device);
    TEST_ASSERT_EQUAL_UINT8(1, input.instance);
    TEST_ASSERT_EQUAL(static_cast<int>(gateway::config::InputType::PushButton),
                      static_cast<int>(input.type));
    TEST_ASSERT_EQUAL(static_cast<int>(gateway::config::InputFunction::ToggleDim),
                      static_cast<int>(input.function));
    TEST_ASSERT_EQUAL_UINT8(20, input.scene);
    TEST_ASSERT_EQUAL_UINT8(10, input.threshold);
    TEST_ASSERT_EQUAL(static_cast<int>(gateway::config::InputType::Unused),
                      static_cast<int>(config.inputs[1].type));
}

void test_invalid_input_values_fall_back_to_defaults() {
    auto buffer = parameters();
    const auto offset = IN_ParamBlockOffset;
    buffer[offset + IN_fDeviceAddress] = 64;
    buffer[offset + IN_fInstance] = 32;
    buffer[offset + IN_fInputType] = 4;
    buffer[offset + IN_fFunction] = 5;
    buffer[offset + IN_fScene] = 65;
    buffer[offset + IN_fThreshold] = 101;
    const auto config = gateway::config::parse_configuration(buffer.data(), buffer.size());
    const auto& input = config.inputs[0];
    TEST_ASSERT_EQUAL_UINT8(0, input.device);
    TEST_ASSERT_EQUAL_UINT8(0, input.instance);
    TEST_ASSERT_EQUAL(static_cast<int>(gateway::config::InputType::Unused),
                      static_cast<int>(input.type));
    TEST_ASSERT_EQUAL(static_cast<int>(gateway::config::InputFunction::Toggle),
                      static_cast<int>(input.function));
    TEST_ASSERT_EQUAL_UINT8(1, input.scene);
    TEST_ASSERT_EQUAL_UINT8(0, input.threshold);
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

}

void run_config_tests() {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_default_configuration_is_last_level_full_output);
    RUN_TEST(test_parse_null_or_short_memory_returns_defaults);
    RUN_TEST(test_parse_channel_policy_fixed_level_and_percent);
    RUN_TEST(test_parse_group_mask_little_endian_two_bytes);
    RUN_TEST(test_parse_group_and_broadcast_blocks);
    RUN_TEST(test_invalid_enum_falls_back_to_last_level);
    RUN_TEST(test_default_input_channels_are_unused);
    RUN_TEST(test_parse_input_channel_block);
    RUN_TEST(test_invalid_input_values_fall_back_to_defaults);
    RUN_TEST(test_out_of_range_percent_falls_back_to_default_level);
}
