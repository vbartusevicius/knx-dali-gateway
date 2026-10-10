// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2025 the knx-dali-gateway contributors
#include <unity.h>

#include <optional>
#include <vector>

#include "dali/GearAddressing.hpp"
#include "dali/SpecialCommands.hpp"
#include "dali/Transport.hpp"

namespace {

using gateway::dali::ByteRead;
using gateway::dali::CommissionMode;
using gateway::dali::CommissionResult;
using gateway::dali::GearAddressing;
using gateway::dali::ShortAddress;
using gateway::dali::TransportStatus;
namespace special = gateway::dali::special;

ShortAddress sa(unsigned value) { return *ShortAddress::from(value); }

// In-memory bus model for Part 102 addressing: each gear tracks the
// initialising/withdrawn flags, its search address and its short address,
// exactly like a ballast would; several units answering COMPARE produce a
// backward collision.
struct SimGearState {
    std::uint32_t random;
    std::uint32_t search = 0;
    int short_addr = -1;
    bool initialising = false;
    bool withdrawn = false;
    bool drops_program = false;
};

class CommissionBus final : public gateway::dali::Transport {
public:
    std::vector<SimGearState> gear;
    std::vector<std::pair<std::uint8_t, std::uint8_t>> specials;
    std::vector<std::pair<std::uint8_t, std::uint8_t>> device_commands;
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
    gateway::dali::CommissionResult commission(gateway::dali::CommissionMode,
                                               gateway::dali::ShortAddress,
                                               unsigned) override {
        return {TransportStatus::Unsupported, 0};
    }
    TransportStatus identify(gateway::dali::ShortAddress) override {
        return TransportStatus::Ok;
    }
    TransportStatus clear_short_addresses() override { return TransportStatus::Ok; }

    TransportStatus send_special(std::uint8_t opcode, std::uint8_t data,
                                 bool /*twice*/) override {
        specials.push_back({opcode, data});
        switch (opcode) {
        case special::terminate:
            // Ends special mode; `withdrawn` stays observable so tests can
            // see which units were withdrawn during the session — a later
            // INITIALISE resets it, like real gear.
            for (auto& g : gear) {
                g.initialising = false;
            }
            break;
        case special::initialise:
            for (auto& g : gear) {
                if (data == 0x00 || (data == 0xFF && g.short_addr < 0)) {
                    g.initialising = true;
                    g.withdrawn = false;
                }
            }
            break;
        case special::searchaddr_h:
        case special::searchaddr_m:
        case special::searchaddr_l: {
            const int shift = opcode == special::searchaddr_h   ? 16
                              : opcode == special::searchaddr_m ? 8
                                                                : 0;
            for (auto& g : gear) {
                g.search = (g.search & ~(0xFFu << shift)) |
                           (static_cast<std::uint32_t>(data) << shift);
            }
            break;
        }
        case special::program_short_address:
            for (auto& g : gear) {
                if (g.initialising && !g.drops_program &&
                    g.random == g.search) {
                    g.short_addr = data >> 1;
                }
            }
            break;
        case special::withdraw:
            for (auto& g : gear) {
                if (g.initialising && g.random == g.search) {
                    g.withdrawn = true;
                }
            }
            break;
        default:
            break;
        }
        return TransportStatus::Ok;
    }

    ByteRead query_special(std::uint8_t opcode, std::uint8_t data) override {
        unsigned responders = 0;
        if (opcode == special::compare) {
            ++compares;
            for (const auto& g : gear) {
                if (g.initialising && !g.withdrawn && g.random <= g.search) {
                    ++responders;
                }
            }
        } else if (opcode == special::verify_short_address) {
            for (const auto& g : gear) {
                if (g.initialising && g.short_addr == (data >> 1)) {
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

    TransportStatus send_device_frame(std::uint32_t frame, bool /*twice*/) override {
        const auto destination = static_cast<std::uint8_t>(frame >> 16);
        const auto instance = static_cast<std::uint8_t>(frame >> 8);
        const auto command = static_cast<std::uint8_t>(frame);
        device_commands.push_back({destination, command});
        if (destination == gateway::dali::device::address_broadcast &&
            instance == gateway::dali::device::instance_device) {
            if (command == gateway::dali::device::cmd::start_quiescent_mode) {
                ++quiescent_starts;
            } else if (command == gateway::dali::device::cmd::stop_quiescent_mode) {
                ++quiescent_stops;
            }
        }
        return TransportStatus::Ok;
    }
};

unsigned special_count(const CommissionBus& bus, std::uint8_t opcode) {
    unsigned count = 0;
    for (const auto& call : bus.specials) {
        if (call.first == opcode) {
            ++count;
        }
    }
    return count;
}

void test_commissions_all_gear_in_random_order() {
    CommissionBus bus;
    bus.gear = {{0x0300}, {0x0050}, {0x0200}, {0x0100}};
    GearAddressing addressing{bus};
    const auto result = addressing.commission(CommissionMode::All, sa(0), 4);
    TEST_ASSERT_EQUAL(TransportStatus::Ok, result.status);
    TEST_ASSERT_EQUAL_UINT32(4, result.count);
    TEST_ASSERT_EQUAL(0, bus.gear[1].short_addr);  // lowest random first
    TEST_ASSERT_EQUAL(1, bus.gear[3].short_addr);
    TEST_ASSERT_EQUAL(2, bus.gear[2].short_addr);
    TEST_ASSERT_EQUAL(3, bus.gear[0].short_addr);
    TEST_ASSERT_EQUAL_UINT8(special::terminate, bus.specials[0].first);
    TEST_ASSERT_EQUAL_UINT8(special::initialise, bus.specials[1].first);
    TEST_ASSERT_EQUAL_UINT8(0x00, bus.specials[1].second);  // all gear
    TEST_ASSERT_EQUAL_UINT32(2, special_count(bus, special::terminate));
    TEST_ASSERT_EQUAL_UINT32(4, special_count(bus, special::withdraw));
    TEST_ASSERT_EQUAL_UINT32(4, special_count(bus, special::program_short_address));
    TEST_ASSERT_EQUAL_UINT32(1, bus.quiescent_starts);
    TEST_ASSERT_EQUAL_UINT32(1, bus.quiescent_stops);
}

void test_unaddressed_mode_only_initialises_unaddressed_gear() {
    CommissionBus bus;
    bus.gear = {{0x0100}, {0x0200}};
    bus.gear[0].short_addr = 7;  // already addressed: stays out
    GearAddressing addressing{bus};
    const auto result = addressing.commission(CommissionMode::Unaddressed, sa(1), 5);
    TEST_ASSERT_EQUAL(TransportStatus::Ok, result.status);
    TEST_ASSERT_EQUAL_UINT32(1, result.count);
    TEST_ASSERT_EQUAL(7, bus.gear[0].short_addr);
    TEST_ASSERT_EQUAL(1, bus.gear[1].short_addr);
    bool saw_unaddressed_init = false;
    for (const auto& call : bus.specials) {
        if (call.first == special::initialise) {
            TEST_ASSERT_EQUAL_UINT8(0xFF, call.second);
            saw_unaddressed_init = true;
        }
    }
    TEST_ASSERT_TRUE(saw_unaddressed_init);
}

void test_compare_collision_counts_as_yes() {
    CommissionBus bus;
    // Two units with the same random address collide on every COMPARE;
    // the search must still converge on them.
    bus.gear = {{0x0800}, {0x0800}};
    GearAddressing addressing{bus};
    const auto result = addressing.commission(CommissionMode::All, sa(0), 2);
    TEST_ASSERT_EQUAL(TransportStatus::Ok, result.status);
    TEST_ASSERT_EQUAL_UINT32(1, result.count);
    TEST_ASSERT_EQUAL(0, bus.gear[0].short_addr);
    TEST_ASSERT_EQUAL(0, bus.gear[1].short_addr);
}

void test_gear_that_drops_program_is_withdrawn_and_skipped() {
    CommissionBus bus;
    bus.gear = {{0x0100, 0, -1, false, false, true}, {0x0200}};
    GearAddressing addressing{bus};
    const auto result = addressing.commission(CommissionMode::All, sa(0), 2);
    TEST_ASSERT_EQUAL(TransportStatus::Ok, result.status);
    // The unprogrammable unit is withdrawn without consuming an address.
    TEST_ASSERT_EQUAL_UINT32(1, result.count);
    TEST_ASSERT_EQUAL(-1, bus.gear[0].short_addr);
    TEST_ASSERT_EQUAL(0, bus.gear[1].short_addr);
    TEST_ASSERT_TRUE(bus.gear[0].withdrawn);
}

void test_empty_bus_commissions_nothing() {
    CommissionBus bus;
    GearAddressing addressing{bus};
    const auto result = addressing.commission(CommissionMode::All, sa(0), 64);
    TEST_ASSERT_EQUAL(TransportStatus::Ok, result.status);
    TEST_ASSERT_EQUAL_UINT32(0, result.count);
    TEST_ASSERT_EQUAL_UINT32(1, bus.compares);  // one probe, then done
    TEST_ASSERT_EQUAL_UINT32(1, bus.quiescent_starts);
    TEST_ASSERT_EQUAL_UINT32(1, bus.quiescent_stops);
}

}

void run_gear_addressing_tests() {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_commissions_all_gear_in_random_order);
    RUN_TEST(test_unaddressed_mode_only_initialises_unaddressed_gear);
    RUN_TEST(test_compare_collision_counts_as_yes);
    RUN_TEST(test_gear_that_drops_program_is_withdrawn_and_skipped);
    RUN_TEST(test_empty_bus_commissions_nothing);
}
