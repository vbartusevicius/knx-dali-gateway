#pragma once

#include <cstdint>

#include "device/StatusLeds.hpp"

namespace gateway::device {

enum class PsuProtectionState : std::uint8_t {
    Normal = 0,
    ShortTripped = 1,
    ExternalNoPower = 2
};

struct PsuSampleResult {
    bool supply_off_gpio = false;  // true: D1/GPIO1 high switches the REC3 off (CTRL through U4)
    PsuProtectionState state = PsuProtectionState::Normal;
    DaliState dali_led_state = DaliState::Ok;
    bool just_tripped_short = false;
    bool just_lost_external_power = false;
};

// Monitors DALI bus idle/low state (from DALI_RX optocoupler U6) and controls
// the REC3-2415SRW/H2/A/CTRL ON/OFF pin (via U4 on D1/GPIO1 = DALI_SUPPLY_OFF).
// Latches the internal converter OFF within 80 ms of a sustained bus short
// circuit so devices on the bus are never repeatedly power-cycled; stays
// latched off until explicitly re-armed (reset_trip / config change / reboot).
class DaliPsuMonitor final {
public:
    static constexpr std::uint32_t short_trip_ms = 80;
    static constexpr std::uint32_t external_loss_ms = 250;

    void set_internal_psu_enabled(bool enabled) noexcept;
    [[nodiscard]] bool internal_psu_enabled() const noexcept { return internal_enabled_; }

    void reset_trip() noexcept;

    [[nodiscard]] PsuSampleResult sample(bool bus_idle_high, std::uint32_t now_ms) noexcept;

    [[nodiscard]] PsuProtectionState state() const noexcept { return state_; }
    [[nodiscard]] std::uint32_t short_trip_count() const noexcept { return trip_count_; }

private:
    bool internal_enabled_ = true;
    PsuProtectionState state_ = PsuProtectionState::Normal;
    bool low_tracking_ = false;
    std::uint32_t low_since_ms_ = 0;
    std::uint32_t trip_count_ = 0;
};

}
