#include <vector>

#include <unity.h>

#include "lighting/RelativeDimmer.hpp"

namespace {

using gateway::dali::Address;
using gateway::dali::Group;
using gateway::dali::ShortAddress;
using gateway::lighting::DimDirection;
using gateway::lighting::DimExpiry;
using gateway::lighting::DimStep;
using gateway::lighting::RelativeDimmer;

Address channel(unsigned value) {
    return Address::short_address(*ShortAddress::from(value));
}

struct Collected {
    std::vector<DimStep> steps;
    std::vector<DimExpiry> expiries;
};

Collected poll(RelativeDimmer& dimmer, std::uint32_t now_ms) {
    Collected collected;
    dimmer.poll(
        now_ms, [&collected](DimStep step) { collected.steps.push_back(step); },
        [&collected](DimExpiry expiry) { collected.expiries.push_back(expiry); });
    return collected;
}

void test_start_produces_an_immediate_first_step() {
    RelativeDimmer dimmer;
    dimmer.start(channel(3), DimDirection::Up, 1'000);
    const auto collected = poll(dimmer, 1'000);
    TEST_ASSERT_EQUAL_UINT32(1, collected.steps.size());
    TEST_ASSERT_TRUE(collected.steps.front().first);
    TEST_ASSERT_TRUE(collected.steps.front().address == channel(3));
    TEST_ASSERT_EQUAL(static_cast<int>(DimDirection::Up),
                      static_cast<int>(collected.steps.front().direction));
    TEST_ASSERT_EQUAL_UINT32(0, collected.expiries.size());
}

void test_steps_repeat_every_interval_and_not_before() {
    RelativeDimmer dimmer;
    dimmer.start(channel(0), DimDirection::Down, 0);
    TEST_ASSERT_EQUAL_UINT32(1, poll(dimmer, 0).steps.size());
    TEST_ASSERT_EQUAL_UINT32(0, poll(dimmer, 199).steps.size());
    const auto due = poll(dimmer, 200);
    TEST_ASSERT_EQUAL_UINT32(1, due.steps.size());
    TEST_ASSERT_FALSE(due.steps.front().first);
    TEST_ASSERT_EQUAL_UINT32(0, poll(dimmer, 399).steps.size());
    TEST_ASSERT_EQUAL_UINT32(1, poll(dimmer, 400).steps.size());
}

void test_stop_ends_the_dim_and_reports_whether_it_ran() {
    RelativeDimmer dimmer;
    dimmer.start(channel(5), DimDirection::Up, 0);
    TEST_ASSERT_TRUE(dimmer.stop(channel(5)));
    TEST_ASSERT_FALSE(dimmer.stop(channel(5)));
    TEST_ASSERT_EQUAL_UINT32(0, poll(dimmer, 10'000).steps.size());
    TEST_ASSERT_EQUAL_UINT32(0, poll(dimmer, 10'000).expiries.size());
}

void test_safety_limit_expires_a_running_dim() {
    RelativeDimmer dimmer;
    dimmer.start(channel(7), DimDirection::Up, 0);
    poll(dimmer, 0);
    TEST_ASSERT_EQUAL_UINT32(0, poll(dimmer, 9'999).expiries.size());
    const auto expired = poll(dimmer, 10'000);
    TEST_ASSERT_EQUAL_UINT32(1, expired.expiries.size());
    TEST_ASSERT_TRUE(expired.expiries.front().address == channel(7));
    TEST_ASSERT_EQUAL_UINT32(0, poll(dimmer, 10'200).steps.size());
}

void test_restart_replaces_direction_and_resets_timing() {
    RelativeDimmer dimmer;
    dimmer.start(channel(0), DimDirection::Up, 0);
    poll(dimmer, 0);
    dimmer.start(channel(0), DimDirection::Down, 500);
    const auto collected = poll(dimmer, 500);
    TEST_ASSERT_EQUAL_UINT32(1, collected.steps.size());
    TEST_ASSERT_TRUE(collected.steps.front().first);
    TEST_ASSERT_EQUAL(static_cast<int>(DimDirection::Down),
                      static_cast<int>(collected.steps.front().direction));
    // Fresh 10 s window: still running just before the new limit.
    TEST_ASSERT_EQUAL_UINT32(0, poll(dimmer, 10'499).expiries.size());
    TEST_ASSERT_EQUAL_UINT32(1, poll(dimmer, 10'500).expiries.size());
}

void test_targets_dim_independently() {
    RelativeDimmer dimmer;
    dimmer.start(channel(0), DimDirection::Up, 0);
    dimmer.start(Address::group(*Group::from(2)), DimDirection::Down, 100);
    const auto first = poll(dimmer, 100);
    TEST_ASSERT_EQUAL_UINT32(2, first.steps.size());
    TEST_ASSERT_TRUE(dimmer.stop(channel(0)));
    const auto next = poll(dimmer, 300);
    TEST_ASSERT_EQUAL_UINT32(1, next.steps.size());
    TEST_ASSERT_TRUE(next.steps.front().address.as_group().has_value());
}

void test_next_deadline_tracks_steps_and_expiry() {
    RelativeDimmer dimmer;
    TEST_ASSERT_FALSE(dimmer.next_deadline().has_value());
    dimmer.start(channel(0), DimDirection::Up, 1'000);
    TEST_ASSERT_EQUAL_UINT32(1'000, *dimmer.next_deadline());
    poll(dimmer, 1'000);
    TEST_ASSERT_EQUAL_UINT32(1'200, *dimmer.next_deadline());
    dimmer.start(channel(1), DimDirection::Down, 1'050);
    TEST_ASSERT_EQUAL_UINT32(1'050, *dimmer.next_deadline());
    TEST_ASSERT_TRUE(dimmer.stop(channel(0)));
    TEST_ASSERT_TRUE(dimmer.stop(channel(1)));
    TEST_ASSERT_FALSE(dimmer.next_deadline().has_value());
}

void test_broadcast_has_its_own_slot() {
    RelativeDimmer dimmer;
    dimmer.start(Address::broadcast(), DimDirection::Up, 0);
    const auto collected = poll(dimmer, 0);
    TEST_ASSERT_EQUAL_UINT32(1, collected.steps.size());
    TEST_ASSERT_TRUE(collected.steps.front().address == Address::broadcast());
    TEST_ASSERT_FALSE(dimmer.stop(channel(0)));
    TEST_ASSERT_TRUE(dimmer.stop(Address::broadcast()));
}

void test_time_wraparound_keeps_timing() {
    RelativeDimmer dimmer;
    constexpr std::uint32_t near_wrap = 0xFFFF'FF00;
    dimmer.start(channel(2), DimDirection::Up, near_wrap);
    TEST_ASSERT_EQUAL_UINT32(1, poll(dimmer, near_wrap).steps.size());
    const auto due = poll(dimmer, near_wrap + 200);  // wraps past 0
    TEST_ASSERT_EQUAL_UINT32(1, due.steps.size());
    TEST_ASSERT_EQUAL_UINT32(0, due.expiries.size());
    const auto expired = poll(dimmer, near_wrap + 10'000);
    TEST_ASSERT_EQUAL_UINT32(1, expired.expiries.size());
}

}

void run_dimmer_tests() {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_start_produces_an_immediate_first_step);
    RUN_TEST(test_steps_repeat_every_interval_and_not_before);
    RUN_TEST(test_stop_ends_the_dim_and_reports_whether_it_ran);
    RUN_TEST(test_safety_limit_expires_a_running_dim);
    RUN_TEST(test_restart_replaces_direction_and_resets_timing);
    RUN_TEST(test_targets_dim_independently);
    RUN_TEST(test_next_deadline_tracks_steps_and_expiry);
    RUN_TEST(test_broadcast_has_its_own_slot);
    RUN_TEST(test_time_wraparound_keeps_timing);
}
