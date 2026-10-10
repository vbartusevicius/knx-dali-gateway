// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2025 the knx-dali-gateway contributors
#include <unity.h>

#include <optional>
#include <vector>

#include "dali/Transport.hpp"
#include "dali/devices/DeviceAddressing.hpp"
#include "dali/devices/DeviceFrames.hpp"

namespace {

using gateway::dali::ByteRead;
using gateway::dali::CommissionMode;
using gateway::dali::CommissionResult;
using gateway::dali::DeviceAddressing;
using gateway::dali::ShortAddress;
using gateway::dali::TransportStatus;
namespace device = gateway::dali::device;

ShortAddress sa(unsigned value) { return *ShortAddress::from(value); }

// In-memory bus model for Part 103 addressing: each control device tracks
// the initialising/withdrawn flags, its search address and its short
// address; several units answering COMPARE produce a backward collision.
struct SimDeviceState {
    std::uint32_t random;
    std::uint32_t search = 0;
    int short_addr = -1;
    bool initialising = false;
    bool withdrawn = false;
    bool drops_program = false;
};

class DeviceCommissionBus final : public gateway::dali::Transport {
public:
    std::vector<SimDeviceState> devices;
    std::vector<std::pair<std::uint32_t, bool>> frames;
    unsigned quiescent_starts = 0;
    unsigned quiescent_stops = 0;
    unsigned compares = 0;

    TransportStatus set_level(gateway::dali::Address, gateway::dali::ArcLevel) override {
        return TransportStatus::Ok;
    }
    TransportStatus send(gateway::dali::Address, gateway::dali::GearCommand) override {
        return TransportStatus::Ok;
    }
    TransportStatus send_command(gateway::dali::Address, std::uint8_t, bool) override {
        return TransportStatus::Ok;
    }
    TransportStatus set_dtr(gateway::dali::Dtr, std::uint8_t) override {
        return TransportStatus::Ok;
    }
    TransportStatus enable_device_type(std::uint8_t) override {
        return TransportStatus::Ok;
    }
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
    TransportStatus set_group_membership(gateway::dali::ShortAddress, gateway::dali::Group,
                                         bool) override {
        return TransportStatus::Ok;
    }
    gateway::dali::GearPresence query_present(gateway::dali::ShortAddress) override {
        return {TransportStatus::Timeout, false};
    }
    gateway::dali::GearStatusRead query_status(gateway::dali::ShortAddress) override {
        return {TransportStatus::Timeout, std::nullopt};
    }
    CommissionResult commission(CommissionMode, ShortAddress, unsigned) override {
        return {TransportStatus::Unsupported, 0};
    }
    TransportStatus identify(gateway::dali::ShortAddress) override {
        return TransportStatus::Ok;
    }
    TransportStatus clear_short_addresses() override { return TransportStatus::Ok; }

    TransportStatus send_device_frame(std::uint32_t frame, bool twice) override {
        frames.push_back({frame, twice});
        const auto address = static_cast<std::uint8_t>(frame >> 16);
        const auto operation = static_cast<std::uint8_t>(frame >> 8);
        const auto operand = static_cast<std::uint8_t>(frame);
        if (address == 0xC1) {
            switch (operation) {
            case 0x00:  // TERMINATE
                for (auto& d : devices) {
                    d.initialising = false;
                }
                break;
            case 0x01:  // INITIALISE
                for (auto& d : devices) {
                    if (operand == 0x00 ||
                        (operand == 0xFF && d.short_addr < 0) ||
                        operand == device::special::encode_short_address(d.short_addr)) {
                        d.initialising = true;
                        d.withdrawn = false;
                    }
                }
                break;
            case 0x02:  // RANDOMISE: random values are preset by the test
                break;
            case 0x04:  // WITHDRAW
                for (auto& d : devices) {
                    if (d.initialising && d.random == d.search) {
                        d.withdrawn = true;
                    }
                }
                break;
            case 0x05:  // SEARCHADDRH/M/L
            case 0x06:
            case 0x07: {
                const int shift = operation == 0x05 ? 16 : operation == 0x06 ? 8 : 0;
                for (auto& d : devices) {
                    d.search = (d.search & ~(0xFFu << shift)) |
                               (static_cast<std::uint32_t>(operand) << shift);
                }
                break;
            }
            case 0x08:  // PROGRAM SHORT ADDRESS
                for (auto& d : devices) {
                    if (d.initialising && !d.drops_program && d.random == d.search) {
                        d.short_addr = operand >> 1;
                    }
                }
                break;
            default:
                break;
            }
        } else if (operation == device::instance_device &&
                   address == device::address_broadcast) {
            if (operand == device::cmd::start_quiescent_mode) {
                ++quiescent_starts;
            } else if (operand == device::cmd::stop_quiescent_mode) {
                ++quiescent_stops;
            }
        }
        return TransportStatus::Ok;
    }

    ByteRead query_device_frame(std::uint32_t frame) override {
        frames.push_back({frame, false});
        const auto address = static_cast<std::uint8_t>(frame >> 16);
        const auto operation = static_cast<std::uint8_t>(frame >> 8);
        const auto operand = static_cast<std::uint8_t>(frame);
        unsigned responders = 0;
        if (address == 0xC1 && operation == 0x03) {  // COMPARE
            ++compares;
            for (const auto& d : devices) {
                if (d.initialising && !d.withdrawn && d.random <= d.search) {
                    ++responders;
                }
            }
        } else if (address == 0xC1 && operation == 0x09) {  // VERIFY SHORT ADDRESS
            for (const auto& d : devices) {
                if (d.initialising && d.short_addr == (operand >> 1)) {
                    ++responders;
                }
            }
        }
        if (responders == 0) {
            return {TransportStatus::Timeout, std::nullopt};
        }
        if (responders > 1) {
            return {TransportStatus::Collision, std::nullopt};
        }
        return {TransportStatus::Ok, std::uint8_t{0xFF}};
    }
};

void test_commissions_all_devices_in_random_order() {
    DeviceCommissionBus bus;
    bus.devices = {{0x0300}, {0x0050}, {0x0200}, {0x0100}};
    DeviceAddressing addressing{bus};
    const auto result = addressing.commission(CommissionMode::All, sa(0), 4);
    TEST_ASSERT_EQUAL(TransportStatus::Ok, result.status);
    TEST_ASSERT_EQUAL_UINT32(4, result.count);
    TEST_ASSERT_EQUAL(0, bus.devices[1].short_addr);  // lowest random first
    TEST_ASSERT_EQUAL(1, bus.devices[3].short_addr);
    TEST_ASSERT_EQUAL(2, bus.devices[2].short_addr);
    TEST_ASSERT_EQUAL(3, bus.devices[0].short_addr);
    TEST_ASSERT_EQUAL_UINT32(1, bus.quiescent_starts);
    TEST_ASSERT_EQUAL_UINT32(1, bus.quiescent_stops);
    // INITIALISE (operand 0x00 = all devices) is a send-twice command.
    bool saw_all = false;
    for (const auto& call : bus.frames) {
        if ((call.first >> 16) == 0xC1 && ((call.first >> 8) & 0xFF) == 0x01) {
            if ((call.first & 0xFF) == 0x00) {
                saw_all = true;
            }
            TEST_ASSERT_TRUE(call.second);
        }
    }
    TEST_ASSERT_TRUE(saw_all);
}

void test_unaddressed_mode_only_initialises_unaddressed_devices() {
    DeviceCommissionBus bus;
    bus.devices = {{0x0100}, {0x0200}};
    bus.devices[0].short_addr = 7;  // already addressed: stays out
    DeviceAddressing addressing{bus};
    const auto result = addressing.commission(CommissionMode::Unaddressed, sa(1), 5);
    TEST_ASSERT_EQUAL(TransportStatus::Ok, result.status);
    TEST_ASSERT_EQUAL_UINT32(1, result.count);
    TEST_ASSERT_EQUAL(7, bus.devices[0].short_addr);
    TEST_ASSERT_EQUAL(1, bus.devices[1].short_addr);
    bool saw_unaddressed_init = false;
    for (const auto& call : bus.frames) {
        if (call.first == device::special::initialise(0xFF)) {
            saw_unaddressed_init = true;
        }
    }
    TEST_ASSERT_TRUE(saw_unaddressed_init);
}

void test_compare_collision_counts_as_yes() {
    DeviceCommissionBus bus;
    // Two units with the same random address collide on every COMPARE;
    // the search must still converge on them.
    bus.devices = {{0x0800}, {0x0800}};
    DeviceAddressing addressing{bus};
    const auto result = addressing.commission(CommissionMode::All, sa(0), 2);
    TEST_ASSERT_EQUAL(TransportStatus::Ok, result.status);
    TEST_ASSERT_EQUAL_UINT32(1, result.count);
    TEST_ASSERT_EQUAL(0, bus.devices[0].short_addr);
    TEST_ASSERT_EQUAL(0, bus.devices[1].short_addr);
}

void test_device_that_drops_program_is_withdrawn_and_skipped() {
    DeviceCommissionBus bus;
    bus.devices = {{0x0100, 0, -1, false, false, true}, {0x0200}};
    DeviceAddressing addressing{bus};
    const auto result = addressing.commission(CommissionMode::All, sa(0), 2);
    TEST_ASSERT_EQUAL(TransportStatus::Ok, result.status);
    // The unprogrammable unit is withdrawn without consuming an address.
    TEST_ASSERT_EQUAL_UINT32(1, result.count);
    TEST_ASSERT_EQUAL(-1, bus.devices[0].short_addr);
    TEST_ASSERT_EQUAL(0, bus.devices[1].short_addr);
    TEST_ASSERT_TRUE(bus.devices[0].withdrawn);
}

void test_empty_bus_commissions_nothing() {
    DeviceCommissionBus bus;
    DeviceAddressing addressing{bus};
    const auto result = addressing.commission(CommissionMode::All, sa(0), 64);
    TEST_ASSERT_EQUAL(TransportStatus::Ok, result.status);
    TEST_ASSERT_EQUAL_UINT32(0, result.count);
    TEST_ASSERT_EQUAL_UINT32(1, bus.compares);
    TEST_ASSERT_EQUAL_UINT32(1, bus.quiescent_starts);
    TEST_ASSERT_EQUAL_UINT32(1, bus.quiescent_stops);
}

}

void run_device_addressing_tests() {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_commissions_all_devices_in_random_order);
    RUN_TEST(test_unaddressed_mode_only_initialises_unaddressed_devices);
    RUN_TEST(test_compare_collision_counts_as_yes);
    RUN_TEST(test_device_that_drops_program_is_withdrawn_and_skipped);
    RUN_TEST(test_empty_bus_commissions_nothing);
}
