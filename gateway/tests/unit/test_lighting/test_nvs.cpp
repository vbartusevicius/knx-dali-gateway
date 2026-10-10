#include <unity.h>

#include "NvsSdkFake.hpp"
#include "espidf/NvsLevelStore.hpp"

namespace {

using gateway::dali::Address;
using gateway::dali::ArcLevel;
using gateway::dali::Group;
using gateway::dali::ShortAddress;
using gateway::espidf::NvsLevelStore;
using gateway::storage::ReadStatus;
using gateway::storage::Slot;
using gateway::storage::SlotKind;
using gateway::testing::nvs_state;
using gateway::testing::reset_nvs;

Address channel(unsigned value) {
    return Address::short_address(*ShortAddress::from(value));
}

Address group_address(unsigned value) {
    return Address::group(*Group::from(value));
}

ArcLevel level(unsigned value) {
    return *ArcLevel::from(value);
}

Slot slot(SlotKind kind, Address address) {
    return {kind, address};
}

void test_nvs_missing_target_is_not_a_storage_failure() {
    reset_nvs();
    NvsLevelStore store{1};
    const auto result = store.load(slot(SlotKind::Target, channel(0)));
    TEST_ASSERT_EQUAL(static_cast<int>(ReadStatus::NotFound), static_cast<int>(result.status));
    TEST_ASSERT_FALSE(result.level.has_value());
}

void test_nvs_rejects_stored_mask_value() {
    reset_nvs();
    nvs_state().committed["arc_00"] = 255;
    NvsLevelStore store{1};
    const auto result = store.load(slot(SlotKind::Target, channel(0)));
    TEST_ASSERT_EQUAL(static_cast<int>(ReadStatus::Failure), static_cast<int>(result.status));
    TEST_ASSERT_FALSE(result.level.has_value());
}

void test_nvs_unavailable_handle_does_not_write() {
    reset_nvs();
    NvsLevelStore store{0};
    TEST_ASSERT_FALSE(store.save(slot(SlotKind::Target, channel(0)), level(0)));
    TEST_ASSERT_FALSE(store.commit());
    TEST_ASSERT_EQUAL_UINT32(0, nvs_state().writes);
    TEST_ASSERT_EQUAL_UINT32(0, nvs_state().commits);
}

void test_nvs_save_only_stages_until_commit() {
    reset_nvs();
    NvsLevelStore store{1};
    TEST_ASSERT_TRUE(store.save(slot(SlotKind::Target, channel(63)), level(0)));
    TEST_ASSERT_EQUAL_UINT32(1, nvs_state().writes);
    TEST_ASSERT_EQUAL_UINT32(0, nvs_state().commits);
    TEST_ASSERT_TRUE(nvs_state().committed.find("arc_63") == nvs_state().committed.end());
    TEST_ASSERT_TRUE(store.commit());
    TEST_ASSERT_EQUAL_UINT8(0, nvs_state().committed.at("arc_63"));
    TEST_ASSERT_EQUAL_UINT32(1, nvs_state().commits);
}

void test_nvs_commit_is_a_no_op_without_staged_writes() {
    reset_nvs();
    NvsLevelStore store{1};
    TEST_ASSERT_TRUE(store.commit());
    TEST_ASSERT_EQUAL_UINT32(0, nvs_state().commits);
}

void test_nvs_retries_commit_after_a_failed_commit() {
    reset_nvs();
    NvsLevelStore store{1};
    nvs_state().commits_succeed = false;
    TEST_ASSERT_TRUE(store.save(slot(SlotKind::Target, channel(0)), level(120)));
    TEST_ASSERT_FALSE(store.commit());
    nvs_state().commits_succeed = true;
    TEST_ASSERT_TRUE(store.commit());
    TEST_ASSERT_EQUAL_UINT32(2, nvs_state().commits);
    TEST_ASSERT_EQUAL_UINT8(120, nvs_state().committed.at("arc_00"));
}

void test_nvs_repeated_targets_do_not_repeat_set_calls() {
    reset_nvs();
    NvsLevelStore store{1};
    const auto target = slot(SlotKind::Target, channel(0));
    TEST_ASSERT_TRUE(store.save(target, level(120)));
    TEST_ASSERT_TRUE(store.save(target, level(120)));
    TEST_ASSERT_EQUAL_UINT32(1, nvs_state().writes);
}

void test_nvs_unchanged_committed_value_skips_the_write() {
    reset_nvs();
    nvs_state().committed["arc_01"] = 120;
    NvsLevelStore store{1};
    TEST_ASSERT_TRUE(store.save(slot(SlotKind::Target, channel(1)), level(120)));
    TEST_ASSERT_EQUAL_UINT32(0, nvs_state().writes);
    TEST_ASSERT_TRUE(store.commit());
    TEST_ASSERT_EQUAL_UINT32(0, nvs_state().commits);
}

void test_nvs_write_failure_marks_nothing_staged() {
    reset_nvs();
    NvsLevelStore store{1};
    nvs_state().writes_succeed = false;
    TEST_ASSERT_FALSE(store.save(slot(SlotKind::Target, channel(0)), level(100)));
    nvs_state().writes_succeed = true;
    TEST_ASSERT_TRUE(store.commit());
    TEST_ASSERT_EQUAL_UINT32(0, nvs_state().commits);
}

void test_nvs_last_active_keys_are_per_address_kind() {
    reset_nvs();
    NvsLevelStore store{1};
    TEST_ASSERT_TRUE(store.save(slot(SlotKind::LastActive, channel(1)), level(10)));
    TEST_ASSERT_TRUE(store.save(slot(SlotKind::LastActive, group_address(2)), level(20)));
    TEST_ASSERT_TRUE(store.save(slot(SlotKind::LastActive, Address::broadcast()), level(30)));
    TEST_ASSERT_TRUE(store.commit());
    TEST_ASSERT_EQUAL_UINT8(10, nvs_state().committed.at("a01"));
    TEST_ASSERT_EQUAL_UINT8(20, nvs_state().committed.at("g02"));
    TEST_ASSERT_EQUAL_UINT8(30, nvs_state().committed.at("b"));
}

void test_nvs_target_slot_rejects_non_short_addresses() {
    reset_nvs();
    NvsLevelStore store{1};
    TEST_ASSERT_FALSE(store.save(slot(SlotKind::Target, Address::broadcast()), level(10)));
    TEST_ASSERT_EQUAL_UINT32(0, nvs_state().writes);
}

void test_nvs_staged_values_are_visible_to_load_before_commit() {
    reset_nvs();
    NvsLevelStore store{1};
    const auto target = slot(SlotKind::LastActive, channel(7));
    TEST_ASSERT_TRUE(store.save(target, level(88)));
    const auto result = store.load(target);
    TEST_ASSERT_EQUAL(static_cast<int>(ReadStatus::Ok), static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_UINT8(88, result.level->value());
}

void test_nvs_resource_is_closed_once() {
    reset_nvs();
    {
        NvsLevelStore store{1};
        TEST_ASSERT_TRUE(store.available());
    }
    TEST_ASSERT_EQUAL_UINT32(1, nvs_state().closes);
}

}

void run_storage_adapter_tests() {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_nvs_missing_target_is_not_a_storage_failure);
    RUN_TEST(test_nvs_rejects_stored_mask_value);
    RUN_TEST(test_nvs_unavailable_handle_does_not_write);
    RUN_TEST(test_nvs_save_only_stages_until_commit);
    RUN_TEST(test_nvs_commit_is_a_no_op_without_staged_writes);
    RUN_TEST(test_nvs_retries_commit_after_a_failed_commit);
    RUN_TEST(test_nvs_repeated_targets_do_not_repeat_set_calls);
    RUN_TEST(test_nvs_unchanged_committed_value_skips_the_write);
    RUN_TEST(test_nvs_write_failure_marks_nothing_staged);
    RUN_TEST(test_nvs_last_active_keys_are_per_address_kind);
    RUN_TEST(test_nvs_target_slot_rejects_non_short_addresses);
    RUN_TEST(test_nvs_staged_values_are_visible_to_load_before_commit);
    RUN_TEST(test_nvs_resource_is_closed_once);
}
