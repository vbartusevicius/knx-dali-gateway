#include <cstdint>
#include <cstdlib>

#include <unity.h>

#include "control/Command.hpp"
#include "knx/BrightnessScale.hpp"
#include "knx/LightingObjects.hpp"
#include "knx/ObjectMap.hpp"

using gateway::control::Command;
using gateway::control::SetLevel;
using gateway::control::StatusUpdate;
using gateway::control::SwitchOff;
using gateway::control::SwitchOn;
using gateway::dali::Address;
using gateway::dali::ArcLevel;
using gateway::dali::Group;
using gateway::dali::ShortAddress;
using gateway::knx::ObjectFunction;
using gateway::lighting::StateSource;

void setUp() {}
void tearDown() {}

namespace {

Address channel(unsigned value) {
    return Address::short_address(*ShortAddress::from(value));
}

Address group(unsigned value) {
    return Address::group(*Group::from(value));
}

ArcLevel level(unsigned value) {
    auto result = ArcLevel::from(value);
    TEST_ASSERT_TRUE(result.has_value());
    return result.value();
}

void test_scaling_edge_values() {
    TEST_ASSERT_EQUAL_UINT8(0, gateway::knx::arc_from_scaling(0).value());
    TEST_ASSERT_EQUAL_UINT8(1, gateway::knx::arc_from_scaling(1).value());
    TEST_ASSERT_EQUAL_UINT8(254, gateway::knx::arc_from_scaling(255).value());
}

void test_scaling_is_monotonic_over_all_raw_values() {
    auto previous = gateway::knx::arc_from_scaling(0).value();
    for (unsigned raw = 1; raw <= 255; ++raw) {
        const auto current = gateway::knx::arc_from_scaling(static_cast<std::uint8_t>(raw)).value();
        TEST_ASSERT_GREATER_OR_EQUAL_UINT8(previous, current);
        previous = current;
    }
}

void test_arc_to_scaling_edge_values() {
    TEST_ASSERT_EQUAL_UINT8(0, gateway::knx::scaling_from_arc(level(0)));
    TEST_ASSERT_EQUAL_UINT8(1, gateway::knx::scaling_from_arc(level(1)));
    TEST_ASSERT_EQUAL_UINT8(255, gateway::knx::scaling_from_arc(level(254)));
}

void test_arc_scaling_arc_roundtrip_for_all_levels() {
    for (unsigned arc = 0; arc <= 254; ++arc) {
        const auto raw = gateway::knx::scaling_from_arc(level(arc));
        TEST_ASSERT_EQUAL_UINT8(arc, gateway::knx::arc_from_scaling(raw).value());
    }
}

void test_object_target_decoding_channel_group_broadcast() {
    const auto channel_first = gateway::knx::object_target(1);
    TEST_ASSERT_TRUE(channel_first.has_value());
    TEST_ASSERT_TRUE(channel_first->address == channel(0));
    TEST_ASSERT_EQUAL(static_cast<int>(ObjectFunction::Switch), static_cast<int>(channel_first->function));

    const auto channel_last = gateway::knx::object_target(320);
    TEST_ASSERT_TRUE(channel_last.has_value());
    TEST_ASSERT_TRUE(channel_last->address == channel(63));
    TEST_ASSERT_EQUAL(static_cast<int>(ObjectFunction::RelativeDimming),
                      static_cast<int>(channel_last->function));

    const auto group_first = gateway::knx::object_target(321);
    TEST_ASSERT_TRUE(group_first.has_value());
    TEST_ASSERT_TRUE(group_first->address == group(0));
    TEST_ASSERT_EQUAL(static_cast<int>(ObjectFunction::Switch), static_cast<int>(group_first->function));

    const auto group_last = gateway::knx::object_target(400);
    TEST_ASSERT_TRUE(group_last.has_value());
    TEST_ASSERT_TRUE(group_last->address == group(15));
    TEST_ASSERT_EQUAL(static_cast<int>(ObjectFunction::RelativeDimming),
                      static_cast<int>(group_last->function));

    const ObjectFunction expected[] = {ObjectFunction::Switch, ObjectFunction::Brightness,
                                       ObjectFunction::RelativeDimming, ObjectFunction::Scene};
    for (std::uint16_t number = 401; number <= 404; ++number) {
        const auto decoded = gateway::knx::object_target(number);
        TEST_ASSERT_TRUE(decoded.has_value());
        TEST_ASSERT_TRUE(decoded->address == Address::broadcast());
        TEST_ASSERT_EQUAL(static_cast<int>(expected[number - 401]), static_cast<int>(decoded->function));
    }
}

void test_object_target_rejects_out_of_range_numbers() {
    TEST_ASSERT_FALSE(gateway::knx::object_target(0).has_value());
    TEST_ASSERT_FALSE(gateway::knx::object_target(405).has_value());
}

void test_object_number_is_zero_for_nonexistent_combinations() {
    TEST_ASSERT_EQUAL_UINT16(0, gateway::knx::object_number(channel(0), ObjectFunction::Scene));
    TEST_ASSERT_EQUAL_UINT16(0, gateway::knx::object_number(group(0), ObjectFunction::Scene));
    TEST_ASSERT_EQUAL_UINT16(0, gateway::knx::object_number(Address::broadcast(), ObjectFunction::SwitchStatus));
    TEST_ASSERT_EQUAL_UINT16(0, gateway::knx::object_number(Address::broadcast(), ObjectFunction::BrightnessStatus));
}

void test_object_map_round_trip_for_all_objects() {
    TEST_ASSERT_EQUAL_UINT16(404, gateway::knx::object_count);
    for (std::uint16_t number = 1; number <= gateway::knx::object_count; ++number) {
        const auto decoded = gateway::knx::object_target(number);
        TEST_ASSERT_TRUE(decoded.has_value());
        TEST_ASSERT_EQUAL_UINT16(number, gateway::knx::object_number(decoded->address, decoded->function));
    }
}

void test_switch_command_for_channel_five() {
    const auto on = gateway::knx::command_for_switch(26, true);
    TEST_ASSERT_TRUE(on.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<SwitchOn>(*on));
    TEST_ASSERT_TRUE(std::get<SwitchOn>(*on).address == channel(5));

    const auto off = gateway::knx::command_for_switch(26, false);
    TEST_ASSERT_TRUE(off.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<SwitchOff>(*off));
}

void test_switch_command_for_group_and_broadcast() {
    const auto group_on = gateway::knx::command_for_switch(331, true);  // group 2 switch
    TEST_ASSERT_TRUE(group_on.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<SwitchOn>(*group_on));
    TEST_ASSERT_TRUE(std::get<SwitchOn>(*group_on).address == group(2));

    const auto broadcast_off = gateway::knx::command_for_switch(401, false);
    TEST_ASSERT_TRUE(broadcast_off.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<SwitchOff>(*broadcast_off));
    TEST_ASSERT_TRUE(std::get<SwitchOff>(*broadcast_off).address == Address::broadcast());
}

void test_scaling_command_for_group_and_broadcast() {
    const auto group_command = gateway::knx::command_for_scaling(333, 128);  // group 2 brightness
    TEST_ASSERT_TRUE(group_command.has_value());
    TEST_ASSERT_TRUE(std::get<SetLevel>(*group_command).address == group(2));

    const auto broadcast_command = gateway::knx::command_for_scaling(402, 255);
    TEST_ASSERT_TRUE(broadcast_command.has_value());
    TEST_ASSERT_TRUE(std::get<SetLevel>(*broadcast_command).address == Address::broadcast());
    TEST_ASSERT_EQUAL_UINT8(254, std::get<SetLevel>(*broadcast_command).level.value());
}

void test_switch_and_scaling_decoders_reject_dim_and_scene_objects() {
    TEST_ASSERT_FALSE(gateway::knx::command_for_switch(30, true).has_value());
    TEST_ASSERT_FALSE(gateway::knx::command_for_scaling(30, 1).has_value());
    TEST_ASSERT_FALSE(gateway::knx::command_for_switch(403, true).has_value());
    TEST_ASSERT_FALSE(gateway::knx::command_for_scaling(404, 1).has_value());
}

void test_status_objects_do_not_produce_commands() {
    TEST_ASSERT_FALSE(gateway::knx::command_for_switch(27, true).has_value());
    TEST_ASSERT_FALSE(gateway::knx::command_for_scaling(29, 128).has_value());
    TEST_ASSERT_FALSE(gateway::knx::command_for_switch(332, true).has_value());  // group switch status
}

void test_scaling_command_maps_full_range() {
    const auto off = gateway::knx::command_for_scaling(28, 0);
    TEST_ASSERT_TRUE(off.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<SetLevel>(*off));
    TEST_ASSERT_TRUE(std::get<SetLevel>(*off).address == channel(5));
    TEST_ASSERT_EQUAL_UINT8(0, std::get<SetLevel>(*off).level.value());

    const auto full = gateway::knx::command_for_scaling(28, 255);
    TEST_ASSERT_TRUE(full.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<SetLevel>(*full));
    TEST_ASSERT_EQUAL_UINT8(254, std::get<SetLevel>(*full).level.value());
}

void test_invalid_objects_produce_no_command() {
    TEST_ASSERT_FALSE(gateway::knx::command_for_switch(0, true).has_value());
    TEST_ASSERT_FALSE(gateway::knx::command_for_scaling(0, 1).has_value());
    TEST_ASSERT_FALSE(gateway::knx::command_for_scaling(405, 1).has_value());
}

void test_relative_dimming_decodes_start_and_stop() {
    const auto start_up = gateway::knx::command_for_relative_dimming(30, 0x09);  // ch5, up, code 1
    TEST_ASSERT_TRUE(start_up.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<gateway::control::DimStart>(*start_up));
    TEST_ASSERT_TRUE(std::get<gateway::control::DimStart>(*start_up).address == channel(5));
    TEST_ASSERT_EQUAL(static_cast<int>(gateway::lighting::DimDirection::Up),
                      static_cast<int>(std::get<gateway::control::DimStart>(*start_up).direction));

    const auto start_down = gateway::knx::command_for_relative_dimming(30, 0x01);  // down, code 1
    TEST_ASSERT_TRUE(start_down.has_value());
    TEST_ASSERT_EQUAL(static_cast<int>(gateway::lighting::DimDirection::Down),
                      static_cast<int>(std::get<gateway::control::DimStart>(*start_down).direction));

    const auto stop_down = gateway::knx::command_for_relative_dimming(30, 0x00);
    TEST_ASSERT_TRUE(stop_down.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<gateway::control::DimStop>(*stop_down));

    const auto stop_up = gateway::knx::command_for_relative_dimming(30, 0x08);
    TEST_ASSERT_TRUE(stop_up.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<gateway::control::DimStop>(*stop_up));
}

void test_relative_dimming_on_group_and_broadcast_objects() {
    const auto group_dim = gateway::knx::command_for_relative_dimming(325, 0x0F);  // group 0 dim
    TEST_ASSERT_TRUE(group_dim.has_value());
    TEST_ASSERT_TRUE(std::get<gateway::control::DimStart>(*group_dim).address == group(0));

    const auto broadcast_dim = gateway::knx::command_for_relative_dimming(403, 0x03);
    TEST_ASSERT_TRUE(broadcast_dim.has_value());
    TEST_ASSERT_TRUE(std::get<gateway::control::DimStart>(*broadcast_dim).address == Address::broadcast());

    TEST_ASSERT_FALSE(gateway::knx::command_for_relative_dimming(1, 0x09).has_value());   // switch object
    TEST_ASSERT_FALSE(gateway::knx::command_for_relative_dimming(404, 0x09).has_value()); // scene object
}

void test_scene_decodes_recall_and_store() {
    const auto recall = gateway::knx::command_for_scene(404, 0x00);
    TEST_ASSERT_TRUE(recall.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<gateway::control::RecallScene>(*recall));
    TEST_ASSERT_EQUAL_UINT8(0, std::get<gateway::control::RecallScene>(*recall).scene.value());

    const auto recall_max = gateway::knx::command_for_scene(404, 0x0F);
    TEST_ASSERT_TRUE(recall_max.has_value());
    TEST_ASSERT_EQUAL_UINT8(15, std::get<gateway::control::RecallScene>(*recall_max).scene.value());

    const auto store = gateway::knx::command_for_scene(404, 0x80);
    TEST_ASSERT_TRUE(store.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<gateway::control::StoreScene>(*store));
    TEST_ASSERT_EQUAL_UINT8(0, std::get<gateway::control::StoreScene>(*store).scene.value());

    TEST_ASSERT_FALSE(gateway::knx::command_for_scene(404, 0x10).has_value());  // scene 16+ has no DALI scene
    TEST_ASSERT_FALSE(gateway::knx::command_for_scene(404, 0x90).has_value());
    TEST_ASSERT_FALSE(gateway::knx::command_for_scene(403, 0x00).has_value());  // relative dimming object
}

void test_status_objects_report_known_levels() {
    const auto off = gateway::knx::status_objects(
        StatusUpdate{channel(5), {level(0), StateSource::Readback}});
    TEST_ASSERT_TRUE(off.has_value());
    TEST_ASSERT_EQUAL_UINT16(27, off->switch_object);
    TEST_ASSERT_FALSE(off->on);
    TEST_ASSERT_EQUAL_UINT16(29, off->brightness_object);
    TEST_ASSERT_EQUAL_UINT8(0, off->scaling);

    const auto full = gateway::knx::status_objects(
        StatusUpdate{channel(5), {level(254), StateSource::Commanded}});
    TEST_ASSERT_TRUE(full.has_value());
    TEST_ASSERT_TRUE(full->on);
    TEST_ASSERT_EQUAL_UINT8(255, full->scaling);
}

void test_status_objects_for_group() {
    const auto update = gateway::knx::status_objects(
        StatusUpdate{group(2), {level(100), StateSource::Commanded}});
    TEST_ASSERT_TRUE(update.has_value());
    TEST_ASSERT_EQUAL_UINT16(332, update->switch_object);
    TEST_ASSERT_EQUAL_UINT16(334, update->brightness_object);
}

void test_status_objects_broadcast_has_none() {
    const auto update = gateway::knx::status_objects(
        StatusUpdate{Address::broadcast(), {level(100), StateSource::Commanded}});
    TEST_ASSERT_FALSE(update.has_value());
}

void test_status_objects_unknown_level_has_no_status() {
    const auto update = gateway::knx::status_objects(
        StatusUpdate{channel(5), {std::nullopt, StateSource::Unknown}});
    TEST_ASSERT_FALSE(update.has_value());
}

}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_scaling_edge_values);
    RUN_TEST(test_scaling_is_monotonic_over_all_raw_values);
    RUN_TEST(test_arc_to_scaling_edge_values);
    RUN_TEST(test_arc_scaling_arc_roundtrip_for_all_levels);
    RUN_TEST(test_object_target_decoding_channel_group_broadcast);
    RUN_TEST(test_object_target_rejects_out_of_range_numbers);
    RUN_TEST(test_object_number_is_zero_for_nonexistent_combinations);
    RUN_TEST(test_object_map_round_trip_for_all_objects);
    RUN_TEST(test_switch_command_for_channel_five);
    RUN_TEST(test_switch_command_for_group_and_broadcast);
    RUN_TEST(test_scaling_command_for_group_and_broadcast);
    RUN_TEST(test_switch_and_scaling_decoders_reject_dim_and_scene_objects);
    RUN_TEST(test_relative_dimming_decodes_start_and_stop);
    RUN_TEST(test_relative_dimming_on_group_and_broadcast_objects);
    RUN_TEST(test_scene_decodes_recall_and_store);
    RUN_TEST(test_status_objects_do_not_produce_commands);
    RUN_TEST(test_scaling_command_maps_full_range);
    RUN_TEST(test_invalid_objects_produce_no_command);
    RUN_TEST(test_status_objects_report_known_levels);
    RUN_TEST(test_status_objects_for_group);
    RUN_TEST(test_status_objects_broadcast_has_none);
    RUN_TEST(test_status_objects_unknown_level_has_no_status);
    return UNITY_END() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
