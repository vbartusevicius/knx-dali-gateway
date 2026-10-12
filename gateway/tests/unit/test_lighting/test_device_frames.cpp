// Copyright (c) 2025 the knx-dali-gateway contributors
#include <unity.h>

#include "dali/devices/DeviceFrames.hpp"
#include "dali/devices/DeviceInventory.hpp"

namespace {

using namespace gateway::dali;

void test_device_address_bytes() {
    // Short address n -> 0AAAAAA1; group g -> 10GGGGG1.
    TEST_ASSERT_EQUAL_UINT8(0x01, device::address_short(0));
    TEST_ASSERT_EQUAL_UINT8(0x0B, device::address_short(5));
    TEST_ASSERT_EQUAL_UINT8(0x7F, device::address_short(63));
    TEST_ASSERT_EQUAL_UINT8(0x81, device::address_group(0));
    TEST_ASSERT_EQUAL_UINT8(0xBF, device::address_group(31));
    TEST_ASSERT_EQUAL_UINT8(0xFF, device::address_broadcast);
    TEST_ASSERT_EQUAL_UINT8(0xFD, device::address_unaddressed);
}

void test_instance_bytes() {
    TEST_ASSERT_EQUAL_UINT8(0x00, device::instance_number(0));
    TEST_ASSERT_EQUAL_UINT8(0x1F, device::instance_number(31));
    TEST_ASSERT_EQUAL_UINT8(0x80, device::instance_group(0));
    TEST_ASSERT_EQUAL_UINT8(0x9F, device::instance_group(31));
    TEST_ASSERT_EQUAL_UINT8(0xC1, device::instance_type(1));
    TEST_ASSERT_EQUAL_UINT8(0xC4, device::instance_type(4));
    TEST_ASSERT_EQUAL_UINT8(0xFE, device::instance_device);
    TEST_ASSERT_EQUAL_UINT8(0xFF, device::instance_broadcast);
}

void test_command_frames() {
    // {address, instance, opcode}; verified against python-dali's
    // dali/device/general.py frame layout.
    TEST_ASSERT_EQUAL_UINT32(0x0BFE1D,
                             device::command(device::address_short(5), device::instance_device, 0x1D));
    TEST_ASSERT_EQUAL_UINT32(0xFFFE1D, device::command(device::address_broadcast, device::instance_device,
                                                       device::cmd::start_quiescent_mode));
    TEST_ASSERT_EQUAL_UINT32(0xFFFE1E, device::command(device::address_broadcast, device::instance_device,
                                                       device::cmd::stop_quiescent_mode));
    // QUERY NUMBER OF INSTANCES to device short 3.
    TEST_ASSERT_EQUAL_UINT32(0x07FE35, device::command(device::address_short(3), device::instance_device,
                                                       device::cmd::query_number_of_instances));
    // SET EVENT SCHEME on instance 2 of device 3.
    TEST_ASSERT_EQUAL_UINT32(0x070267, device::command(device::address_short(3), device::instance_number(2),
                                                       device::inst::set_event_scheme));
    // QUERY INSTANCE TYPE on instance 0 of device 3.
    TEST_ASSERT_EQUAL_UINT32(0x070080, device::command(device::address_short(3), device::instance_number(0),
                                                       device::inst::query_instance_type));
}

void test_special_command_frames() {
    // IEC 62386-103 Table 22; verified against python-dali
    // dali/device/general.py (_SpecialDeviceCommand frame layout).
    TEST_ASSERT_EQUAL_UINT32(0xC10000, device::special::terminate());
    TEST_ASSERT_EQUAL_UINT32(0xC101FF, device::special::initialise(0xFF));
    TEST_ASSERT_EQUAL_UINT32(0xC10100, device::special::initialise(0x00));
    TEST_ASSERT_EQUAL_UINT32(0xC10200, device::special::randomise());
    TEST_ASSERT_EQUAL_UINT32(0xC10300, device::special::compare());
    TEST_ASSERT_EQUAL_UINT32(0xC10400, device::special::withdraw());
    TEST_ASSERT_EQUAL_UINT32(0xC105AB, device::special::searchaddr_h(0xAB));
    TEST_ASSERT_EQUAL_UINT32(0xC106CD, device::special::searchaddr_m(0xCD));
    TEST_ASSERT_EQUAL_UINT32(0xC107EF, device::special::searchaddr_l(0xEF));
    TEST_ASSERT_EQUAL_UINT32(0xC10809, device::special::program_short_address(0x09));
    TEST_ASSERT_EQUAL_UINT32(0xC10909, device::special::verify_short_address(0x09));
    TEST_ASSERT_EQUAL_UINT32(0xC10A00, device::special::query_short_address());
    TEST_ASSERT_EQUAL_UINT32(0xC12042, device::special::write_memory_location(0x42));
    TEST_ASSERT_EQUAL_UINT32(0xC12142, device::special::write_memory_location_no_reply(0x42));
    TEST_ASSERT_EQUAL_UINT32(0xC13042, device::special::dtr0(0x42));
    TEST_ASSERT_EQUAL_UINT32(0xC13110, device::special::dtr1(0x10));
    TEST_ASSERT_EQUAL_UINT32(0xC13220, device::special::dtr2(0x20));
    TEST_ASSERT_EQUAL_UINT32(0xC13377, device::special::send_testframe(0x77));
    TEST_ASSERT_EQUAL_UINT32(0xC51122, device::special::direct_write_memory(0x11, 0x22));
    TEST_ASSERT_EQUAL_UINT32(0xC71122, device::special::dtr1_dtr0(0x11, 0x22));
    TEST_ASSERT_EQUAL_UINT32(0xC93344, device::special::dtr2_dtr1(0x33, 0x44));
    TEST_ASSERT_EQUAL_UINT8(0x01, device::special::encode_short_address(0));
    TEST_ASSERT_EQUAL_UINT8(0x7F, device::special::encode_short_address(63));
}

void test_default_event_filters() {
    // Part 301 Table 3: short press + long press start/repeat/stop +
    // button stuck/free = 0x04|0x10|0x20|0x40|0x80.
    TEST_ASSERT_EQUAL_UINT32(0xF4, gateway::dali::default_event_filter(device::type::push_button));
    // Part 303 Table 3: occupied + vacant + movement + no movement.
    TEST_ASSERT_EQUAL_UINT32(0x1B, gateway::dali::default_event_filter(device::type::occupancy));
    // Part 304 Table 2: illuminance level.
    TEST_ASSERT_EQUAL_UINT32(0x01, gateway::dali::default_event_filter(device::type::light));
    TEST_ASSERT_EQUAL_UINT32(0, gateway::dali::default_event_filter(0));
    TEST_ASSERT_EQUAL_UINT32(0, gateway::dali::default_event_filter(99));
}

}

void run_device_frame_tests() {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_device_address_bytes);
    RUN_TEST(test_instance_bytes);
    RUN_TEST(test_command_frames);
    RUN_TEST(test_special_command_frames);
    RUN_TEST(test_default_event_filters);
}
