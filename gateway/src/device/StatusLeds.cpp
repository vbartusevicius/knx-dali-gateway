#include "device/StatusLeds.hpp"

#include <cstdint>

#include "device/FrontPanel.hpp"

namespace gateway::device {

namespace {

bool phase(std::uint32_t now_ms, std::uint32_t half_period_ms) noexcept {
    return (now_ms / half_period_ms) % 2 == 0;
}

Rgb blink(Rgb on, std::uint32_t now_ms, std::uint32_t half_period_ms) noexcept {
    return phase(now_ms, half_period_ms) ? on : colour::off;
}

Rgb status_colour(const DeviceStatus& s, std::uint32_t now_ms) noexcept {
    if (s.identify) {
        return blink(colour::white, now_ms, fast_blink_ms);
    }
    if (s.fault || !s.knx_bus_ok || s.dali == DaliState::NoPower || s.dali == DaliState::Short) {
        return colour::red;
    }
    if (s.wifi == WifiState::Setup) {
        return blink(colour::blue, now_ms, slow_blink_ms);
    }
    if (s.wifi == WifiState::Connecting || s.dali == DaliState::GearFault) {
        return colour::amber;
    }
    if (s.wifi == WifiState::Connected && now_ms % wifi_beacon_period_ms < wifi_beacon_ms) {
        return colour::blue;
    }
    return colour::green;
}

Rgb knx_colour(const DeviceStatus& s, std::uint32_t now_ms) noexcept {
    if (s.knx_prog_mode) {
        return colour::red;
    }
    if (!s.knx_bus_ok) {
        return blink(colour::red, now_ms, slow_blink_ms);
    }
    return s.knx_activity ? colour::green : colour::dim_green;
}

Rgb dali_colour(const DeviceStatus& s, std::uint32_t now_ms) noexcept {
    switch (s.dali) {
    case DaliState::Short:
        return blink(colour::red, now_ms, fast_blink_ms);
    case DaliState::NoPower:
        return colour::red;
    case DaliState::GearFault:
        return colour::amber;
    case DaliState::Commissioning:
        return colour::blue;
    case DaliState::Ok:
        break;
    }
    if (s.dali_activity) {
        return colour::green;
    }
    return s.dali_other_master ? colour::cyan : colour::dim_green;
}

}

LedColours led_colours(const DeviceStatus& status, std::uint32_t now_ms) noexcept {
    if (status.hint == PanelHint::FactoryResetArming) {
        const auto warning = blink(colour::red, now_ms, fast_blink_ms);
        return {warning, warning, warning};
    }
    return {status_colour(status, now_ms), knx_colour(status, now_ms), dali_colour(status, now_ms)};
}

}
