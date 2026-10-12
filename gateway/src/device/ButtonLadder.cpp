#include "device/ButtonLadder.hpp"

#include <cstdint>

namespace gateway::device {

Buttons classify_ladder(std::uint16_t millivolts) noexcept {
    if (millivolts >= ladder_idle_min_mv) {
        return {};
    }
    if (millivolts >= ladder_prg_min_mv) {
        return {true, false};
    }
    if (millivolts >= ladder_func_min_mv) {
        return {false, true};
    }
    return {true, true};
}

}
