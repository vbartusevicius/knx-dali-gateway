#pragma once

#include <cstdint>

namespace gateway::device {

// Completed button gestures, reported on release.
enum class Gesture : std::uint8_t {
    None,
    ToggleProgMode,
    FactoryReset
};

enum class ButtonState : std::uint8_t {
    Idle,
    Held,
    ResetWarning
};

// What the prog LED should show; the caller owns the blink timing.
enum class LedPattern : std::uint8_t {
    Off,
    On,
    FastBlink
};

// Gesture thresholds (IEC-style prog button: tap toggles programming mode,
// long hold arms a factory reset, very long hold executes it).
constexpr std::uint32_t debounce_ms = 50;
constexpr std::uint32_t toggle_max_ms = 2'000;
constexpr std::uint32_t reset_warning_ms = 5'000;
constexpr std::uint32_t factory_reset_ms = 10'000;

// Debounced prog-button state machine with injected time. A short release
// toggles programming mode, holding past reset_warning_ms shows the warning
// LED, and a release past factory_reset_ms issues FactoryReset. Releases in
// between are ignored so a mis-timed hold cannot reset the device.
class ProgButton final {
public:
    Gesture sample(bool pressed, std::uint32_t now_ms) noexcept;
    ButtonState state() const noexcept { return state_; }
    LedPattern led_pattern(bool prog_mode) const noexcept;

private:
    bool raw_ = false;               // last sampled (undebounced) level
    std::uint32_t raw_changed_ms_ = 0;
    bool pressed_ = false;           // debounced level
    std::uint32_t pressed_ms_ = 0;   // debounced press start
    ButtonState state_ = ButtonState::Idle;
};

}
