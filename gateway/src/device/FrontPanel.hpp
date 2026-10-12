#pragma once

#include <cstdint>

#include "device/ButtonLadder.hpp"

namespace gateway::device {

enum class Gesture : std::uint8_t {
    None,
    Reboot,          // PRG/RST tap: restart the ESP32 and the KNX stack, configuration kept
    ToggleProgMode,  // PRG/RST held 5 s: KNX programming mode on/off
    NextPage,        // FUNC tap: next display page, wakes the display
    WifiSetup,       // FUNC held 5 s: WiFi setup mode
    FactoryReset     // both held 10 s: erase KNX, lighting and WiFi configuration
};

// What a button being held is about to do, for LED feedback.
enum class PanelHint : std::uint8_t {
    None,
    FactoryResetArming  // both buttons held, factory reset pending
};

constexpr std::uint32_t debounce_ms = 50;
constexpr std::uint32_t long_press_ms = 5'000;
constexpr std::uint32_t factory_reset_ms = 10'000;

// Debounced two-button gesture machine with injected time. A press session
// runs from the first button down to both up. Taps report on release; long
// presses fire as soon as their time is reached so the LEDs confirm them while
// the button is still held. Once both buttons have been down together the
// session can only end in a factory reset, never in a single-button gesture.
class FrontPanel final {
public:
    Gesture sample(Buttons pressed, std::uint32_t now_ms) noexcept;
    PanelHint hint() const noexcept { return hint_; }

private:
    enum class Session : std::uint8_t {
        Idle,
        Prg,
        Func,
        Both,
        Done
    };

    Gesture on_change(Buttons previous, std::uint32_t now_ms) noexcept;
    Gesture on_hold(std::uint32_t now_ms) noexcept;

    Buttons raw_{};
    std::uint32_t raw_changed_ms_ = 0;
    Buttons stable_{};
    Session session_ = Session::Idle;
    std::uint32_t session_ms_ = 0;
    PanelHint hint_ = PanelHint::None;
};

}
