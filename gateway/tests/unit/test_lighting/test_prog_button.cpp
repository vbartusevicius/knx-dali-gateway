#include <initializer_list>

#include <unity.h>

#include "device/ProgButton.hpp"

namespace {

using gateway::device::ButtonState;
using gateway::device::Gesture;
using gateway::device::LedPattern;
using gateway::device::ProgButton;

// Settle a level through the debounce window.
void hold(ProgButton& button, bool pressed, std::uint32_t& now, std::uint32_t extra_ms = 0) {
    (void)button.sample(pressed, now);
    now += gateway::device::debounce_ms + extra_ms;
    (void)button.sample(pressed, now);
}

void test_bounce_is_rejected() {
    ProgButton button;
    std::uint32_t now = 0;
    (void)button.sample(true, now);       // first contact
    now += 10;
    (void)button.sample(false, now);      // bounce
    now += 10;
    (void)button.sample(true, now);       // settle
    now += 50;
    (void)button.sample(true, now);
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::Held), static_cast<int>(button.state()));

    now += 10;
    (void)button.sample(false, now);      // bounce on release
    now += 10;
    (void)button.sample(true, now);
    now += 50;
    TEST_ASSERT_EQUAL(static_cast<int>(Gesture::None),
                      static_cast<int>(button.sample(true, now)));
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::Held), static_cast<int>(button.state()));
}

void test_shorter_than_debounce_is_no_press() {
    ProgButton button;
    std::uint32_t now = 0;
    (void)button.sample(true, now);
    now += 20;
    (void)button.sample(false, now);
    now += 100;
    TEST_ASSERT_EQUAL(static_cast<int>(Gesture::None),
                      static_cast<int>(button.sample(false, now)));
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::Idle), static_cast<int>(button.state()));
}

void test_short_press_toggles_prog_mode() {
    ProgButton button;
    std::uint32_t now = 0;
    hold(button, true, now, 300);         // debounced press, ~350 ms held
    (void)button.sample(false, now);      // release
    now += 50;
    TEST_ASSERT_EQUAL(static_cast<int>(Gesture::ToggleProgMode),
                      static_cast<int>(button.sample(false, now)));
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::Idle), static_cast<int>(button.state()));
}

void test_release_between_two_and_ten_seconds_is_ignored() {
    // Debounce adds ~100 ms, so the top case stays under the 10 s reset line.
    for (const std::uint32_t held : {2'000u, 3'000u, 9'800u}) {
        ProgButton button;
        std::uint32_t now = 0;
        hold(button, true, now);
        now += held;
        (void)button.sample(true, now);
        (void)button.sample(false, now);
        now += 50;
        TEST_ASSERT_EQUAL(static_cast<int>(Gesture::None),
                          static_cast<int>(button.sample(false, now)));
    }
}

void test_holding_past_five_seconds_shows_the_warning() {
    ProgButton button;
    std::uint32_t now = 0;
    hold(button, true, now);
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::Held), static_cast<int>(button.state()));
    TEST_ASSERT_EQUAL(static_cast<int>(LedPattern::Off),
                      static_cast<int>(button.led_pattern(false)));

    now += 4'999;
    (void)button.sample(true, now);
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::Held), static_cast<int>(button.state()));

    now += 1;
    (void)button.sample(true, now);
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::ResetWarning), static_cast<int>(button.state()));
    TEST_ASSERT_EQUAL(static_cast<int>(LedPattern::FastBlink),
                      static_cast<int>(button.led_pattern(false)));
    // The warning wins over the programming-mode indicator.
    TEST_ASSERT_EQUAL(static_cast<int>(LedPattern::FastBlink),
                      static_cast<int>(button.led_pattern(true)));
}

void test_release_after_ten_seconds_factory_resets() {
    ProgButton button;
    std::uint32_t now = 0;
    hold(button, true, now);
    now += 12'000;
    (void)button.sample(true, now);
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::ResetWarning),
                      static_cast<int>(button.state()));  // still warning while held
    (void)button.sample(false, now);
    now += 50;
    TEST_ASSERT_EQUAL(static_cast<int>(Gesture::FactoryReset),
                      static_cast<int>(button.sample(false, now)));
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::Idle), static_cast<int>(button.state()));
}

void test_led_pattern_follows_prog_mode() {
    ProgButton button;
    TEST_ASSERT_EQUAL(static_cast<int>(LedPattern::Off),
                      static_cast<int>(button.led_pattern(false)));
    TEST_ASSERT_EQUAL(static_cast<int>(LedPattern::On),
                      static_cast<int>(button.led_pattern(true)));

    // While held without warning the LED still reports prog mode.
    std::uint32_t now = 0;
    hold(button, true, now, 100);
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::Held), static_cast<int>(button.state()));
    TEST_ASSERT_EQUAL(static_cast<int>(LedPattern::On), static_cast<int>(button.led_pattern(true)));
    TEST_ASSERT_EQUAL(static_cast<int>(LedPattern::Off), static_cast<int>(button.led_pattern(false)));
}

void test_time_wraparound_does_not_break_gestures() {
    ProgButton button;
    std::uint32_t now = 0xFFFFFFF0u;
    hold(button, true, now);              // crosses the wrap during debounce
    now += 300;
    (void)button.sample(true, now);
    (void)button.sample(false, now);
    now += 60;
    TEST_ASSERT_EQUAL(static_cast<int>(Gesture::ToggleProgMode),
                      static_cast<int>(button.sample(false, now)));
}

}

void run_prog_button_tests() {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_bounce_is_rejected);
    RUN_TEST(test_shorter_than_debounce_is_no_press);
    RUN_TEST(test_short_press_toggles_prog_mode);
    RUN_TEST(test_release_between_two_and_ten_seconds_is_ignored);
    RUN_TEST(test_holding_past_five_seconds_shows_the_warning);
    RUN_TEST(test_release_after_ten_seconds_factory_resets);
    RUN_TEST(test_led_pattern_follows_prog_mode);
    RUN_TEST(test_time_wraparound_does_not_break_gestures);
}
