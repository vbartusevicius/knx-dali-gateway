// Copyright (c) 2025 the knx-dali-gateway contributors
#include <array>
#include <map>
#include <optional>
#include <vector>

#include <unity.h>

#include "dali/Transport.hpp"
#include "dali/devices/DeviceFrames.hpp"
#include "dali/devices/DeviceInventory.hpp"
#include "inputs/InputMapper.hpp"
#include "knx/ObjectMap.hpp"

namespace {

using gateway::config::InputChannel;
using gateway::config::InputFunction;
using gateway::config::InputType;
using gateway::dali::ByteRead;
using gateway::dali::TransportStatus;
using gateway::inputs::Action;
using gateway::inputs::InputMapper;
using gateway::knx::InputObjectFunction;
namespace device = gateway::dali::device;

constexpr std::uint16_t object(std::uint8_t channel, InputObjectFunction function) {
    return gateway::knx::input_object_number(channel, function);
}

gateway::config::Configuration one_channel(std::uint8_t device, std::uint8_t instance, InputType type,
                                           InputFunction function = InputFunction::Toggle,
                                           std::uint8_t scene = 1, std::uint8_t threshold = 0) {
    gateway::config::Configuration config{};
    config.inputs[0] = InputChannel{device, instance, type, function, scene, threshold};
    return config;
}

struct Mapped {
    std::array<Action, InputMapper::max_actions> actions{};
    std::uint8_t count = 0;
};

Mapped map(InputMapper& mapper, std::uint8_t device, std::uint8_t instance, std::uint16_t info,
           std::optional<std::uint8_t> reported_type = std::nullopt) {
    Mapped result;
    result.count = mapper.map({device, instance, info}, reported_type, result.actions);
    return result;
}

// Inverse of the mapper's DPT 9.004 encoding for assertions:
// value = 0.01 * mantissa * 2^exponent.
float decode_dpt9(const Action& action) {
    const auto raw = static_cast<std::uint16_t>((action.data[0] << 8) | action.data[1]);
    const int exponent = (raw >> 11) & 0x0F;
    int mantissa = raw & 0x7FF;
    if (raw & 0x8000) {
        mantissa -= 2048;
    }
    return 0.01F * static_cast<float>(mantissa) * static_cast<float>(1 << exponent);
}

void test_toggle_short_press_alternates() {
    InputMapper mapper;
    mapper.configure(one_channel(5, 0, InputType::PushButton));

    const auto first = map(mapper, 5, 0, 2, device::type::push_button);
    TEST_ASSERT_EQUAL_UINT8(1, first.count);
    TEST_ASSERT_EQUAL_UINT16(object(0, InputObjectFunction::Switch), first.actions[0].object);
    TEST_ASSERT_EQUAL_UINT8(1, first.actions[0].length);
    TEST_ASSERT_EQUAL_UINT8(1, first.actions[0].data[0]);

    const auto second = map(mapper, 5, 0, 2, device::type::push_button);
    TEST_ASSERT_EQUAL_UINT8(1, second.count);
    TEST_ASSERT_EQUAL_UINT8(0, second.actions[0].data[0]);

    const auto third = map(mapper, 5, 0, 2);
    TEST_ASSERT_EQUAL_UINT8(1, third.actions[0].data[0]);
}

void test_switch_feedback_flips_next_toggle() {
    InputMapper mapper;
    mapper.configure(one_channel(5, 0, InputType::PushButton));

    map(mapper, 5, 0, 2);  // toggles on
    TEST_ASSERT_TRUE(mapper.toggle_state(0));
    mapper.switch_feedback(0, false);  // KNX side switched it off again
    TEST_ASSERT_FALSE(mapper.toggle_state(0));

    const auto next = map(mapper, 5, 0, 2);
    TEST_ASSERT_EQUAL_UINT8(1, next.count);
    TEST_ASSERT_EQUAL_UINT8(1, next.actions[0].data[0]);
}

void test_on_and_off_functions() {
    InputMapper mapper;
    auto config = one_channel(5, 0, InputType::PushButton, InputFunction::On);
    config.inputs[1] = InputChannel{6, 0, InputType::PushButton, InputFunction::Off, 1, 0};
    mapper.configure(config);

    const auto on = map(mapper, 5, 0, 2);
    TEST_ASSERT_EQUAL_UINT8(1, on.count);
    TEST_ASSERT_EQUAL_UINT16(object(0, InputObjectFunction::Switch), on.actions[0].object);
    TEST_ASSERT_EQUAL_UINT8(1, on.actions[0].data[0]);
    TEST_ASSERT_EQUAL_UINT8(1, map(mapper, 5, 0, 2).actions[0].data[0]);  // stays on

    const auto off = map(mapper, 6, 0, 2);
    TEST_ASSERT_EQUAL_UINT8(1, off.count);
    TEST_ASSERT_EQUAL_UINT16(object(1, InputObjectFunction::Switch), off.actions[0].object);
    TEST_ASSERT_EQUAL_UINT8(0, off.actions[0].data[0]);
}

void test_toggle_dim_short_toggle_and_alternating_long_press() {
    InputMapper mapper;
    mapper.configure(one_channel(5, 0, InputType::PushButton, InputFunction::ToggleDim));

    const auto toggled = map(mapper, 5, 0, 2);  // short press
    TEST_ASSERT_EQUAL_UINT8(1, toggled.count);
    TEST_ASSERT_EQUAL_UINT16(object(0, InputObjectFunction::Switch), toggled.actions[0].object);
    TEST_ASSERT_EQUAL_UINT8(1, toggled.actions[0].data[0]);

    const auto up = map(mapper, 5, 0, 9);  // long press start
    TEST_ASSERT_EQUAL_UINT8(1, up.count);
    TEST_ASSERT_EQUAL_UINT16(object(0, InputObjectFunction::Dimming), up.actions[0].object);
    TEST_ASSERT_EQUAL_UINT8(1, up.actions[0].length);
    TEST_ASSERT_EQUAL_UINT8(0x09, up.actions[0].data[0]);  // increase, step code 1

    const auto stop = map(mapper, 5, 0, 12);  // long press stop
    TEST_ASSERT_EQUAL_UINT8(1, stop.count);
    TEST_ASSERT_EQUAL_UINT16(object(0, InputObjectFunction::Dimming), stop.actions[0].object);
    TEST_ASSERT_EQUAL_UINT8(0x00, stop.actions[0].data[0]);  // break/stop

    const auto down = map(mapper, 5, 0, 9);                  // direction alternates
    TEST_ASSERT_EQUAL_UINT8(0x01, down.actions[0].data[0]);  // decrease, step code 1

    TEST_ASSERT_EQUAL_UINT8(0x09, map(mapper, 5, 0, 9).actions[0].data[0]);
}

void test_scene_button_sends_scene_number() {
    InputMapper mapper;
    mapper.configure(one_channel(5, 0, InputType::PushButton, InputFunction::Scene, 20));

    const auto sent = map(mapper, 5, 0, 2);
    TEST_ASSERT_EQUAL_UINT8(1, sent.count);
    TEST_ASSERT_EQUAL_UINT16(object(0, InputObjectFunction::Scene), sent.actions[0].object);
    TEST_ASSERT_EQUAL_UINT8(1, sent.actions[0].length);
    TEST_ASSERT_EQUAL_UINT8(19, sent.actions[0].data[0]);  // DPT 17 is 0-based on the wire
}

void test_button_events_without_actions_are_ignored() {
    InputMapper mapper;
    mapper.configure(one_channel(5, 0, InputType::PushButton));

    // Pressed, released, double press, repeat and the fault codes produce
    // no KNX traffic under the default functions.
    for (const std::uint16_t info : {0, 1, 5, 11, 14, 15}) {
        TEST_ASSERT_EQUAL_UINT8(0, map(mapper, 5, 0, info).count);
    }
    // ToggleDim still ignores repeat and fault codes.
    mapper.configure(one_channel(5, 0, InputType::PushButton, InputFunction::ToggleDim));
    for (const std::uint16_t info : {0, 1, 5, 11, 14, 15}) {
        TEST_ASSERT_EQUAL_UINT8(0, map(mapper, 5, 0, info).count);
    }
}

void test_occupancy_maps_to_switch_value() {
    InputMapper mapper;
    mapper.configure(one_channel(7, 1, InputType::Occupancy));

    const auto occupied = map(mapper, 7, 1, 0x03, device::type::occupancy);
    TEST_ASSERT_EQUAL_UINT8(1, occupied.count);
    TEST_ASSERT_EQUAL_UINT16(object(0, InputObjectFunction::Switch), occupied.actions[0].object);
    TEST_ASSERT_EQUAL_UINT8(1, occupied.actions[0].data[0]);

    const auto vacant = map(mapper, 7, 1, 0x00, device::type::occupancy);
    TEST_ASSERT_EQUAL_UINT8(1, vacant.count);
    TEST_ASSERT_EQUAL_UINT8(0, vacant.actions[0].data[0]);
}

void test_light_raw_value_without_resolution() {
    InputMapper mapper;
    mapper.configure(one_channel(7, 0, InputType::Light));

    const auto sent = map(mapper, 7, 0, 500, device::type::light);
    TEST_ASSERT_EQUAL_UINT8(1, sent.count);
    TEST_ASSERT_EQUAL_UINT16(object(0, InputObjectFunction::Illuminance), sent.actions[0].object);
    TEST_ASSERT_EQUAL_UINT8(2, sent.actions[0].length);
    // Unknown resolution publishes the raw 10-bit value in step units.
    TEST_ASSERT_FLOAT_WITHIN(10.0F, 500.0F, decode_dpt9(sent.actions[0]));
}

void test_light_threshold_suppresses_small_changes() {
    InputMapper mapper;
    mapper.configure(one_channel(7, 0, InputType::Light, InputFunction::Toggle, 1, 20));

    TEST_ASSERT_EQUAL_UINT8(1, map(mapper, 7, 0, 500).count);  // first event always sends
    TEST_ASSERT_EQUAL_UINT8(0, map(mapper, 7, 0, 550).count);  // +10 % suppressed
    TEST_ASSERT_EQUAL_UINT8(0, map(mapper, 7, 0, 450).count);  // -10 % suppressed
    TEST_ASSERT_EQUAL_UINT8(1, map(mapper, 7, 0, 610).count);  // +22 % sends
    TEST_ASSERT_EQUAL_UINT8(1, map(mapper, 7, 0, 300).count);  // -51 % sends
    // A fresh baseline is whatever was last sent.
    TEST_ASSERT_EQUAL_UINT8(0, map(mapper, 7, 0, 310).count);
}

void test_light_threshold_zero_sends_every_event() {
    InputMapper mapper;
    mapper.configure(one_channel(7, 0, InputType::Light, InputFunction::Toggle, 1, 0));

    TEST_ASSERT_EQUAL_UINT8(1, map(mapper, 7, 0, 500).count);
    TEST_ASSERT_EQUAL_UINT8(1, map(mapper, 7, 0, 501).count);
    TEST_ASSERT_EQUAL_UINT8(1, map(mapper, 7, 0, 501).count);
}

// Minimal Part 103 bus: a light device answering QUERY RESOLUTION, so the
// mapper picks up the lux step from the inventory.
class ResolutionBus final : public gateway::dali::Transport {
public:
    TransportStatus set_level(gateway::dali::Address, gateway::dali::ArcLevel) override {
        return TransportStatus::Ok;
    }
    TransportStatus send(gateway::dali::Address, gateway::dali::GearCommand) override {
        return TransportStatus::Ok;
    }
    TransportStatus send_command(gateway::dali::Address, std::uint8_t, bool) override {
        return TransportStatus::Ok;
    }
    TransportStatus set_dtr(gateway::dali::Dtr, std::uint8_t) override { return TransportStatus::Ok; }
    TransportStatus enable_device_type(std::uint8_t) override { return TransportStatus::Ok; }
    ByteRead query(gateway::dali::ShortAddress, std::uint8_t) override {
        return {TransportStatus::Timeout, std::nullopt};
    }
    TransportStatus go_to_scene(gateway::dali::Address, gateway::dali::Scene) override {
        return TransportStatus::Ok;
    }
    gateway::dali::LevelRead read_level(gateway::dali::ShortAddress) override {
        return {TransportStatus::Timeout, std::nullopt};
    }
    gateway::dali::GroupsRead read_groups(gateway::dali::ShortAddress) override {
        return {TransportStatus::Timeout, std::nullopt};
    }
    TransportStatus set_group_membership(gateway::dali::ShortAddress, gateway::dali::Group, bool) override {
        return TransportStatus::Ok;
    }
    gateway::dali::GearPresence query_present(gateway::dali::ShortAddress) override {
        return {TransportStatus::Timeout, false};
    }
    gateway::dali::GearStatusRead query_status(gateway::dali::ShortAddress) override {
        return {TransportStatus::Timeout, std::nullopt};
    }
    gateway::dali::CommissionResult commission(gateway::dali::CommissionMode, gateway::dali::ShortAddress,
                                               unsigned) override {
        return {TransportStatus::Unsupported, 0};
    }
    TransportStatus identify(gateway::dali::ShortAddress) override { return TransportStatus::Ok; }
    TransportStatus clear_short_addresses() override { return TransportStatus::Ok; }
    TransportStatus send_device_frame(std::uint32_t, bool) override { return TransportStatus::Ok; }

    ByteRead query_device_frame(std::uint32_t frame) override {
        const auto address = static_cast<std::uint8_t>(frame >> 16);
        const auto instance = static_cast<std::uint8_t>(frame >> 8);
        const auto opcode = static_cast<std::uint8_t>(frame);
        if ((address & 0x81) != 0x01 || (address >> 1) != 7) {
            return {TransportStatus::Timeout, std::nullopt};
        }
        if (instance == device::instance_device && opcode == device::cmd::query_number_of_instances) {
            return {TransportStatus::Ok, 1};
        }
        if (instance == 0 && opcode == device::inst::query_instance_type) {
            return {TransportStatus::Ok, device::type::light};
        }
        if (instance == 0 && opcode == device::inst::query_resolution) {
            return {TransportStatus::Ok, 12};
        }
        return {TransportStatus::Timeout, std::nullopt};
    }
};

void test_light_resolution_scales_to_lux() {
    ResolutionBus bus;
    gateway::dali::DeviceInventory inventory{bus};
    TEST_ASSERT_EQUAL(TransportStatus::Ok, inventory.scan());

    InputMapper mapper;
    mapper.configure(one_channel(7, 0, InputType::Light));
    mapper.refresh_resolutions(inventory);
    TEST_ASSERT_EQUAL_UINT8(12, mapper.resolution(0));

    const auto sent = map(mapper, 7, 0, 100);
    TEST_ASSERT_EQUAL_UINT8(1, sent.count);
    TEST_ASSERT_FLOAT_WITHIN(24.0F, 1200.0F, decode_dpt9(sent.actions[0]));
}

void test_unconfigured_instances_and_type_mismatch_are_dropped() {
    InputMapper mapper;
    mapper.configure(one_channel(5, 0, InputType::PushButton));

    TEST_ASSERT_EQUAL_UINT8(0, map(mapper, 5, 1, 2).count);  // other instance
    TEST_ASSERT_EQUAL_UINT8(0, map(mapper, 6, 0, 2).count);  // other device
    // The inventory says the bound instance is an occupancy sensor, not a
    // button: the channel's declared type wins and the event is dropped.
    TEST_ASSERT_EQUAL_UINT8(0, map(mapper, 5, 0, 2, device::type::occupancy).count);
    // A matching reported type is accepted.
    TEST_ASSERT_EQUAL_UINT8(1, map(mapper, 5, 0, 2, device::type::push_button).count);
}

void test_bound_reports_configured_pairs() {
    InputMapper mapper;
    auto config = one_channel(5, 0, InputType::PushButton);
    config.inputs[3] = InputChannel{9, 2, InputType::Light, InputFunction::Toggle, 1, 0};
    mapper.configure(config);

    TEST_ASSERT_TRUE(mapper.bound(5, 0));
    TEST_ASSERT_TRUE(mapper.bound(9, 2));
    TEST_ASSERT_FALSE(mapper.bound(5, 1));
    TEST_ASSERT_FALSE(mapper.bound(9, 0));
    TEST_ASSERT_FALSE(mapper.bound(0, 0));
}

}

void run_input_mapper_tests() {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_toggle_short_press_alternates);
    RUN_TEST(test_switch_feedback_flips_next_toggle);
    RUN_TEST(test_on_and_off_functions);
    RUN_TEST(test_toggle_dim_short_toggle_and_alternating_long_press);
    RUN_TEST(test_scene_button_sends_scene_number);
    RUN_TEST(test_button_events_without_actions_are_ignored);
    RUN_TEST(test_occupancy_maps_to_switch_value);
    RUN_TEST(test_light_raw_value_without_resolution);
    RUN_TEST(test_light_threshold_suppresses_small_changes);
    RUN_TEST(test_light_threshold_zero_sends_every_event);
    RUN_TEST(test_light_resolution_scales_to_lux);
    RUN_TEST(test_unconfigured_instances_and_type_mismatch_are_dropped);
    RUN_TEST(test_bound_reports_configured_pairs);
}
