#include "device/DaliPsuMonitor.hpp"

#include <cstdint>

#include "device/StatusLeds.hpp"

namespace gateway::device {

void DaliPsuMonitor::set_internal_psu_enabled(bool enabled) noexcept {
    if (internal_enabled_ == enabled) {
        return;
    }
    internal_enabled_ = enabled;
    state_ = PsuProtectionState::Normal;
    low_tracking_ = false;
}

void DaliPsuMonitor::reset_trip() noexcept {
    state_ = PsuProtectionState::Normal;
    low_tracking_ = false;
}

PsuSampleResult DaliPsuMonitor::sample(bool bus_idle_high, std::uint32_t now_ms) noexcept {
    PsuSampleResult out{};

    if (!internal_enabled_) {
        out.supply_off_gpio = true;
        if (bus_idle_high) {
            low_tracking_ = false;
            state_ = PsuProtectionState::Normal;
            out.state = state_;
            out.dali_led_state = DaliState::Ok;
            return out;
        }
        if (!low_tracking_) {
            low_tracking_ = true;
            low_since_ms_ = now_ms;
        } else if (state_ != PsuProtectionState::ExternalNoPower &&
                   (now_ms - low_since_ms_) >= external_loss_ms) {
            state_ = PsuProtectionState::ExternalNoPower;
            out.just_lost_external_power = true;
        }
        out.state = state_;
        out.dali_led_state =
            (state_ == PsuProtectionState::ExternalNoPower) ? DaliState::NoPower : DaliState::Ok;
        return out;
    }

    if (state_ == PsuProtectionState::ShortTripped) {
        // Latched OFF: never pulse or bash the bus until explicitly reset.
        out.supply_off_gpio = true;
        out.state = PsuProtectionState::ShortTripped;
        out.dali_led_state = DaliState::Short;
        return out;
    }

    state_ = PsuProtectionState::Normal;
    out.supply_off_gpio = false;
    if (bus_idle_high) {
        low_tracking_ = false;
        out.state = state_;
        out.dali_led_state = DaliState::Ok;
        return out;
    }

    if (!low_tracking_) {
        low_tracking_ = true;
        low_since_ms_ = now_ms;
    } else if ((now_ms - low_since_ms_) >= short_trip_ms) {
        state_ = PsuProtectionState::ShortTripped;
        low_tracking_ = false;
        ++trip_count_;
        out.supply_off_gpio = true;
        out.just_tripped_short = true;
        out.state = state_;
        out.dali_led_state = DaliState::Short;
        return out;
    }

    out.state = state_;
    out.dali_led_state = DaliState::Ok;
    return out;
}

}
