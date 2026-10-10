#include "device/ProgButton.hpp"

namespace gateway::device {

Gesture ProgButton::sample(bool pressed, std::uint32_t now_ms) noexcept {
    if (pressed != raw_) {
        raw_ = pressed;
        raw_changed_ms_ = now_ms;
    }
    auto gesture = Gesture::None;
    if (pressed_ != raw_ && static_cast<std::uint32_t>(now_ms - raw_changed_ms_) >= debounce_ms) {
        pressed_ = raw_;
        if (pressed_) {
            pressed_ms_ = now_ms;
        } else {
            const auto held_ms = static_cast<std::uint32_t>(now_ms - pressed_ms_);
            if (held_ms < toggle_max_ms) {
                gesture = Gesture::ToggleProgMode;
            } else if (held_ms >= factory_reset_ms) {
                gesture = Gesture::FactoryReset;
            }
        }
    }
    if (!pressed_) {
        state_ = ButtonState::Idle;
    } else if (static_cast<std::uint32_t>(now_ms - pressed_ms_) >= reset_warning_ms) {
        state_ = ButtonState::ResetWarning;
    } else {
        state_ = ButtonState::Held;
    }
    return gesture;
}

LedPattern ProgButton::led_pattern(bool prog_mode) const noexcept {
    if (state_ == ButtonState::ResetWarning) {
        return LedPattern::FastBlink;
    }
    return prog_mode ? LedPattern::On : LedPattern::Off;
}

}
