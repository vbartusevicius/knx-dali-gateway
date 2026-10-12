#include <unity.h>

#include "device/StatusLeds.hpp"

namespace {

using namespace gateway::device;

void assert_colour(Rgb expected, Rgb actual) {
    TEST_ASSERT_EQUAL_UINT8(expected.r, actual.r);
    TEST_ASSERT_EQUAL_UINT8(expected.g, actual.g);
    TEST_ASSERT_EQUAL_UINT8(expected.b, actual.b);
}

bool blinks(const DeviceStatus& s, Led led, Rgb on, std::uint32_t half_period_ms) {
    return led_colours(s, 1'000)[led] == on && led_colours(s, 1'000 + half_period_ms)[led] == colour::off;
}

void test_healthy_device_shows_steady_green_and_never_goes_dark() {
    const DeviceStatus healthy{};
    for (std::uint32_t t = 0; t < 600'000; t += 997) {
        const auto leds = led_colours(healthy, t);
        assert_colour(colour::green, leds[status_led]);
        assert_colour(colour::dim_green, leds[knx_led]);
        assert_colour(colour::dim_green, leds[dali_led]);
    }
}

void test_programming_mode_shows_steady_red_on_the_knx_led() {
    DeviceStatus s;
    s.knx_prog_mode = true;
    s.knx_activity = true;
    assert_colour(colour::red, led_colours(s, 0)[knx_led]);
    assert_colour(colour::red, led_colours(s, slow_blink_ms)[knx_led]);
}

void test_missing_knx_bus_blinks_knx_red_and_turns_status_red() {
    DeviceStatus s;
    s.knx_bus_ok = false;
    TEST_ASSERT_TRUE(blinks(s, knx_led, colour::red, slow_blink_ms));
    assert_colour(colour::red, led_colours(s, 0)[status_led]);
}

void test_activity_brightens_the_bus_leds() {
    DeviceStatus s;
    s.knx_activity = true;
    s.dali_activity = true;
    assert_colour(colour::green, led_colours(s, 0)[knx_led]);
    assert_colour(colour::green, led_colours(s, 0)[dali_led]);
    s.dali_activity = false;
    s.dali_other_master = true;
    assert_colour(colour::cyan, led_colours(s, 0)[dali_led]);
}

void test_dali_faults() {
    DeviceStatus s;
    s.dali = DaliState::NoPower;
    assert_colour(colour::red, led_colours(s, 0)[dali_led]);
    assert_colour(colour::red, led_colours(s, 0)[status_led]);
    s.dali = DaliState::Short;
    TEST_ASSERT_TRUE(blinks(s, dali_led, colour::red, fast_blink_ms));
    s.dali = DaliState::GearFault;
    assert_colour(colour::amber, led_colours(s, 0)[dali_led]);
    assert_colour(colour::amber, led_colours(s, 0)[status_led]);
    s.dali = DaliState::Commissioning;
    assert_colour(colour::blue, led_colours(s, 0)[dali_led]);
}

void test_wifi_states_on_the_status_led() {
    DeviceStatus s;
    s.wifi = WifiState::Setup;
    TEST_ASSERT_TRUE(blinks(s, status_led, colour::blue, slow_blink_ms));
    s.wifi = WifiState::Connecting;
    assert_colour(colour::amber, led_colours(s, 0)[status_led]);
    s.wifi = WifiState::Connected;
    assert_colour(colour::blue, led_colours(s, 0)[status_led]);
    assert_colour(colour::green, led_colours(s, wifi_beacon_ms)[status_led]);
    assert_colour(colour::blue, led_colours(s, wifi_beacon_period_ms)[status_led]);
}

void test_fault_wins_over_wifi_and_identify_wins_over_fault() {
    DeviceStatus s;
    s.wifi = WifiState::Setup;
    s.fault = true;
    assert_colour(colour::red, led_colours(s, 0)[status_led]);
    s.identify = true;
    TEST_ASSERT_TRUE(blinks(s, status_led, colour::white, fast_blink_ms));
}

void test_factory_reset_arming_blinks_all_three_red() {
    DeviceStatus s;
    s.knx_prog_mode = true;
    s.hint = PanelHint::FactoryResetArming;
    for (const auto led : {status_led, knx_led, dali_led}) {
        TEST_ASSERT_TRUE(blinks(s, led, colour::red, fast_blink_ms));
    }
}

}

void run_status_leds_tests() {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_healthy_device_shows_steady_green_and_never_goes_dark);
    RUN_TEST(test_programming_mode_shows_steady_red_on_the_knx_led);
    RUN_TEST(test_missing_knx_bus_blinks_knx_red_and_turns_status_red);
    RUN_TEST(test_activity_brightens_the_bus_leds);
    RUN_TEST(test_dali_faults);
    RUN_TEST(test_wifi_states_on_the_status_led);
    RUN_TEST(test_fault_wins_over_wifi_and_identify_wins_over_fault);
    RUN_TEST(test_factory_reset_arming_blinks_all_three_red);
}
