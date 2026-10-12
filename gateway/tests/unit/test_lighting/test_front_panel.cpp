#include <initializer_list>

#include <unity.h>

#include "device/ButtonLadder.hpp"
#include "device/FrontPanel.hpp"

namespace {

using gateway::device::Buttons;
using gateway::device::classify_ladder;
using gateway::device::FrontPanel;
using gateway::device::Gesture;
using gateway::device::PanelHint;

constexpr Buttons none{};
constexpr Buttons prg{true, false};
constexpr Buttons func{false, true};
constexpr Buttons both{true, true};

int as_int(Gesture g) {
    return static_cast<int>(g);
}

// Holds `pressed` from `now` for `ms`, sampling every 10 ms; returns the first
// gesture reported.
Gesture hold(FrontPanel& panel, Buttons pressed, std::uint32_t& now, std::uint32_t ms) {
    auto first = Gesture::None;
    for (const auto end = now + ms; now != end; now += 10) {
        const auto g = panel.sample(pressed, now);
        if (first == Gesture::None) {
            first = g;
        }
    }
    return first;
}

Gesture press_and_release(FrontPanel& panel, Buttons pressed, std::uint32_t hold_ms) {
    std::uint32_t now = 1'000;
    const auto during = hold(panel, pressed, now, hold_ms);
    const auto after = hold(panel, none, now, 200);
    return during != Gesture::None ? during : after;
}

void test_ladder_levels_decode_to_buttons() {
    TEST_ASSERT_TRUE(classify_ladder(3300) == none);
    TEST_ASSERT_TRUE(classify_ladder(2200) == prg);
    TEST_ASSERT_TRUE(classify_ladder(1650) == func);
    TEST_ASSERT_TRUE(classify_ladder(1320) == both);
}

void test_ladder_tolerates_ten_percent_drift() {
    for (const auto mv : {1980, 2420}) {
        TEST_ASSERT_TRUE(classify_ladder(static_cast<std::uint16_t>(mv)) == prg);
    }
    for (const auto mv : {1500, 1815}) {
        TEST_ASSERT_TRUE(classify_ladder(static_cast<std::uint16_t>(mv)) == func);
    }
    for (const auto mv : {1188, 1452}) {
        TEST_ASSERT_TRUE(classify_ladder(static_cast<std::uint16_t>(mv)) == both);
    }
    TEST_ASSERT_TRUE(classify_ladder(2970) == none);
}

void test_prg_tap_reboots() {
    FrontPanel panel;
    TEST_ASSERT_EQUAL(as_int(Gesture::Reboot), as_int(press_and_release(panel, prg, 300)));
}

void test_prg_hold_enters_programming_mode_at_five_seconds_while_held() {
    FrontPanel panel;
    std::uint32_t now = 0;
    TEST_ASSERT_EQUAL(as_int(Gesture::None), as_int(hold(panel, prg, now, 4'900)));
    TEST_ASSERT_EQUAL(as_int(Gesture::ToggleProgMode), as_int(hold(panel, prg, now, 300)));
    TEST_ASSERT_EQUAL(as_int(Gesture::None), as_int(hold(panel, prg, now, 3'000)));
    TEST_ASSERT_EQUAL(as_int(Gesture::None), as_int(hold(panel, none, now, 200)));
}

void test_func_tap_shows_next_page() {
    FrontPanel panel;
    TEST_ASSERT_EQUAL(as_int(Gesture::NextPage), as_int(press_and_release(panel, func, 300)));
}

void test_func_hold_starts_wifi_setup() {
    FrontPanel panel;
    TEST_ASSERT_EQUAL(as_int(Gesture::WifiSetup), as_int(press_and_release(panel, func, 5'300)));
}

void test_both_held_ten_seconds_factory_resets() {
    FrontPanel panel;
    std::uint32_t now = 0;
    TEST_ASSERT_EQUAL(as_int(Gesture::None), as_int(hold(panel, both, now, 9'900)));
    TEST_ASSERT_EQUAL(static_cast<int>(PanelHint::FactoryResetArming), static_cast<int>(panel.hint()));
    TEST_ASSERT_EQUAL(as_int(Gesture::FactoryReset), as_int(hold(panel, both, now, 300)));
    TEST_ASSERT_EQUAL(static_cast<int>(PanelHint::None), static_cast<int>(panel.hint()));
}

void test_releasing_both_early_does_nothing() {
    FrontPanel panel;
    TEST_ASSERT_EQUAL(as_int(Gesture::None), as_int(press_and_release(panel, both, 6'000)));
}

void test_second_button_turns_a_tap_into_the_reset_combo() {
    FrontPanel panel;
    std::uint32_t now = 0;
    hold(panel, prg, now, 1'000);
    TEST_ASSERT_EQUAL(as_int(Gesture::None), as_int(hold(panel, both, now, 3'000)));
    TEST_ASSERT_EQUAL(as_int(Gesture::None), as_int(hold(panel, prg, now, 500)));
    TEST_ASSERT_EQUAL(as_int(Gesture::None), as_int(hold(panel, none, now, 200)));
}

void test_combo_timer_starts_when_both_are_down() {
    FrontPanel panel;
    std::uint32_t now = 0;
    hold(panel, func, now, 4'000);
    TEST_ASSERT_EQUAL(as_int(Gesture::None), as_int(hold(panel, both, now, 9'000)));
    TEST_ASSERT_EQUAL(as_int(Gesture::FactoryReset), as_int(hold(panel, both, now, 1'500)));
}

void test_bounce_shorter_than_debounce_is_ignored() {
    FrontPanel panel;
    std::uint32_t now = 0;
    hold(panel, prg, now, 30);
    TEST_ASSERT_EQUAL(as_int(Gesture::None), as_int(hold(panel, none, now, 500)));
}

void test_gestures_survive_time_wraparound() {
    FrontPanel panel;
    std::uint32_t now = 0xFFFFFF00u;
    hold(panel, prg, now, 400);
    TEST_ASSERT_EQUAL(as_int(Gesture::Reboot), as_int(hold(panel, none, now, 200)));
}

}

void run_front_panel_tests() {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_ladder_levels_decode_to_buttons);
    RUN_TEST(test_ladder_tolerates_ten_percent_drift);
    RUN_TEST(test_prg_tap_reboots);
    RUN_TEST(test_prg_hold_enters_programming_mode_at_five_seconds_while_held);
    RUN_TEST(test_func_tap_shows_next_page);
    RUN_TEST(test_func_hold_starts_wifi_setup);
    RUN_TEST(test_both_held_ten_seconds_factory_resets);
    RUN_TEST(test_releasing_both_early_does_nothing);
    RUN_TEST(test_second_button_turns_a_tap_into_the_reset_combo);
    RUN_TEST(test_combo_timer_starts_when_both_are_down);
    RUN_TEST(test_bounce_shorter_than_debounce_is_ignored);
    RUN_TEST(test_gestures_survive_time_wraparound);
}
