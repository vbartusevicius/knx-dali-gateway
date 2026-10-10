#include <deque>
#include <optional>

#include <unity.h>

#include "dali/RetryingTransport.hpp"

namespace {

using gateway::dali::Address;
using gateway::dali::ArcLevel;
using gateway::dali::GearCommand;
using gateway::dali::Group;
using gateway::dali::Scene;
using gateway::dali::ShortAddress;
using gateway::dali::TransportStatus;

ShortAddress channel(unsigned value) {
    return *ShortAddress::from(value);
}

// Scripted transport counting attempts per operation.
class AttemptTransport final : public gateway::dali::Transport {
public:
    std::deque<TransportStatus> write_script;
    std::deque<TransportStatus> membership_script;
    std::deque<gateway::dali::LevelRead> read_script;
    std::deque<gateway::dali::GroupsRead> groups_script;
    std::deque<gateway::dali::GearPresence> presence_script;
    std::deque<gateway::dali::GearStatusRead> status_script;
    std::deque<gateway::dali::CommissionResult> commission_script;
    std::deque<TransportStatus> identify_script;
    std::deque<gateway::dali::ByteRead> query_script;
    unsigned writes = 0;
    unsigned memberships = 0;
    unsigned reads = 0;
    unsigned groups_reads = 0;
    unsigned presences = 0;
    unsigned statuses = 0;
    unsigned commissions = 0;
    unsigned identifies = 0;

    TransportStatus set_level(Address, ArcLevel) override {
        ++writes;
        const auto status = write_script.front();
        write_script.pop_front();
        return status;
    }
    TransportStatus send(Address, GearCommand) override {
        ++writes;
        const auto status = write_script.front();
        write_script.pop_front();
        return status;
    }
    TransportStatus go_to_scene(Address, Scene) override {
        ++writes;
        const auto status = write_script.front();
        write_script.pop_front();
        return status;
    }
    TransportStatus send_command(Address, std::uint8_t, bool) override {
        ++writes;
        const auto status = write_script.front();
        write_script.pop_front();
        return status;
    }
    TransportStatus set_dtr(gateway::dali::Dtr, std::uint8_t) override {
        ++writes;
        const auto status = write_script.front();
        write_script.pop_front();
        return status;
    }
    TransportStatus enable_device_type(std::uint8_t) override {
        ++writes;
        const auto status = write_script.front();
        write_script.pop_front();
        return status;
    }
    gateway::dali::ByteRead query(ShortAddress, std::uint8_t) override {
        ++reads;
        const auto result = query_script.front();
        query_script.pop_front();
        return result;
    }
    gateway::dali::LevelRead read_level(ShortAddress) override {
        ++reads;
        const auto result = read_script.front();
        read_script.pop_front();
        return result;
    }
    gateway::dali::GroupsRead read_groups(ShortAddress) override {
        ++groups_reads;
        const auto result = groups_script.front();
        groups_script.pop_front();
        return result;
    }
    TransportStatus set_group_membership(ShortAddress, Group, bool) override {
        ++memberships;
        const auto status = membership_script.front();
        membership_script.pop_front();
        return status;
    }
    gateway::dali::GearPresence query_present(ShortAddress) override {
        ++presences;
        const auto result = presence_script.front();
        presence_script.pop_front();
        return result;
    }
    gateway::dali::GearStatusRead query_status(ShortAddress) override {
        ++statuses;
        const auto result = status_script.front();
        status_script.pop_front();
        return result;
    }
    gateway::dali::CommissionResult commission(gateway::dali::CommissionMode, ShortAddress,
                                               unsigned) override {
        ++commissions;
        const auto result = commission_script.front();
        commission_script.pop_front();
        return result;
    }
    TransportStatus identify(ShortAddress) override {
        ++identifies;
        const auto status = identify_script.front();
        identify_script.pop_front();
        return status;
    }
    TransportStatus clear_short_addresses() override {
        ++writes;
        const auto status = write_script.front();
        write_script.pop_front();
        return status;
    }
};

gateway::dali::RetryingTransport retrying(AttemptTransport& inner) {
    return gateway::dali::RetryingTransport{inner};
}

void test_writes_never_retry() {
    AttemptTransport inner;
    // Writes pass every status through after a single call: collision
    // recovery is the link layer's job, and multiplying its attempts here
    // would hide the bounded recovery the bus layer already performs.
    inner.write_script = {TransportStatus::Collision, TransportStatus::Ok};
    auto transport = retrying(inner);
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Collision),
                      static_cast<int>(transport.set_level(Address::broadcast(), *ArcLevel::from(10))));
    TEST_ASSERT_EQUAL_UINT32(1, inner.writes);

    inner.write_script = {TransportStatus::Timeout, TransportStatus::Ok};
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Timeout),
                      static_cast<int>(transport.send(Address::broadcast(), GearCommand::Off)));
    TEST_ASSERT_EQUAL_UINT32(2, inner.writes);

    inner.write_script = {TransportStatus::InvalidResponse, TransportStatus::Ok};
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::InvalidResponse),
                      static_cast<int>(transport.go_to_scene(Address::broadcast(), *Scene::from(0))));
    TEST_ASSERT_EQUAL_UINT32(3, inner.writes);
}

void test_write_collision_returns_after_one_attempt() {
    AttemptTransport inner;
    inner.write_script = {TransportStatus::Collision, TransportStatus::Collision,
                          TransportStatus::Collision, TransportStatus::Ok};
    auto transport = retrying(inner);
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Collision),
                      static_cast<int>(transport.send_command(Address::broadcast(), 0x21, true)));
    TEST_ASSERT_EQUAL_UINT32(1, inner.writes);
}

void test_membership_write_does_not_retry_collision() {
    AttemptTransport inner;
    inner.membership_script = {TransportStatus::Collision, TransportStatus::Ok};
    auto transport = retrying(inner);
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Collision),
                      static_cast<int>(transport.set_group_membership(channel(0), *Group::from(0), true)));
    TEST_ASSERT_EQUAL_UINT32(1, inner.memberships);
}

void test_read_retries_transient_statuses_and_returns_last_result() {
    AttemptTransport inner;
    for (const auto status : {TransportStatus::Timeout, TransportStatus::Collision,
                              TransportStatus::InvalidResponse}) {
        inner.reads = 0;
        inner.read_script = {{status, std::nullopt}, {TransportStatus::Ok, ArcLevel::from(42)}};
        auto transport = retrying(inner);
        const auto result = transport.read_level(channel(0));
        TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Ok), static_cast<int>(result.status));
        TEST_ASSERT_EQUAL_UINT8(42, result.level->value());
        TEST_ASSERT_EQUAL_UINT32(2, inner.reads);
    }
}

void test_read_does_not_retry_permanent_statuses() {
    AttemptTransport inner;
    for (const auto status : {TransportStatus::Unconfigured, TransportStatus::Unsupported,
                              TransportStatus::Failure}) {
        inner.reads = 0;
        inner.read_script = {{status, std::nullopt}, {TransportStatus::Ok, ArcLevel::from(42)}};
        auto transport = retrying(inner);
        const auto result = transport.read_level(channel(0));
        TEST_ASSERT_EQUAL(static_cast<int>(status), static_cast<int>(result.status));
        TEST_ASSERT_EQUAL_UINT32(1, inner.reads);
    }
}

void test_read_gives_up_after_three_attempts() {
    AttemptTransport inner;
    inner.read_script = {{TransportStatus::Timeout, std::nullopt},
                         {TransportStatus::Timeout, std::nullopt},
                         {TransportStatus::Timeout, std::nullopt},
                         {TransportStatus::Ok, ArcLevel::from(1)}};
    auto transport = retrying(inner);
    const auto result = transport.read_level(channel(0));
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Timeout), static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_UINT32(3, inner.reads);
}

void test_group_presence_and_status_reads_retry() {
    AttemptTransport inner;
    inner.groups_script = {{TransportStatus::Collision, std::nullopt},
                           {TransportStatus::Ok, std::uint16_t{0x00FF}}};
    inner.presence_script = {{TransportStatus::InvalidResponse, false},
                             {TransportStatus::Ok, true}};
    inner.status_script = {{TransportStatus::Timeout, std::nullopt},
                           {TransportStatus::Ok, std::uint8_t{0x04}}};
    auto transport = retrying(inner);
    TEST_ASSERT_EQUAL_UINT16(0x00FF, *transport.read_groups(channel(0)).groups);
    TEST_ASSERT_EQUAL_UINT32(2, inner.groups_reads);
    TEST_ASSERT_TRUE(transport.query_present(channel(0)).present);
    TEST_ASSERT_EQUAL_UINT32(2, inner.presences);
    TEST_ASSERT_EQUAL_UINT8(0x04, *transport.query_status(channel(0)).gear_status);
    TEST_ASSERT_EQUAL_UINT32(2, inner.statuses);
}

void test_commission_and_identify_are_never_retried() {
    AttemptTransport inner;
    inner.commission_script = {{TransportStatus::Collision, 0}};
    inner.identify_script = {TransportStatus::Collision};
    auto transport = retrying(inner);
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Collision),
                      static_cast<int>(transport.commission(gateway::dali::CommissionMode::All,
                                                            channel(0), 64)
                                           .status));
    TEST_ASSERT_EQUAL_UINT32(1, inner.commissions);
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Collision),
                      static_cast<int>(transport.identify(channel(0))));
    TEST_ASSERT_EQUAL_UINT32(1, inner.identifies);
}

void test_dt8_primitives_follow_read_write_retry_rules() {
    AttemptTransport inner;
    // Query is a read: retries on Timeout.
    inner.query_script = {{TransportStatus::Timeout, std::nullopt},
                          {TransportStatus::Ok, std::uint8_t{0x2A}}};
    // DTR/EDT writes pass Collision and Timeout straight through.
    inner.write_script = {TransportStatus::Collision, TransportStatus::Timeout,
                          TransportStatus::Ok};
    auto transport = retrying(inner);
    const auto answer = transport.query(channel(0), 0xF9);
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Ok), static_cast<int>(answer.status));
    TEST_ASSERT_EQUAL_UINT8(0x2A, *answer.value);
    TEST_ASSERT_EQUAL_UINT32(2, inner.reads);
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Collision),
                      static_cast<int>(transport.set_dtr(gateway::dali::Dtr::Dtr1, 0x10)));
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Timeout),
                      static_cast<int>(transport.enable_device_type(8)));
    TEST_ASSERT_EQUAL_UINT32(2, inner.writes);  // writes never retry
}

}

void run_retry_tests() {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_writes_never_retry);
    RUN_TEST(test_write_collision_returns_after_one_attempt);
    RUN_TEST(test_membership_write_does_not_retry_collision);
    RUN_TEST(test_read_retries_transient_statuses_and_returns_last_result);
    RUN_TEST(test_read_does_not_retry_permanent_statuses);
    RUN_TEST(test_read_gives_up_after_three_attempts);
    RUN_TEST(test_group_presence_and_status_reads_retry);
    RUN_TEST(test_commission_and_identify_are_never_retried);
    RUN_TEST(test_dt8_primitives_follow_read_write_retry_rules);
}
