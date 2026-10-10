#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

namespace gateway::control {

// Milliseconds until `deadline_ms`, wrap-safe; 0 when already due.
inline std::uint32_t ms_until(std::uint32_t deadline_ms, std::uint32_t now_ms) noexcept {
    const auto remaining = static_cast<std::int32_t>(deadline_ms - now_ms);
    return remaining <= 0 ? 0 : static_cast<std::uint32_t>(remaining);
}

// How long the worker may block waiting for the next command: the earlier of
// the dimmer's next step deadline and the next background fault poll, capped
// at max_wait_ms so the loop wakes to feed the watchdog (firmware) or stay
// responsive (lab).
inline std::uint32_t worker_wait_ms(std::uint32_t now_ms,
                                    std::optional<std::uint32_t> dim_deadline_ms,
                                    std::uint32_t next_poll_ms,
                                    std::uint32_t max_wait_ms) noexcept {
    auto wait = max_wait_ms;
    if (dim_deadline_ms) {
        wait = std::min(wait, ms_until(*dim_deadline_ms, now_ms));
    }
    return std::min(wait, ms_until(next_poll_ms, now_ms));
}

}
