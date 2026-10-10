#include <unity.h>

#include "DaliSdkFake.hpp"
#include "espidf/DaliTransport.hpp"

namespace {

using gateway::dali::Address;
using gateway::dali::ArcLevel;
using gateway::dali::GearCommand;
using gateway::dali::Group;
using gateway::dali::Scene;
using gateway::dali::ShortAddress;
using gateway::dali::TransportStatus;
using gateway::espidf::DaliTransport;
using gateway::testing::dali_handle;
using gateway::testing::dali_state;
using gateway::testing::reset_dali;

Address channel(unsigned value) {
    return Address::short_address(*ShortAddress::from(value));
}

Address group_address(unsigned value) {
    return Address::group(*Group::from(value));
}

const dali_master_transaction_config_t& only_transaction() {
    TEST_ASSERT_EQUAL_UINT32(1, dali_state().transactions.size());
    return dali_state().transactions.front().config;
}

void test_disabled_dali_adapter_never_calls_sdk() {
    reset_dali();
    DaliTransport transport{nullptr};
    const auto address = *ShortAddress::from(0);
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Unconfigured),
                      static_cast<int>(transport.read_level(address).status));
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Unconfigured),
                      static_cast<int>(transport.read_groups(address).status));
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Unconfigured),
                      static_cast<int>(transport.set_level(channel(0), *ArcLevel::from(254))));
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Unconfigured),
                      static_cast<int>(transport.send(channel(0), GearCommand::Off)));
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Unconfigured),
                      static_cast<int>(transport.go_to_scene(channel(0), *Scene::from(0))));
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Unconfigured),
                      static_cast<int>(transport.store_scene(channel(0), *Scene::from(0))));
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Unconfigured),
                      static_cast<int>(transport.set_group_membership(address, *Group::from(0), true)));
    TEST_ASSERT_EQUAL_UINT32(0, dali_state().transactions.size());
}

void test_dali_adapter_sends_maximum_as_data_not_mask_or_command() {
    reset_dali();
    DaliTransport transport{dali_handle()};
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Ok),
                      static_cast<int>(transport.set_level(channel(63), *ArcLevel::from(254))));
    const auto& config = only_transaction();
    TEST_ASSERT_EQUAL_UINT8(63, config.addr);
    TEST_ASSERT_EQUAL_UINT8(254, config.command);
    TEST_ASSERT_EQUAL(static_cast<int>(DALI_ADDR_SHORT), static_cast<int>(config.addr_type));
    TEST_ASSERT_FALSE(config.is_cmd);
    TEST_ASSERT_FALSE(config.send_twice);
    TEST_ASSERT_FALSE(dali_state().transactions.front().expects_reply);
}

void test_dali_adapter_maps_group_and_broadcast_addresses() {
    reset_dali();
    DaliTransport transport{dali_handle()};
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Ok),
                      static_cast<int>(transport.set_level(group_address(9), *ArcLevel::from(100))));
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Ok),
                      static_cast<int>(transport.set_level(Address::broadcast(), *ArcLevel::from(100))));
    TEST_ASSERT_EQUAL_UINT32(2, dali_state().transactions.size());
    TEST_ASSERT_EQUAL(static_cast<int>(DALI_ADDR_GROUP),
                      static_cast<int>(dali_state().transactions[0].config.addr_type));
    TEST_ASSERT_EQUAL_UINT8(9, dali_state().transactions[0].config.addr);
    TEST_ASSERT_EQUAL(static_cast<int>(DALI_ADDR_BROADCAST),
                      static_cast<int>(dali_state().transactions[1].config.addr_type));
    TEST_ASSERT_FALSE(dali_state().transactions[1].config.is_cmd);
}

void test_dali_adapter_sends_gear_commands_as_commands() {
    reset_dali();
    DaliTransport transport{dali_handle()};
    for (const auto& entry : {std::pair{GearCommand::Off, DALI_CMD_OFF},
                              {GearCommand::Up, DALI_CMD_UP},
                              {GearCommand::Down, DALI_CMD_DOWN},
                              {GearCommand::OnAndStepUp, DALI_CMD_ON_AND_STEP_UP}}) {
        dali_state().transactions.clear();
        TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Ok),
                          static_cast<int>(transport.send(channel(0), entry.first)));
        const auto& config = only_transaction();
        TEST_ASSERT_TRUE(config.is_cmd);
        TEST_ASSERT_EQUAL_UINT8(entry.second, config.command);
        TEST_ASSERT_FALSE(config.send_twice);
    }
}

void test_dali_adapter_goes_to_scene() {
    reset_dali();
    DaliTransport transport{dali_handle()};
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Ok),
                      static_cast<int>(transport.go_to_scene(group_address(4), *Scene::from(15))));
    const auto& config = only_transaction();
    TEST_ASSERT_TRUE(config.is_cmd);
    TEST_ASSERT_EQUAL_UINT8(DALI_CMD_GO_TO_SCENE_0 + 15, config.command);
    TEST_ASSERT_EQUAL(static_cast<int>(DALI_ADDR_GROUP), static_cast<int>(config.addr_type));
    TEST_ASSERT_EQUAL_UINT8(4, config.addr);
    TEST_ASSERT_FALSE(config.send_twice);
}

void test_dali_adapter_stores_scene_as_two_send_twice_frames() {
    reset_dali();
    DaliTransport transport{dali_handle()};
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Ok),
                      static_cast<int>(transport.store_scene(channel(3), *Scene::from(5))));
    TEST_ASSERT_EQUAL_UINT32(2, dali_state().transactions.size());
    const auto& first = dali_state().transactions[0].config;
    const auto& second = dali_state().transactions[1].config;
    TEST_ASSERT_EQUAL_UINT8(DALI_CMD_STORE_ACTUAL_LEVEL, first.command);
    TEST_ASSERT_TRUE(first.is_cmd);
    TEST_ASSERT_TRUE(first.send_twice);
    TEST_ASSERT_EQUAL_UINT8(DALI_CMD_STORE_DTR_AS_SCENE_0 + 5, second.command);
    TEST_ASSERT_TRUE(second.is_cmd);
    TEST_ASSERT_TRUE(second.send_twice);
    TEST_ASSERT_EQUAL_UINT8(3, second.addr);
}

void test_dali_adapter_aborts_store_scene_when_dtr_write_fails() {
    reset_dali();
    DaliTransport transport{dali_handle()};
    dali_state().transaction_error = ESP_ERR_TIMEOUT;
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Timeout),
                      static_cast<int>(transport.store_scene(channel(0), *Scene::from(0))));
    TEST_ASSERT_EQUAL_UINT32(1, dali_state().transactions.size());
}

void test_dali_adapter_queries_actual_level() {
    reset_dali();
    dali_state().reply = 0;
    DaliTransport transport{dali_handle()};
    const auto reply = transport.read_level(*ShortAddress::from(0));
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Ok), static_cast<int>(reply.status));
    TEST_ASSERT_TRUE(reply.level.has_value());
    TEST_ASSERT_EQUAL_UINT8(0, reply.level->value());
    const auto& config = only_transaction();
    TEST_ASSERT_TRUE(config.is_cmd);
    TEST_ASSERT_EQUAL_UINT8(DALI_CMD_QUERY_ACTUAL_LEVEL, config.command);
    TEST_ASSERT_TRUE(dali_state().transactions.front().expects_reply);
}

void test_dali_adapter_combines_both_group_queries() {
    reset_dali();
    dali_state().replies = {0x21, 0x81};
    DaliTransport transport{dali_handle()};
    const auto reply = transport.read_groups(*ShortAddress::from(0));
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Ok), static_cast<int>(reply.status));
    TEST_ASSERT_TRUE(reply.groups.has_value());
    TEST_ASSERT_EQUAL_UINT16(0x8121, *reply.groups);
    TEST_ASSERT_EQUAL_UINT32(2, dali_state().transactions.size());
    TEST_ASSERT_EQUAL_UINT8(DALI_CMD_QUERY_GROUPS_0_7, dali_state().transactions[0].config.command);
    TEST_ASSERT_EQUAL_UINT8(DALI_CMD_QUERY_GROUPS_8_15, dali_state().transactions[1].config.command);
    TEST_ASSERT_TRUE(dali_state().transactions[0].expects_reply);
}

void test_dali_adapter_read_groups_failure_reports_status_without_mask() {
    reset_dali();
    dali_state().replies = {0x00, DALI_RESULT_NO_REPLY};
    DaliTransport transport{dali_handle()};
    const auto reply = transport.read_groups(*ShortAddress::from(0));
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Timeout), static_cast<int>(reply.status));
    TEST_ASSERT_FALSE(reply.groups.has_value());
}

void test_dali_adapter_sets_group_membership_send_twice() {
    reset_dali();
    DaliTransport transport{dali_handle()};
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Ok),
                      static_cast<int>(transport.set_group_membership(*ShortAddress::from(7), *Group::from(9), true)));
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Ok),
                      static_cast<int>(transport.set_group_membership(*ShortAddress::from(7), *Group::from(9), false)));
    TEST_ASSERT_EQUAL_UINT32(2, dali_state().transactions.size());
    const auto& added = dali_state().transactions[0].config;
    TEST_ASSERT_EQUAL_UINT8(DALI_CMD_ADD_TO_GROUP_0 + 9, added.command);
    TEST_ASSERT_TRUE(added.send_twice);
    TEST_ASSERT_EQUAL_UINT8(7, added.addr);
    const auto& removed = dali_state().transactions[1].config;
    TEST_ASSERT_EQUAL_UINT8(DALI_CMD_REMOVE_FROM_GROUP_0 + 9, removed.command);
    TEST_ASSERT_TRUE(removed.send_twice);
}

void test_dali_adapter_does_not_classify_timeout_as_unsupported() {
    reset_dali();
    dali_state().reply = DALI_RESULT_NO_REPLY;
    DaliTransport transport{dali_handle()};
    const auto reply = transport.read_level(*ShortAddress::from(0));
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Timeout), static_cast<int>(reply.status));
    TEST_ASSERT_FALSE(reply.level.has_value());
}

void test_dali_adapter_rejects_invalid_actual_levels() {
    reset_dali();
    DaliTransport transport{dali_handle()};
    for (int raw : {255, 256, -2}) {
        dali_state().transactions.clear();
        dali_state().reply = raw;
        const auto reply = transport.read_level(*ShortAddress::from(0));
        TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::InvalidResponse), static_cast<int>(reply.status));
        TEST_ASSERT_FALSE(reply.level.has_value());
    }
}

void test_dali_adapter_backend_errors_are_not_gear_capabilities() {
    reset_dali();
    dali_state().transaction_error = ESP_ERR_NOT_SUPPORTED;
    DaliTransport transport{dali_handle()};
    const auto reply = transport.read_level(*ShortAddress::from(0));
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Failure), static_cast<int>(reply.status));
}

void test_dali_adapter_releases_driver_once() {
    reset_dali();
    {
        DaliTransport transport{dali_handle()};
        TEST_ASSERT_TRUE(transport.configured());
    }
    TEST_ASSERT_EQUAL_UINT32(1, dali_state().deletions);
}

}

void run_dali_adapter_tests() {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_disabled_dali_adapter_never_calls_sdk);
    RUN_TEST(test_dali_adapter_sends_maximum_as_data_not_mask_or_command);
    RUN_TEST(test_dali_adapter_maps_group_and_broadcast_addresses);
    RUN_TEST(test_dali_adapter_sends_gear_commands_as_commands);
    RUN_TEST(test_dali_adapter_goes_to_scene);
    RUN_TEST(test_dali_adapter_stores_scene_as_two_send_twice_frames);
    RUN_TEST(test_dali_adapter_aborts_store_scene_when_dtr_write_fails);
    RUN_TEST(test_dali_adapter_queries_actual_level);
    RUN_TEST(test_dali_adapter_combines_both_group_queries);
    RUN_TEST(test_dali_adapter_read_groups_failure_reports_status_without_mask);
    RUN_TEST(test_dali_adapter_sets_group_membership_send_twice);
    RUN_TEST(test_dali_adapter_does_not_classify_timeout_as_unsupported);
    RUN_TEST(test_dali_adapter_rejects_invalid_actual_levels);
    RUN_TEST(test_dali_adapter_backend_errors_are_not_gear_capabilities);
    RUN_TEST(test_dali_adapter_releases_driver_once);
}
