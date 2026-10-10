// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2025 the knx-dali-gateway contributors
#include <unity.h>

#include "dali/devices/DeviceFrames.hpp"
#include "dali/devices/EventDecoder.hpp"

namespace {

using gateway::dali::BusEvent;
namespace device = gateway::dali::device;
using device::ButtonEvent;

// Device/instance event frame: bit 23 = 0 (short address), bit 16 = 0
// (event), bit 15 = 1; {0AAAAAA, 1, 0IIIII, info[9:0]}.
constexpr std::uint32_t event_frame(std::uint8_t address, std::uint8_t instance,
                                    std::uint16_t info) {
    return (static_cast<std::uint32_t>(address) << 17) | (1u << 15) |
           (static_cast<std::uint32_t>(instance) << 10) | (info & 0x3FF);
}

BusEvent event(std::uint32_t frame) { return BusEvent{frame, 24}; }

void test_decodes_device_instance_event_fields() {
    // Device short 5, instance 2, info 0x002 (push-button short press);
    // frame layout cross-checked against python-dali _Event.
    const auto decoded = device::decode_event(event(event_frame(5, 2, 2)));
    TEST_ASSERT_TRUE(decoded.has_value());
    TEST_ASSERT_EQUAL_UINT8(5, decoded->address);
    TEST_ASSERT_EQUAL_UINT8(2, decoded->instance);
    TEST_ASSERT_EQUAL_UINT16(2, decoded->info);

    const auto boundary = device::decode_event(event(event_frame(63, 31, 0x3FF)));
    TEST_ASSERT_TRUE(boundary.has_value());
    TEST_ASSERT_EQUAL_UINT8(63, boundary->address);
    TEST_ASSERT_EQUAL_UINT8(31, boundary->instance);
    TEST_ASSERT_EQUAL_UINT16(0x3FF, boundary->info);
}

void test_rejects_non_event_and_other_schemes() {
    // Command frames set bit 16.
    TEST_ASSERT_FALSE(
        device::decode_event(event(device::command(device::address_short(5),
                                                   device::instance_device, 0x1D)))
            .has_value());
    // Device scheme (bit 15 clear) is not device/instance.
    TEST_ASSERT_FALSE(device::decode_event(event((5u << 17) | (1u << 10) | 2)).has_value());
    // Device-group / instance-group schemes set bit 23.
    TEST_ASSERT_FALSE(
        device::decode_event(event((1u << 23) | (1u << 15) | 2)).has_value());
    // Non-24-bit frames are never device events.
    TEST_ASSERT_FALSE(device::decode_event(BusEvent{0x0B80, 16}).has_value());
    TEST_ASSERT_FALSE(device::decode_event(BusEvent{0x00, 8}).has_value());
}

void test_button_event_codes() {
    // Part 301 event information values; verified against python-dali
    // dali/device/pushbutton.py.
    TEST_ASSERT_EQUAL(ButtonEvent::Released, *device::decode_button(0));
    TEST_ASSERT_EQUAL(ButtonEvent::Pressed, *device::decode_button(1));
    TEST_ASSERT_EQUAL(ButtonEvent::ShortPress, *device::decode_button(2));
    TEST_ASSERT_EQUAL(ButtonEvent::DoublePress, *device::decode_button(5));
    TEST_ASSERT_EQUAL(ButtonEvent::LongStart, *device::decode_button(9));
    TEST_ASSERT_EQUAL(ButtonEvent::LongRepeat, *device::decode_button(11));
    TEST_ASSERT_EQUAL(ButtonEvent::LongStop, *device::decode_button(12));
    TEST_ASSERT_EQUAL(ButtonEvent::ButtonFree, *device::decode_button(14));
    TEST_ASSERT_EQUAL(ButtonEvent::ButtonStuck, *device::decode_button(15));
    for (std::uint16_t info : {3, 4, 6, 7, 8, 10, 13, 16, 0x3FF}) {
        TEST_ASSERT_FALSE(device::decode_button(info).has_value());
    }
}

void test_occupancy_event_bits() {
    // Part 303: bit 0 movement, bit 1 occupied, bit 2 repeat, bit 3
    // movement-vs-presence sensor flag.
    const auto occupied = device::decode_occupancy(0x03);
    TEST_ASSERT_TRUE(occupied.has_value());
    TEST_ASSERT_TRUE(occupied->movement);
    TEST_ASSERT_TRUE(occupied->occupied);
    TEST_ASSERT_FALSE(occupied->repeat);
    TEST_ASSERT_FALSE(occupied->movement_sensor);

    const auto vacant_repeat = device::decode_occupancy(0x0C);
    TEST_ASSERT_TRUE(vacant_repeat.has_value());
    TEST_ASSERT_FALSE(vacant_repeat->movement);
    TEST_ASSERT_FALSE(vacant_repeat->occupied);
    TEST_ASSERT_TRUE(vacant_repeat->repeat);
    TEST_ASSERT_TRUE(vacant_repeat->movement_sensor);

    // Bits above the lowest nibble are reserved.
    TEST_ASSERT_FALSE(device::decode_occupancy(0x10).has_value());
    TEST_ASSERT_FALSE(device::decode_occupancy(0x1FF).has_value());
}

void test_light_event_value_is_ten_bits() {
    TEST_ASSERT_EQUAL_UINT16(0, device::decode_light(0).illuminance);
    TEST_ASSERT_EQUAL_UINT16(0x3FF, device::decode_light(0x3FF).illuminance);
    TEST_ASSERT_EQUAL_UINT16(0x3FF, device::decode_light(0x7FF).illuminance);
}

void test_typed_pipeline() {
    const auto frame = device::decode_event(event(event_frame(9, 0, 9)));
    TEST_ASSERT_TRUE(frame.has_value());
    const auto button = device::decode_button(frame->info);
    TEST_ASSERT_TRUE(button.has_value());
    TEST_ASSERT_EQUAL(ButtonEvent::LongStart, *button);
}

}

void run_event_decoder_tests() {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_decodes_device_instance_event_fields);
    RUN_TEST(test_rejects_non_event_and_other_schemes);
    RUN_TEST(test_button_event_codes);
    RUN_TEST(test_occupancy_event_bits);
    RUN_TEST(test_light_event_value_is_ten_bits);
    RUN_TEST(test_typed_pipeline);
}
