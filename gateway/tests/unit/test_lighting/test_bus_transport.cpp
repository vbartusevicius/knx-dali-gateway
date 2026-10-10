// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2025 the knx-dali-gateway contributors
#include <unity.h>

#include <deque>
#include <optional>
#include <vector>

#include "dali/BusTransport.hpp"
#include "dali/SpecialCommands.hpp"

namespace {

using gateway::dali::Address;
using gateway::dali::ArcLevel;
using gateway::dali::BusEvent;
using gateway::dali::BusRequest;
using gateway::dali::BusTransport;
using gateway::dali::Dtr;
using gateway::dali::GearCommand;
using gateway::dali::Group;
using gateway::dali::Scene;
using gateway::dali::ShortAddress;
using gateway::dali::TransportStatus;
using gateway::dali::bus::TxOutcome;
using gateway::dali::bus::TxResult;

ShortAddress sa(unsigned value) { return *ShortAddress::from(value); }
Group grp(unsigned value) { return *Group::from(value); }
Scene scene(unsigned value) { return *Scene::from(value); }

class FakeBusPort final : public gateway::dali::BusPort {
public:
    std::deque<TxResult> scripted;
    TxResult fallback{TxOutcome::Sent, 0};
    std::vector<BusRequest> requests;
    std::deque<BusEvent> events;

    TxResult transact(const BusRequest& request) override {
        requests.push_back(request);
        if (!scripted.empty()) {
            const auto result = scripted.front();
            scripted.pop_front();
            return result;
        }
        return fallback;
    }

    std::optional<BusEvent> poll_event() override {
        if (events.empty()) {
            return std::nullopt;
        }
        const auto event = events.front();
        events.pop_front();
        return event;
    }
};

void test_set_level_sends_dapc_frame() {
    FakeBusPort port;
    BusTransport transport{port};
    TEST_ASSERT_EQUAL(TransportStatus::Ok,
                      transport.set_level(Address::short_address(sa(5)),
                                          *ArcLevel::from(128)));
    TEST_ASSERT_EQUAL_size_t(1, port.requests.size());
    TEST_ASSERT_EQUAL_UINT32(0x0A80, port.requests[0].value);
    TEST_ASSERT_EQUAL_UINT8(16, port.requests[0].bits);
    TEST_ASSERT_FALSE(port.requests[0].expect_reply);
    TEST_ASSERT_EQUAL_UINT32(0, port.requests[0].repeat_gap_us);
}

void test_send_twice_requests_repeat_gap() {
    FakeBusPort port;
    BusTransport transport{port};
    transport.set_group_membership(sa(0), grp(3), true);
    TEST_ASSERT_EQUAL_size_t(1, port.requests.size());
    TEST_ASSERT_EQUAL_UINT32(0x0163, port.requests[0].value);
    TEST_ASSERT_TRUE(port.requests[0].repeat_gap_us > 0);
}

void test_query_maps_link_outcomes() {
    FakeBusPort port;
    BusTransport transport{port};

    port.scripted.push_back({TxOutcome::Reply, 0x42});
    const auto ok = transport.query(sa(1), 0xA0);
    TEST_ASSERT_EQUAL(TransportStatus::Ok, ok.status);
    TEST_ASSERT_EQUAL_UINT8(0x42, *ok.value);
    TEST_ASSERT_TRUE(port.requests[0].expect_reply);

    port.scripted.push_back({TxOutcome::NoReply, 0});
    TEST_ASSERT_EQUAL(TransportStatus::Timeout, transport.query(sa(1), 0xA0).status);

    port.scripted.push_back({TxOutcome::ReplyCollision, 0});
    TEST_ASSERT_EQUAL(TransportStatus::Collision,
                      transport.query(sa(1), 0xA0).status);

    port.scripted.push_back({TxOutcome::Collision, 0});
    TEST_ASSERT_EQUAL(TransportStatus::Collision,
                      transport.query(sa(1), 0xA0).status);
}

void test_special_commands_use_special_frames() {
    FakeBusPort port;
    BusTransport transport{port};
    transport.set_dtr(Dtr::Dtr0, 0x80);
    transport.set_dtr(Dtr::Dtr1, 0x10);
    transport.set_dtr(Dtr::Dtr2, 0x20);
    transport.enable_device_type(8);
    TEST_ASSERT_EQUAL_UINT32(0xA380, port.requests[0].value);
    TEST_ASSERT_EQUAL_UINT32(0xC310, port.requests[1].value);
    TEST_ASSERT_EQUAL_UINT32(0xC520, port.requests[2].value);
    TEST_ASSERT_EQUAL_UINT32(0xC108, port.requests[3].value);

    transport.send_special(gateway::dali::special::searchaddr_h, 0xAB, false);
    TEST_ASSERT_EQUAL_UINT32(0xB1AB, port.requests[4].value);

    port.scripted.push_back({TxOutcome::ReplyCollision, 0});
    const auto compare = transport.query_special(
        gateway::dali::special::compare, 0x00);
    TEST_ASSERT_EQUAL(TransportStatus::Collision, compare.status);
    TEST_ASSERT_EQUAL_UINT32(0xA900, port.requests[5].value);
}

void test_device_command_sends_24bit_frame() {
    FakeBusPort port;
    BusTransport transport{port};
    transport.send_device_command(0xFF, gateway::dali::device::cmd::start_quiescent_mode,
                                  true);
    TEST_ASSERT_EQUAL_size_t(1, port.requests.size());
    TEST_ASSERT_EQUAL_UINT32(0xFFFE1D, port.requests[0].value);
    TEST_ASSERT_EQUAL_UINT8(24, port.requests[0].bits);
    TEST_ASSERT_TRUE(port.requests[0].repeat_gap_us > 0);
}

void test_poll_event_forwards_port_events() {
    FakeBusPort port;
    BusTransport transport{port};
    TEST_ASSERT_FALSE(transport.poll_event().has_value());
    port.events.push_back(BusEvent{0xABCDEF, 24});
    const auto event = transport.poll_event();
    TEST_ASSERT_TRUE(event.has_value());
    TEST_ASSERT_EQUAL_UINT32(0xABCDEF, event->value);
    TEST_ASSERT_EQUAL_UINT8(24, event->bits);
}

void test_read_level_maps_byte_to_arc() {
    FakeBusPort port;
    BusTransport transport{port};
    port.scripted.push_back({TxOutcome::Reply, 128});
    const auto level = transport.read_level(sa(2));
    TEST_ASSERT_EQUAL(TransportStatus::Ok, level.status);
    TEST_ASSERT_EQUAL_UINT8(128, level.level->value());
    TEST_ASSERT_EQUAL_UINT32(0x05A0, port.requests[0].value);
}

void test_clear_short_addresses_sends_dtr_then_broadcast_store() {
    FakeBusPort port;
    BusTransport transport{port};
    TEST_ASSERT_EQUAL(TransportStatus::Ok, transport.clear_short_addresses());
    TEST_ASSERT_EQUAL_size_t(2, port.requests.size());
    TEST_ASSERT_EQUAL_UINT32(0xA3FF, port.requests[0].value);
    TEST_ASSERT_EQUAL_UINT32(0xFF80, port.requests[1].value);
    TEST_ASSERT_TRUE(port.requests[1].repeat_gap_us > 0);
}

void test_commission_runs_gear_addressing_sequence() {
    FakeBusPort port;
    BusTransport transport{port};
    // No gear answers COMPARE: quiescent start, terminate, initialise,
    // randomise, three SEARCHADDR loads, COMPARE timeout, terminate,
    // quiescent stop.
    for (int i = 0; i < 7; ++i) {
        port.scripted.push_back({TxOutcome::Sent, 0});
    }
    port.scripted.push_back({TxOutcome::NoReply, 0});  // COMPARE
    const auto result = transport.commission(gateway::dali::CommissionMode::All,
                                             sa(0), 4);
    TEST_ASSERT_EQUAL(TransportStatus::Ok, result.status);
    TEST_ASSERT_EQUAL_UINT32(0, result.count);
    TEST_ASSERT_EQUAL_size_t(10, port.requests.size());
    TEST_ASSERT_EQUAL_UINT32(0xFFFE1D, port.requests[0].value);
    TEST_ASSERT_EQUAL_UINT8(24, port.requests[0].bits);
    TEST_ASSERT_EQUAL_UINT32(0xA100, port.requests[1].value);
    TEST_ASSERT_EQUAL_UINT32(0xA500, port.requests[2].value);
    TEST_ASSERT_EQUAL_UINT32(0xA700, port.requests[3].value);
    TEST_ASSERT_EQUAL_UINT32(0xB1FF, port.requests[4].value);
    TEST_ASSERT_EQUAL_UINT32(0xB3FF, port.requests[5].value);
    TEST_ASSERT_EQUAL_UINT32(0xB5FF, port.requests[6].value);
    TEST_ASSERT_EQUAL_UINT32(0xA900, port.requests[7].value);
    TEST_ASSERT_EQUAL_UINT32(0xA100, port.requests[8].value);
    TEST_ASSERT_EQUAL_UINT32(0xFFFE1E, port.requests[9].value);
}

void test_scenes_groups_and_identify_frames() {
    FakeBusPort port;
    BusTransport transport{port};
    transport.go_to_scene(Address::group(grp(15)), scene(2));
    transport.identify(sa(3));
    transport.send(Address::broadcast(), GearCommand::Off);
    TEST_ASSERT_EQUAL_UINT32(0x9F12, port.requests[0].value);
    TEST_ASSERT_EQUAL_UINT32(0x0705, port.requests[1].value);
    TEST_ASSERT_EQUAL_UINT32(0xFF00, port.requests[2].value);
}

}

void run_bus_transport_tests() {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_set_level_sends_dapc_frame);
    RUN_TEST(test_send_twice_requests_repeat_gap);
    RUN_TEST(test_query_maps_link_outcomes);
    RUN_TEST(test_special_commands_use_special_frames);
    RUN_TEST(test_device_command_sends_24bit_frame);
    RUN_TEST(test_poll_event_forwards_port_events);
    RUN_TEST(test_read_level_maps_byte_to_arc);
    RUN_TEST(test_clear_short_addresses_sends_dtr_then_broadcast_store);
    RUN_TEST(test_commission_runs_gear_addressing_sequence);
    RUN_TEST(test_scenes_groups_and_identify_frames);
}
