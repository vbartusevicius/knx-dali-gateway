#include <cstdint>
#include <cstdlib>
#include <optional>
#include <variant>

#include <unity.h>

#include "control/Command.hpp"
#include "knx/BrightnessScale.hpp"
#include "knx/ColourDatapoints.hpp"
#include "knx/CommissioningProtocol.hpp"
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

    const auto channel_last = gateway::knx::object_target(960);
    TEST_ASSERT_TRUE(channel_last.has_value());
    TEST_ASSERT_TRUE(channel_last->address == channel(63));
    TEST_ASSERT_EQUAL(static_cast<int>(ObjectFunction::XyYStatus), static_cast<int>(channel_last->function));

    const auto group_first = gateway::knx::object_target(961);
    TEST_ASSERT_TRUE(group_first.has_value());
    TEST_ASSERT_TRUE(group_first->address == group(0));
    TEST_ASSERT_EQUAL(static_cast<int>(ObjectFunction::Switch), static_cast<int>(group_first->function));

    const auto group_last = gateway::knx::object_target(1184);
    TEST_ASSERT_TRUE(group_last.has_value());
    TEST_ASSERT_TRUE(group_last->address == group(15));
    TEST_ASSERT_EQUAL(static_cast<int>(ObjectFunction::XyYStatus), static_cast<int>(group_last->function));

    const ObjectFunction expected[] = {ObjectFunction::Switch,
                                       ObjectFunction::Brightness,
                                       ObjectFunction::RelativeDimming,
                                       ObjectFunction::Scene,
                                       ObjectFunction::ColourTemperature,
                                       ObjectFunction::RelativeColourTemperature,
                                       ObjectFunction::Rgb,
                                       ObjectFunction::Rgbw,
                                       ObjectFunction::XyY};
    for (std::uint16_t number = 1185; number <= 1193; ++number) {
        const auto decoded = gateway::knx::object_target(number);
        TEST_ASSERT_TRUE(decoded.has_value());
        TEST_ASSERT_TRUE(decoded->address == Address::broadcast());
        TEST_ASSERT_EQUAL(static_cast<int>(expected[number - 1185]), static_cast<int>(decoded->function));
    }
}

void test_object_target_rejects_out_of_range_numbers() {
    TEST_ASSERT_FALSE(gateway::knx::object_target(0).has_value());
    TEST_ASSERT_FALSE(gateway::knx::object_target(1322).has_value());
}

void test_input_object_map_layout() {
    // Input objects live after the broadcast objects: 32 channels x 4
    // objects (switch, dimming, illuminance, scene), 1194..1321.
    TEST_ASSERT_EQUAL_UINT16(1194,
                             gateway::knx::input_object_number(0, gateway::knx::InputObjectFunction::Switch));
    TEST_ASSERT_EQUAL_UINT16(
        1195, gateway::knx::input_object_number(0, gateway::knx::InputObjectFunction::Dimming));
    TEST_ASSERT_EQUAL_UINT16(
        1196, gateway::knx::input_object_number(0, gateway::knx::InputObjectFunction::Illuminance));
    TEST_ASSERT_EQUAL_UINT16(1197,
                             gateway::knx::input_object_number(0, gateway::knx::InputObjectFunction::Scene));
    TEST_ASSERT_EQUAL_UINT16(1194 + 4 * 31 + 3,
                             gateway::knx::input_object_number(31, gateway::knx::InputObjectFunction::Scene));
    TEST_ASSERT_EQUAL_UINT16(1321,
                             gateway::knx::input_object_number(31, gateway::knx::InputObjectFunction::Scene));
    TEST_ASSERT_EQUAL_UINT16(
        0, gateway::knx::input_object_number(32, gateway::knx::InputObjectFunction::Switch));

    for (std::uint16_t number = 1194; number <= 1321; ++number) {
        const auto decoded = gateway::knx::input_object_target(number);
        TEST_ASSERT_TRUE(decoded.has_value());
        TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>((number - 1194) / 4), decoded->channel);
        TEST_ASSERT_EQUAL(static_cast<int>(gateway::knx::InputObjectFunction::Switch) +
                              static_cast<int>((number - 1194) % 4),
                          static_cast<int>(decoded->function));
        TEST_ASSERT_EQUAL_UINT16(number,
                                 gateway::knx::input_object_number(decoded->channel, decoded->function));
    }
    TEST_ASSERT_FALSE(gateway::knx::input_object_target(1193).has_value());
    TEST_ASSERT_FALSE(gateway::knx::input_object_target(1322).has_value());
    // The gear object map does not claim the input range and vice versa.
    TEST_ASSERT_FALSE(gateway::knx::object_target(1194).has_value());
}

void test_object_number_is_zero_for_nonexistent_combinations() {
    TEST_ASSERT_EQUAL_UINT16(0, gateway::knx::object_number(channel(0), ObjectFunction::Scene));
    TEST_ASSERT_EQUAL_UINT16(0, gateway::knx::object_number(group(0), ObjectFunction::Scene));
    TEST_ASSERT_EQUAL_UINT16(0, gateway::knx::object_number(group(0), ObjectFunction::Fault));
    TEST_ASSERT_EQUAL_UINT16(0,
                             gateway::knx::object_number(Address::broadcast(), ObjectFunction::SwitchStatus));
    TEST_ASSERT_EQUAL_UINT16(
        0, gateway::knx::object_number(Address::broadcast(), ObjectFunction::BrightnessStatus));
    TEST_ASSERT_EQUAL_UINT16(0, gateway::knx::object_number(Address::broadcast(), ObjectFunction::Fault));
    // Broadcast has colour write objects but no colour status objects.
    TEST_ASSERT_EQUAL_UINT16(
        0, gateway::knx::object_number(Address::broadcast(), ObjectFunction::ColourTemperatureStatus));
    TEST_ASSERT_EQUAL_UINT16(0, gateway::knx::object_number(Address::broadcast(), ObjectFunction::RgbStatus));
    TEST_ASSERT_EQUAL_UINT16(0,
                             gateway::knx::object_number(Address::broadcast(), ObjectFunction::RgbwStatus));
    TEST_ASSERT_EQUAL_UINT16(0, gateway::knx::object_number(Address::broadcast(), ObjectFunction::XyYStatus));
}

void test_channel_fault_object_numbers() {
    TEST_ASSERT_EQUAL_UINT16(6, gateway::knx::object_number(channel(0), ObjectFunction::Fault));
    TEST_ASSERT_EQUAL_UINT16(81, gateway::knx::object_number(channel(5), ObjectFunction::Fault));
    TEST_ASSERT_EQUAL_UINT16(951, gateway::knx::object_number(channel(63), ObjectFunction::Fault));
}

void test_object_map_round_trip_for_all_objects() {
    TEST_ASSERT_EQUAL_UINT16(1321, gateway::knx::object_count);
    for (std::uint16_t number = 1; number <= 1193; ++number) {
        const auto decoded = gateway::knx::object_target(number);
        TEST_ASSERT_TRUE(decoded.has_value());
        TEST_ASSERT_EQUAL_UINT16(number, gateway::knx::object_number(decoded->address, decoded->function));
    }
    for (std::uint16_t number = 1194; number <= gateway::knx::object_count; ++number) {
        const auto decoded = gateway::knx::input_object_target(number);
        TEST_ASSERT_TRUE(decoded.has_value());
        TEST_ASSERT_EQUAL_UINT16(number,
                                 gateway::knx::input_object_number(decoded->channel, decoded->function));
    }
}

void test_switch_command_for_channel_five() {
    const auto on = gateway::knx::command_for_switch(76, true);
    TEST_ASSERT_TRUE(on.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<SwitchOn>(*on));
    TEST_ASSERT_TRUE(std::get<SwitchOn>(*on).address == channel(5));

    const auto off = gateway::knx::command_for_switch(76, false);
    TEST_ASSERT_TRUE(off.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<SwitchOff>(*off));
}

void test_switch_command_for_group_and_broadcast() {
    const auto group_on = gateway::knx::command_for_switch(989, true);  // group 2 switch
    TEST_ASSERT_TRUE(group_on.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<SwitchOn>(*group_on));
    TEST_ASSERT_TRUE(std::get<SwitchOn>(*group_on).address == group(2));

    const auto broadcast_off = gateway::knx::command_for_switch(1185, false);
    TEST_ASSERT_TRUE(broadcast_off.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<SwitchOff>(*broadcast_off));
    TEST_ASSERT_TRUE(std::get<SwitchOff>(*broadcast_off).address == Address::broadcast());
}

void test_scaling_command_for_group_and_broadcast() {
    const auto group_command = gateway::knx::command_for_scaling(991, 128);  // group 2 brightness
    TEST_ASSERT_TRUE(group_command.has_value());
    TEST_ASSERT_TRUE(std::get<SetLevel>(*group_command).address == group(2));

    const auto broadcast_command = gateway::knx::command_for_scaling(1186, 255);
    TEST_ASSERT_TRUE(broadcast_command.has_value());
    TEST_ASSERT_TRUE(std::get<SetLevel>(*broadcast_command).address == Address::broadcast());
    TEST_ASSERT_EQUAL_UINT8(254, std::get<SetLevel>(*broadcast_command).level.value());
}

void test_switch_and_scaling_decoders_reject_dim_and_scene_objects() {
    TEST_ASSERT_FALSE(gateway::knx::command_for_switch(80, true).has_value());  // ch5 rel dim
    TEST_ASSERT_FALSE(gateway::knx::command_for_scaling(80, 1).has_value());
    TEST_ASSERT_FALSE(gateway::knx::command_for_switch(1187, true).has_value());  // broadcast rel dim
    TEST_ASSERT_FALSE(gateway::knx::command_for_scaling(1188, 1).has_value());    // broadcast scene
}

void test_status_objects_do_not_produce_commands() {
    TEST_ASSERT_FALSE(gateway::knx::command_for_switch(77, true).has_value());   // ch5 switch status
    TEST_ASSERT_FALSE(gateway::knx::command_for_scaling(79, 128).has_value());   // ch5 brightness status
    TEST_ASSERT_FALSE(gateway::knx::command_for_switch(990, true).has_value());  // group 2 switch status
}

void test_scaling_command_maps_full_range() {
    const auto off = gateway::knx::command_for_scaling(78, 0);
    TEST_ASSERT_TRUE(off.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<SetLevel>(*off));
    TEST_ASSERT_TRUE(std::get<SetLevel>(*off).address == channel(5));
    TEST_ASSERT_EQUAL_UINT8(0, std::get<SetLevel>(*off).level.value());

    const auto full = gateway::knx::command_for_scaling(78, 255);
    TEST_ASSERT_TRUE(full.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<SetLevel>(*full));
    TEST_ASSERT_EQUAL_UINT8(254, std::get<SetLevel>(*full).level.value());
}

void test_invalid_objects_produce_no_command() {
    TEST_ASSERT_FALSE(gateway::knx::command_for_switch(0, true).has_value());
    TEST_ASSERT_FALSE(gateway::knx::command_for_scaling(0, 1).has_value());
    TEST_ASSERT_FALSE(gateway::knx::command_for_scaling(1194, 1).has_value());
}

void test_relative_dimming_decodes_start_and_stop() {
    const auto start_up = gateway::knx::command_for_relative_dimming(80, 0x09);  // ch5, up, code 1
    TEST_ASSERT_TRUE(start_up.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<gateway::control::DimStart>(*start_up));
    TEST_ASSERT_TRUE(std::get<gateway::control::DimStart>(*start_up).address == channel(5));
    TEST_ASSERT_EQUAL(static_cast<int>(gateway::lighting::DimDirection::Up),
                      static_cast<int>(std::get<gateway::control::DimStart>(*start_up).direction));

    const auto start_down = gateway::knx::command_for_relative_dimming(80, 0x01);  // down, code 1
    TEST_ASSERT_TRUE(start_down.has_value());
    TEST_ASSERT_EQUAL(static_cast<int>(gateway::lighting::DimDirection::Down),
                      static_cast<int>(std::get<gateway::control::DimStart>(*start_down).direction));

    const auto stop_down = gateway::knx::command_for_relative_dimming(80, 0x00);
    TEST_ASSERT_TRUE(stop_down.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<gateway::control::DimStop>(*stop_down));

    const auto stop_up = gateway::knx::command_for_relative_dimming(80, 0x08);
    TEST_ASSERT_TRUE(stop_up.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<gateway::control::DimStop>(*stop_up));
}

void test_relative_dimming_on_group_and_broadcast_objects() {
    const auto group_dim = gateway::knx::command_for_relative_dimming(965, 0x0F);  // group 0 dim
    TEST_ASSERT_TRUE(group_dim.has_value());
    TEST_ASSERT_TRUE(std::get<gateway::control::DimStart>(*group_dim).address == group(0));

    const auto broadcast_dim = gateway::knx::command_for_relative_dimming(1187, 0x03);
    TEST_ASSERT_TRUE(broadcast_dim.has_value());
    TEST_ASSERT_TRUE(std::get<gateway::control::DimStart>(*broadcast_dim).address == Address::broadcast());

    TEST_ASSERT_FALSE(gateway::knx::command_for_relative_dimming(1, 0x09).has_value());     // switch object
    TEST_ASSERT_FALSE(gateway::knx::command_for_relative_dimming(1188, 0x09).has_value());  // scene object
}

void test_scene_decodes_recall_and_store() {
    const auto recall = gateway::knx::command_for_scene(1188, 0x00);
    TEST_ASSERT_TRUE(recall.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<gateway::control::RecallScene>(*recall));
    TEST_ASSERT_EQUAL_UINT8(0, std::get<gateway::control::RecallScene>(*recall).scene.value());

    const auto recall_max = gateway::knx::command_for_scene(1188, 0x0F);
    TEST_ASSERT_TRUE(recall_max.has_value());
    TEST_ASSERT_EQUAL_UINT8(15, std::get<gateway::control::RecallScene>(*recall_max).scene.value());

    const auto store = gateway::knx::command_for_scene(1188, 0x80);
    TEST_ASSERT_TRUE(store.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<gateway::control::StoreScene>(*store));
    TEST_ASSERT_EQUAL_UINT8(0, std::get<gateway::control::StoreScene>(*store).scene.value());

    TEST_ASSERT_FALSE(
        gateway::knx::command_for_scene(1188, 0x10).has_value());  // scene 16+ has no DALI scene
    TEST_ASSERT_FALSE(gateway::knx::command_for_scene(1188, 0x90).has_value());
    TEST_ASSERT_FALSE(gateway::knx::command_for_scene(1187, 0x00).has_value());  // relative dimming object
}

void test_status_objects_report_known_levels() {
    const auto off =
        gateway::knx::status_objects(StatusUpdate{channel(5), {level(0), StateSource::Readback}});
    TEST_ASSERT_TRUE(off.has_value());
    TEST_ASSERT_EQUAL_UINT16(77, off->switch_object);
    TEST_ASSERT_FALSE(off->on);
    TEST_ASSERT_EQUAL_UINT16(79, off->brightness_object);
    TEST_ASSERT_EQUAL_UINT8(0, off->scaling);

    const auto full =
        gateway::knx::status_objects(StatusUpdate{channel(5), {level(254), StateSource::Commanded}});
    TEST_ASSERT_TRUE(full.has_value());
    TEST_ASSERT_TRUE(full->on);
    TEST_ASSERT_EQUAL_UINT8(255, full->scaling);
}

void test_status_objects_for_group() {
    const auto update =
        gateway::knx::status_objects(StatusUpdate{group(2), {level(100), StateSource::Commanded}});
    TEST_ASSERT_TRUE(update.has_value());
    TEST_ASSERT_EQUAL_UINT16(990, update->switch_object);
    TEST_ASSERT_EQUAL_UINT16(992, update->brightness_object);
}

void test_status_objects_broadcast_has_none() {
    const auto update = gateway::knx::status_objects(
        StatusUpdate{Address::broadcast(), {level(100), StateSource::Commanded}});
    TEST_ASSERT_FALSE(update.has_value());
}

void test_status_objects_unknown_level_has_no_status() {
    const auto update =
        gateway::knx::status_objects(StatusUpdate{channel(5), {std::nullopt, StateSource::Unknown}});
    TEST_ASSERT_FALSE(update.has_value());
}

// --- colour datapoints (DPT 7.600 / 232.600 / 251.600 / 242.600) ----------

void test_colour_temperature_datapoint_roundtrip() {
    const auto encoded = gateway::knx::encode_colour_temperature({2700});
    TEST_ASSERT_EQUAL_UINT8(0x0A, encoded[0]);
    TEST_ASSERT_EQUAL_UINT8(0x8C, encoded[1]);
    const auto decoded = gateway::knx::decode_colour_temperature(encoded.data(), encoded.size());
    TEST_ASSERT_TRUE(decoded.has_value());
    TEST_ASSERT_EQUAL_UINT16(2700, decoded->kelvin);

    const auto max =
        gateway::knx::decode_colour_temperature(std::array<std::uint8_t, 2>{0xFF, 0xFF}.data(), 2);
    TEST_ASSERT_EQUAL_UINT16(65535, max->kelvin);
    const std::uint8_t short_data[] = {0x01};
    TEST_ASSERT_FALSE(gateway::knx::decode_colour_temperature(short_data, 1).has_value());
}

void test_rgb_datapoint_roundtrip() {
    const auto decoded = gateway::knx::decode_rgb(std::array<std::uint8_t, 3>{0x10, 0x80, 0xFF}.data(), 3);
    TEST_ASSERT_TRUE(decoded.has_value());
    TEST_ASSERT_EQUAL_UINT8(0x10, decoded->red);
    TEST_ASSERT_EQUAL_UINT8(0x80, decoded->green);
    TEST_ASSERT_EQUAL_UINT8(0xFF, decoded->blue);
    const std::uint8_t bad[] = {1, 2};
    TEST_ASSERT_FALSE(gateway::knx::decode_rgb(bad, 2).has_value());
}

void test_rgbw_datapoint_layout_and_validity() {
    // DPT 251.600: R, G, B, W, reserved, validity mask (bit3=R..bit0=W).
    const auto encoded = gateway::knx::encode_rgbw({10, 20, 30, 40, 0b1011});
    const std::uint8_t expected[] = {10, 20, 30, 40, 0, 0b1011};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, encoded.data(), 6);

    const auto decoded = gateway::knx::decode_rgbw(encoded.data(), encoded.size());
    TEST_ASSERT_TRUE(decoded.has_value());
    TEST_ASSERT_EQUAL_UINT8(10, decoded->red);
    TEST_ASSERT_EQUAL_UINT8(40, decoded->white);
    TEST_ASSERT_EQUAL_UINT8(0b1011, decoded->validity);

    const std::uint8_t mask[] = {0, 0, 0, 0, 0, 0xFF};
    const auto masked = gateway::knx::decode_rgbw(mask, 6);
    TEST_ASSERT_EQUAL_UINT8(0x0F, masked->validity);  // upper nibble ignored
    TEST_ASSERT_FALSE(gateway::knx::decode_rgbw(mask, 5).has_value());
}

void test_xyy_datapoint_layout_and_validity() {
    // DPT 242.600: x u16 BE, y u16 BE, brightness u8, reserved, validity
    // (bit1 = xy, bit0 = brightness).
    const auto encoded = gateway::knx::encode_xyy({0x3227, 0x3341, 200, 0b11});
    const std::uint8_t expected[] = {0x32, 0x27, 0x33, 0x41, 200, 0b11};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, encoded.data(), 6);

    const auto decoded = gateway::knx::decode_xyy(encoded.data(), encoded.size());
    TEST_ASSERT_TRUE(decoded.has_value());
    TEST_ASSERT_EQUAL_UINT16(0x3227, decoded->x);
    TEST_ASSERT_EQUAL_UINT16(0x3341, decoded->y);
    TEST_ASSERT_EQUAL_UINT8(200, decoded->brightness);
    TEST_ASSERT_EQUAL_UINT8(0b11, decoded->validity);

    const std::uint8_t brightness_only[] = {0, 0, 0, 0, 128, 0b01};
    const auto partial = gateway::knx::decode_xyy(brightness_only, 6);
    TEST_ASSERT_EQUAL_UINT8(0b01, partial->validity);
    TEST_ASSERT_FALSE(gateway::knx::decode_xyy(brightness_only, 5).has_value());
}

// --- colour object decoders -------------------------------------------------

void test_colour_temperature_command() {
    const std::uint8_t data[] = {0x0A, 0x8C};  // 2700 K
    const auto channel_cmd = gateway::knx::command_for_colour_temperature(82, data, 2);
    TEST_ASSERT_TRUE(channel_cmd.has_value());
    const auto& set = std::get<gateway::control::SetColourTemperature>(*channel_cmd);
    TEST_ASSERT_TRUE(set.address == channel(5));
    TEST_ASSERT_EQUAL_UINT16(2700, set.kelvin);

    const auto group_cmd = gateway::knx::command_for_colour_temperature(994, data, 2);
    TEST_ASSERT_TRUE(std::get<gateway::control::SetColourTemperature>(*group_cmd).address == group(2));

    const auto broadcast_cmd = gateway::knx::command_for_colour_temperature(1189, data, 2);
    TEST_ASSERT_TRUE(std::get<gateway::control::SetColourTemperature>(*broadcast_cmd).address ==
                     Address::broadcast());

    TEST_ASSERT_FALSE(gateway::knx::command_for_colour_temperature(82, data, 1).has_value());
    TEST_ASSERT_FALSE(gateway::knx::command_for_colour_temperature(83, data, 2).has_value());  // status
}

void test_relative_colour_temperature_decodes() {
    const auto start = gateway::knx::command_for_relative_colour_temperature(84, 0x09);
    TEST_ASSERT_TRUE(start.has_value());
    const auto& dim = std::get<gateway::control::ColourDimStart>(*start);
    TEST_ASSERT_TRUE(dim.address == channel(5));
    TEST_ASSERT_EQUAL(static_cast<int>(gateway::lighting::DimDirection::Up), static_cast<int>(dim.direction));

    const auto stop = gateway::knx::command_for_relative_colour_temperature(1190, 0x00);
    TEST_ASSERT_TRUE(stop.has_value());
    const auto& dim_stop = std::get<gateway::control::ColourDimStop>(*stop);
    TEST_ASSERT_TRUE(dim_stop.address == Address::broadcast());

    TEST_ASSERT_FALSE(gateway::knx::command_for_relative_colour_temperature(80, 0x09).has_value());
}

void test_rgb_command() {
    const std::uint8_t data[] = {0x10, 0x80, 0xFF};
    const auto decoded = gateway::knx::command_for_rgb(85, data, 3);
    TEST_ASSERT_TRUE(decoded.has_value());
    const auto& set = std::get<gateway::control::SetRgb>(*decoded);
    TEST_ASSERT_TRUE(set.address == channel(5));
    TEST_ASSERT_EQUAL_UINT8(0x10, set.red);
    TEST_ASSERT_EQUAL_UINT8(0x80, set.green);
    TEST_ASSERT_EQUAL_UINT8(0xFF, set.blue);

    TEST_ASSERT_TRUE(gateway::knx::command_for_rgb(1191, data, 3).has_value());
    TEST_ASSERT_FALSE(gateway::knx::command_for_rgb(85, data, 2).has_value());
    TEST_ASSERT_FALSE(gateway::knx::command_for_rgb(86, data, 3).has_value());  // RGB status
}

void test_rgbw_command() {
    const std::uint8_t data[] = {10, 20, 30, 40, 0, 0b1011};
    const auto decoded = gateway::knx::command_for_rgbw(87, data, 6);
    TEST_ASSERT_TRUE(decoded.has_value());
    const auto& set = std::get<gateway::control::SetRgbw>(*decoded);
    TEST_ASSERT_TRUE(set.address == channel(5));
    TEST_ASSERT_EQUAL_UINT8(30, set.blue);
    TEST_ASSERT_EQUAL_UINT8(40, set.white);
    TEST_ASSERT_EQUAL_UINT8(0b1011, set.validity);

    TEST_ASSERT_TRUE(gateway::knx::command_for_rgbw(1192, data, 6).has_value());
    TEST_ASSERT_FALSE(gateway::knx::command_for_rgbw(87, data, 5).has_value());
}

void test_xyy_command() {
    const std::uint8_t data[] = {0x32, 0x27, 0x33, 0x41, 200, 0b11};
    const auto decoded = gateway::knx::command_for_xyy(89, data, 6);
    TEST_ASSERT_TRUE(decoded.has_value());
    const auto& set = std::get<gateway::control::SetXy>(*decoded);
    TEST_ASSERT_TRUE(set.address == channel(5));
    TEST_ASSERT_EQUAL_UINT16(0x3227, set.x);
    TEST_ASSERT_EQUAL_UINT16(0x3341, set.y);
    TEST_ASSERT_EQUAL_UINT8(200, set.brightness);
    TEST_ASSERT_TRUE(set.xy_valid);
    TEST_ASSERT_TRUE(set.brightness_valid);

    const std::uint8_t brightness_only[] = {0, 0, 0, 0, 128, 0b01};
    const auto partial = gateway::knx::command_for_xyy(89, brightness_only, 6);
    TEST_ASSERT_TRUE(partial.has_value());
    TEST_ASSERT_FALSE(std::get<gateway::control::SetXy>(*partial).xy_valid);
    TEST_ASSERT_TRUE(std::get<gateway::control::SetXy>(*partial).brightness_valid);

    TEST_ASSERT_TRUE(gateway::knx::command_for_xyy(1193, data, 6).has_value());
    TEST_ASSERT_FALSE(gateway::knx::command_for_xyy(90, data, 6).has_value());  // xyY status
}

void test_colour_status_object_per_kind() {
    using gateway::control::ColourUpdate;
    using gateway::lighting::ColourKind;
    using gateway::lighting::ColourReport;

    ColourReport temperature{ColourKind::Temperature};
    temperature.kelvin = 4000;
    const auto tc = gateway::knx::colour_status_object(ColourUpdate{channel(5), temperature});
    TEST_ASSERT_TRUE(tc.has_value());
    TEST_ASSERT_EQUAL_UINT16(83, tc->object);  // ch5 Tc status
    TEST_ASSERT_EQUAL_UINT8(2, tc->length);
    TEST_ASSERT_EQUAL_UINT8(4000 >> 8, tc->data[0]);
    TEST_ASSERT_EQUAL_UINT8(4000 & 0xFF, tc->data[1]);

    ColourReport rgb{ColourKind::Rgb};
    rgb.r = 10;
    rgb.g = 20;
    rgb.b = 30;
    const auto rgb_status = gateway::knx::colour_status_object(ColourUpdate{group(2), rgb});
    TEST_ASSERT_TRUE(rgb_status.has_value());
    TEST_ASSERT_EQUAL_UINT16(998, rgb_status->object);  // group 2 RGB status
    TEST_ASSERT_EQUAL_UINT8(3, rgb_status->length);
    TEST_ASSERT_EQUAL_UINT8(20, rgb_status->data[1]);

    ColourReport rgbw{ColourKind::Rgbw};
    rgbw.r = 1;
    rgbw.g = 2;
    rgbw.b = 3;
    rgbw.w = 4;
    rgbw.rgbw_validity = 0b0110;
    const auto rgbw_status = gateway::knx::colour_status_object(ColourUpdate{channel(5), rgbw});
    TEST_ASSERT_EQUAL_UINT16(88, rgbw_status->object);
    TEST_ASSERT_EQUAL_UINT8(6, rgbw_status->length);
    TEST_ASSERT_EQUAL_UINT8(0b0110, rgbw_status->data[5]);

    ColourReport xyy{ColourKind::XyY};
    xyy.x = 0x3227;
    xyy.y = 0x3341;
    xyy.brightness = 128;
    const auto xyy_status = gateway::knx::colour_status_object(ColourUpdate{channel(5), xyy});
    TEST_ASSERT_EQUAL_UINT16(90, xyy_status->object);
    TEST_ASSERT_EQUAL_UINT8(6, xyy_status->length);
    TEST_ASSERT_EQUAL_UINT8(0x32, xyy_status->data[0]);
    TEST_ASSERT_EQUAL_UINT8(128, xyy_status->data[4]);
    TEST_ASSERT_EQUAL_UINT8(0b11, xyy_status->data[5]);  // a report is fully valid

    ColourReport none{ColourKind::None};
    TEST_ASSERT_FALSE(gateway::knx::colour_status_object(ColourUpdate{channel(5), none}).has_value());
    // Broadcast colour changes have no status object.
    TEST_ASSERT_FALSE(
        gateway::knx::colour_status_object(ColourUpdate{Address::broadcast(), temperature}).has_value());
}

using gateway::commissioning::Phase;
using gateway::commissioning::Snapshot;
using gateway::dali::CommissionMode;
using gateway::dali::TransportStatus;
using gateway::knx::CommandProperty;
using gateway::knx::RequestResponse;

void test_protocol_decodes_scan() {
    const auto decoded =
        gateway::knx::decode_command(gateway::knx::function_property_object_index,
                                     static_cast<std::uint8_t>(CommandProperty::Scan), nullptr, 0);
    TEST_ASSERT_TRUE(decoded.command.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<gateway::control::ScanBus>(*decoded.command));
    TEST_ASSERT_EQUAL(static_cast<int>(RequestResponse::Accepted), static_cast<int>(decoded.response));
}

void test_protocol_decodes_assign_modes_and_rejects_invalid() {
    const auto assign = static_cast<std::uint8_t>(CommandProperty::Assign);
    const std::uint8_t unaddressed[] = {0};
    auto decoded =
        gateway::knx::decode_command(gateway::knx::function_property_object_index, assign, unaddressed, 1);
    TEST_ASSERT_TRUE(decoded.command.has_value());
    TEST_ASSERT_EQUAL(static_cast<int>(CommissionMode::Unaddressed),
                      static_cast<int>(std::get<gateway::control::AssignAddresses>(*decoded.command).mode));

    const std::uint8_t all[] = {1};
    decoded = gateway::knx::decode_command(gateway::knx::function_property_object_index, assign, all, 1);
    TEST_ASSERT_EQUAL(static_cast<int>(CommissionMode::All),
                      static_cast<int>(std::get<gateway::control::AssignAddresses>(*decoded.command).mode));

    const std::uint8_t invalid[] = {2};
    decoded = gateway::knx::decode_command(gateway::knx::function_property_object_index, assign, invalid, 1);
    TEST_ASSERT_FALSE(decoded.command.has_value());
    TEST_ASSERT_EQUAL(static_cast<int>(RequestResponse::InvalidArgument), static_cast<int>(decoded.response));

    decoded = gateway::knx::decode_command(gateway::knx::function_property_object_index, assign, nullptr, 0);
    TEST_ASSERT_EQUAL(static_cast<int>(RequestResponse::InvalidArgument), static_cast<int>(decoded.response));
}

void test_protocol_decodes_replace_and_identify_addresses() {
    const std::uint8_t five[] = {5};
    auto decoded = gateway::knx::decode_command(gateway::knx::function_property_object_index,
                                                static_cast<std::uint8_t>(CommandProperty::Replace), five, 1);
    TEST_ASSERT_TRUE(decoded.command.has_value());
    TEST_ASSERT_EQUAL_UINT8(5, std::get<gateway::control::ReplaceDevice>(*decoded.command).address.value());

    decoded = gateway::knx::decode_command(gateway::knx::function_property_object_index,
                                           static_cast<std::uint8_t>(CommandProperty::Identify), five, 1);
    TEST_ASSERT_TRUE(decoded.command.has_value());
    TEST_ASSERT_EQUAL_UINT8(5, std::get<gateway::control::IdentifyDevice>(*decoded.command).address.value());

    const std::uint8_t out_of_range[] = {64};
    decoded =
        gateway::knx::decode_command(gateway::knx::function_property_object_index,
                                     static_cast<std::uint8_t>(CommandProperty::Replace), out_of_range, 1);
    TEST_ASSERT_EQUAL(static_cast<int>(RequestResponse::InvalidArgument), static_cast<int>(decoded.response));
    decoded = gateway::knx::decode_command(gateway::knx::function_property_object_index,
                                           static_cast<std::uint8_t>(CommandProperty::Identify), nullptr, 0);
    TEST_ASSERT_EQUAL(static_cast<int>(RequestResponse::InvalidArgument), static_cast<int>(decoded.response));
}

void test_protocol_decodes_reset_addresses() {
    const auto decoded =
        gateway::knx::decode_command(gateway::knx::function_property_object_index,
                                     static_cast<std::uint8_t>(CommandProperty::ResetAddresses), nullptr, 0);
    TEST_ASSERT_TRUE(decoded.command.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<gateway::control::ResetAddresses>(*decoded.command));
    TEST_ASSERT_EQUAL(static_cast<int>(RequestResponse::Accepted), static_cast<int>(decoded.response));
}

void test_protocol_decodes_input_commands() {
    const auto scan =
        gateway::knx::decode_command(gateway::knx::function_property_object_index,
                                     static_cast<std::uint8_t>(CommandProperty::ScanInputs), nullptr, 0);
    TEST_ASSERT_TRUE(scan.command.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<gateway::control::ScanInputDevices>(*scan.command));
    TEST_ASSERT_EQUAL(static_cast<int>(RequestResponse::Accepted), static_cast<int>(scan.response));

    const auto assign = static_cast<std::uint8_t>(CommandProperty::AssignInputs);
    const std::uint8_t unaddressed[] = {0};
    auto decoded =
        gateway::knx::decode_command(gateway::knx::function_property_object_index, assign, unaddressed, 1);
    TEST_ASSERT_TRUE(decoded.command.has_value());
    TEST_ASSERT_EQUAL(
        static_cast<int>(CommissionMode::Unaddressed),
        static_cast<int>(std::get<gateway::control::AssignInputDevices>(*decoded.command).mode));

    const std::uint8_t all[] = {1};
    decoded = gateway::knx::decode_command(gateway::knx::function_property_object_index, assign, all, 1);
    TEST_ASSERT_EQUAL(
        static_cast<int>(CommissionMode::All),
        static_cast<int>(std::get<gateway::control::AssignInputDevices>(*decoded.command).mode));

    const std::uint8_t invalid[] = {2};
    decoded = gateway::knx::decode_command(gateway::knx::function_property_object_index, assign, invalid, 1);
    TEST_ASSERT_FALSE(decoded.command.has_value());
    TEST_ASSERT_EQUAL(static_cast<int>(RequestResponse::InvalidArgument), static_cast<int>(decoded.response));
    decoded = gateway::knx::decode_command(gateway::knx::function_property_object_index, assign, nullptr, 0);
    TEST_ASSERT_EQUAL(static_cast<int>(RequestResponse::InvalidArgument), static_cast<int>(decoded.response));
}

void test_protocol_encodes_input_snapshot_layout() {
    gateway::commissioning::InputSnapshot snapshot{};
    snapshot.present = {0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80};
    snapshot.summary[0] = 0x20 | 2;   // device 0: 2 instances incl. a push button
    snapshot.summary[9] = 0x40 | 1;   // device 9: 1 instance incl. occupancy
    snapshot.summary[63] = 0x80 | 3;  // device 63: 3 instances incl. light

    std::uint8_t output[gateway::knx::input_state_response_size] = {};
    const auto length = gateway::knx::encode_input_state(snapshot, output);
    TEST_ASSERT_EQUAL_UINT8(gateway::knx::input_state_response_size, length);
    TEST_ASSERT_EQUAL_UINT8(72, length);
    TEST_ASSERT_EQUAL_UINT8(0x01, output[0]);  // presence: device 0
    TEST_ASSERT_EQUAL_UINT8(0x02, output[1]);  // device 9
    TEST_ASSERT_EQUAL_UINT8(0x80, output[7]);  // device 63
    TEST_ASSERT_EQUAL_UINT8(0x22, output[8]);
    TEST_ASSERT_EQUAL_UINT8(0x41, output[8 + 9]);
    TEST_ASSERT_EQUAL_UINT8(0x83, output[8 + 63]);
}

void test_protocol_rejects_wrong_object_and_property() {
    auto decoded = gateway::knx::decode_command(gateway::knx::function_property_object_index - 1,
                                                static_cast<std::uint8_t>(CommandProperty::Scan), nullptr, 0);
    TEST_ASSERT_EQUAL(static_cast<int>(RequestResponse::InvalidArgument), static_cast<int>(decoded.response));
    decoded = gateway::knx::decode_command(gateway::knx::function_property_object_index, 99, nullptr, 0);
    TEST_ASSERT_EQUAL(static_cast<int>(RequestResponse::InvalidArgument), static_cast<int>(decoded.response));
}

void test_protocol_response_byte_values() {
    // The single response byte on the wire: [0] accepted, [1] busy, [2] invalid.
    TEST_ASSERT_EQUAL_UINT8(0, static_cast<std::uint8_t>(RequestResponse::Accepted));
    TEST_ASSERT_EQUAL_UINT8(1, static_cast<std::uint8_t>(RequestResponse::Busy));
    TEST_ASSERT_EQUAL_UINT8(2, static_cast<std::uint8_t>(RequestResponse::InvalidArgument));
}

void test_protocol_encodes_snapshot_layout() {
    Snapshot snapshot{};
    snapshot.progress.phase = Phase::Scanning;
    snapshot.progress.progress = 42;
    snapshot.progress.found = 7;
    snapshot.progress.last_error = TransportStatus::Timeout;
    snapshot.present = {0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80};
    snapshot.faulty = {0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    std::uint8_t output[gateway::knx::state_response_size] = {};
    const auto length = gateway::knx::encode_state(snapshot, output);
    TEST_ASSERT_EQUAL_UINT8(gateway::knx::state_response_size, length);
    TEST_ASSERT_EQUAL_UINT8(20, length);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(Phase::Scanning), output[0]);
    TEST_ASSERT_EQUAL_UINT8(42, output[1]);
    TEST_ASSERT_EQUAL_UINT8(7, output[2]);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TransportStatus::Timeout), output[3]);
    TEST_ASSERT_EQUAL_UINT8(0x01, output[4]);   // presence: address 0 → byte 0 bit 0
    TEST_ASSERT_EQUAL_UINT8(0x02, output[5]);   // address 9 → byte 1 bit 1
    TEST_ASSERT_EQUAL_UINT8(0x80, output[11]);  // address 63 → byte 7 bit 7
    TEST_ASSERT_EQUAL_UINT8(0x02, output[13]);  // fault: address 9 → byte 1 bit 1
}

void test_protocol_decodes_lamp_finder_commands() {
    const std::uint8_t blink_data[] = {12, 1};
    const auto blink = gateway::knx::decode_command(160, 8, blink_data, 2);
    TEST_ASSERT_TRUE(blink.command.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<gateway::control::FindLamp>(*blink.command));
    const auto& fl = std::get<gateway::control::FindLamp>(*blink.command);
    TEST_ASSERT_EQUAL_UINT8(12, fl.address.as_short()->value());
    TEST_ASSERT_EQUAL(static_cast<int>(gateway::lighting::FinderMode::BlinkOnOff), static_cast<int>(fl.mode));

    const std::uint8_t stop_data[] = {0xFF, 0};
    const auto stop = gateway::knx::decode_command(160, 8, stop_data, 2);
    TEST_ASSERT_TRUE(stop.command.has_value());
    const auto& fl_stop = std::get<gateway::control::FindLamp>(*stop.command);
    TEST_ASSERT_EQUAL(static_cast<int>(gateway::lighting::FinderMode::Stop), static_cast<int>(fl_stop.mode));
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
    RUN_TEST(test_channel_fault_object_numbers);
    RUN_TEST(test_object_map_round_trip_for_all_objects);
    RUN_TEST(test_input_object_map_layout);
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
    RUN_TEST(test_colour_temperature_datapoint_roundtrip);
    RUN_TEST(test_rgb_datapoint_roundtrip);
    RUN_TEST(test_rgbw_datapoint_layout_and_validity);
    RUN_TEST(test_xyy_datapoint_layout_and_validity);
    RUN_TEST(test_colour_temperature_command);
    RUN_TEST(test_relative_colour_temperature_decodes);
    RUN_TEST(test_rgb_command);
    RUN_TEST(test_rgbw_command);
    RUN_TEST(test_xyy_command);
    RUN_TEST(test_colour_status_object_per_kind);
    RUN_TEST(test_protocol_decodes_scan);
    RUN_TEST(test_protocol_decodes_assign_modes_and_rejects_invalid);
    RUN_TEST(test_protocol_decodes_replace_and_identify_addresses);
    RUN_TEST(test_protocol_decodes_reset_addresses);
    RUN_TEST(test_protocol_decodes_input_commands);
    RUN_TEST(test_protocol_encodes_input_snapshot_layout);
    RUN_TEST(test_protocol_rejects_wrong_object_and_property);
    RUN_TEST(test_protocol_response_byte_values);
    RUN_TEST(test_protocol_encodes_snapshot_layout);
    RUN_TEST(test_protocol_decodes_lamp_finder_commands);
    return UNITY_END() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
