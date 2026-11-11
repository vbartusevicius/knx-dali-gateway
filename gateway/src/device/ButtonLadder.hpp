#pragma once

#include <cstdint>

namespace gateway::device {

struct Buttons {
    bool prg = false;
    bool func = false;

    friend bool operator==(Buttons a, Buttons b) noexcept { return a.prg == b.prg && a.func == b.func; }
    friend bool operator!=(Buttons a, Buttons b) noexcept { return !(a == b); }
};

// UI board ladder on one ADC pin: 10k pull-up to 3V3, RESET (right, SW2) 20k
// and SCROLL (left, SW1) 10k to ground. Nominal levels: idle 3300 mV, RESET
// 2200 (prg), SCROLL 1650 (func), both 1320.
// Thresholds sit halfway between neighbouring levels.
constexpr std::uint16_t ladder_idle_min_mv = 2750;
constexpr std::uint16_t ladder_prg_min_mv = 1925;
constexpr std::uint16_t ladder_func_min_mv = 1485;

Buttons classify_ladder(std::uint16_t millivolts) noexcept;

}
