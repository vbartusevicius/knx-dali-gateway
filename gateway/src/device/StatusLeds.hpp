#pragma once

#include <array>
#include <cstdint>

#include "device/FrontPanel.hpp"

namespace gateway::device {

struct Rgb {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    friend bool operator==(Rgb a, Rgb b) noexcept { return a.r == b.r && a.g == b.g && a.b == b.b; }
};

namespace colour {
constexpr Rgb off{};
constexpr Rgb green{0, 255, 0};
constexpr Rgb dim_green{0, 40, 0};
constexpr Rgb red{255, 0, 0};
constexpr Rgb amber{255, 110, 0};
constexpr Rgb blue{0, 0, 255};
constexpr Rgb cyan{0, 200, 200};
constexpr Rgb white{255, 255, 255};
}

enum class WifiState : std::uint8_t {
    Off,
    Setup,
    Connecting,
    Connected
};
enum class DaliState : std::uint8_t {
    Ok,
    NoPower,
    Short,
    GearFault,
    Commissioning
};

struct DeviceStatus {
    bool fault = false;     // configuration or internal error
    bool identify = false;  // "find me" from ETS or the dashboard
    bool knx_bus_ok = true;
    bool knx_prog_mode = false;
    bool knx_activity = false;  // telegram within the last activity window
    DaliState dali = DaliState::Ok;
    bool dali_activity = false;  // our frame within the last activity window
    bool dali_other_master = false;
    WifiState wifi = WifiState::Off;
    PanelHint hint = PanelHint::None;
};

enum Led : std::uint8_t {
    status_led = 0,
    knx_led = 1,
    dali_led = 2
};
using LedColours = std::array<Rgb, 3>;

constexpr std::uint32_t slow_blink_ms = 500;
constexpr std::uint32_t fast_blink_ms = 100;
constexpr std::uint32_t wifi_beacon_period_ms = 5'000;
constexpr std::uint32_t wifi_beacon_ms = 150;

// The three front LEDs never switch off by themselves: each always shows its
// subsystem's state, blinking only for conditions that need attention.
LedColours led_colours(const DeviceStatus& status, std::uint32_t now_ms) noexcept;

}
