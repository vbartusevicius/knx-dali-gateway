// Copyright (c) 2025 the knx-dali-gateway contributors
#include <unity.h>

#include "dali/Frame.hpp"

namespace {

using gateway::dali::Address;
using gateway::dali::ArcLevel;
using gateway::dali::Group;
using gateway::dali::ShortAddress;
namespace frame = gateway::dali::frame;

ShortAddress sa(unsigned value) {
    return *ShortAddress::from(value);
}
ArcLevel arc(unsigned value) {
    return *ArcLevel::from(value);
}
Group grp(unsigned value) {
    return *Group::from(value);
}

// All vectors cross-checked against IEC 62386-102 addressing rules:
// short = (a << 1) | selector, group = 0x80 | (g << 1) | selector,
// broadcast = 0xFE | selector; selector 0 = DAPC, 1 = command.

void test_dapc_short_address() {
    TEST_ASSERT_EQUAL_UINT16(0x0A80, frame::dapc(Address::short_address(sa(5)), arc(128)));
    TEST_ASSERT_EQUAL_UINT16(0x0000, frame::dapc(Address::short_address(sa(0)), arc(0)));
    TEST_ASSERT_EQUAL_UINT16(0x7EFE, frame::dapc(Address::short_address(sa(63)), arc(254)));
}

void test_dapc_group_and_broadcast() {
    TEST_ASSERT_EQUAL_UINT16(0xFE80, frame::dapc(Address::broadcast(), arc(128)));
    TEST_ASSERT_EQUAL_UINT16(0x8604, frame::dapc(Address::group(grp(3)), arc(4)));
}

void test_command_frames() {
    // OFF = 0x00, UP = 0x01, QUERY ACTUAL LEVEL = 0xA0
    TEST_ASSERT_EQUAL_UINT16(0xFF00, frame::command(Address::broadcast(), 0x00));
    TEST_ASSERT_EQUAL_UINT16(0x8701, frame::command(Address::group(grp(3)), 0x01));
    TEST_ASSERT_EQUAL_UINT16(0x03A0, frame::command(Address::short_address(sa(1)), 0xA0));
    // GO TO SCENE = 0x10 | scene; group 15 scene 2
    TEST_ASSERT_EQUAL_UINT16(0x9F12, frame::command(Address::group(grp(15)), 0x12));
    // STORE DTR AS SHORT ADDRESS = 0x80, broadcast
    TEST_ASSERT_EQUAL_UINT16(0xFF80, frame::command(Address::broadcast(), 0x80));
}

void test_special_frames() {
    // DTR0 = 0xA3 with the DTR value in the second byte
    TEST_ASSERT_EQUAL_UINT16(0xA3FF, frame::special(0xA3, 0xFF));
    TEST_ASSERT_EQUAL_UINT16(0xA300, frame::special(0xA3, 0x00));
    // INITIALISE = 0xA5, RANDOMISE = 0xA7 carry their operand byte
    TEST_ASSERT_EQUAL_UINT16(0xA500, frame::special(0xA5, 0x00));
    TEST_ASSERT_EQUAL_UINT16(0xA700, frame::special(0xA7, 0x00));
}

}

void run_frame_tests() {
    RUN_TEST(test_dapc_short_address);
    RUN_TEST(test_dapc_group_and_broadcast);
    RUN_TEST(test_command_frames);
    RUN_TEST(test_special_frames);
}
