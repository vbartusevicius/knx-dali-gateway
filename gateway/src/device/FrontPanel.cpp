#include "device/FrontPanel.hpp"

#include <cstdint>

#include "device/ButtonLadder.hpp"

namespace gateway::device {

namespace {

std::uint32_t elapsed(std::uint32_t since, std::uint32_t now) noexcept {
    return now - since;
}

}

Gesture FrontPanel::sample(Buttons pressed, std::uint32_t now_ms) noexcept {
    if (pressed != raw_) {
        raw_ = pressed;
        raw_changed_ms_ = now_ms;
    }
    auto gesture = Gesture::None;
    if (stable_ != raw_ && elapsed(raw_changed_ms_, now_ms) >= debounce_ms) {
        const auto previous = stable_;
        stable_ = raw_;
        gesture = on_change(previous, now_ms);
    }
    if (gesture == Gesture::None) {
        gesture = on_hold(now_ms);
    }
    hint_ = session_ == Session::Both ? PanelHint::FactoryResetArming : PanelHint::None;
    return gesture;
}

Gesture FrontPanel::on_change(Buttons previous, std::uint32_t now_ms) noexcept {
    if (stable_.prg && stable_.func) {
        if (session_ != Session::Both && session_ != Session::Done) {
            session_ = Session::Both;
            session_ms_ = now_ms;
        }
        return Gesture::None;
    }
    if (stable_.prg || stable_.func) {
        if (session_ == Session::Idle) {
            session_ = stable_.prg ? Session::Prg : Session::Func;
            session_ms_ = now_ms;
        } else if (session_ == Session::Both) {
            session_ = Session::Done;
        }
        return Gesture::None;
    }
    const auto ended = session_;
    session_ = Session::Idle;
    if (!previous.prg && !previous.func) {
        return Gesture::None;
    }
    if (ended == Session::Prg) {
        return Gesture::Reboot;
    }
    if (ended == Session::Func) {
        return Gesture::NextPage;
    }
    return Gesture::None;
}

Gesture FrontPanel::on_hold(std::uint32_t now_ms) noexcept {
    const auto held = elapsed(session_ms_, now_ms);
    switch (session_) {
    case Session::Prg:
        if (held >= long_press_ms) {
            session_ = Session::Done;
            return Gesture::ToggleProgMode;
        }
        break;
    case Session::Func:
        if (held >= long_press_ms) {
            session_ = Session::Done;
            return Gesture::WifiSetup;
        }
        break;
    case Session::Both:
        if (held >= factory_reset_ms) {
            session_ = Session::Done;
            return Gesture::FactoryReset;
        }
        break;
    case Session::Idle:
    case Session::Done:
        break;
    }
    return Gesture::None;
}

}
