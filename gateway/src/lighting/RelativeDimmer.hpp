#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "dali/Address.hpp"

namespace gateway::lighting {

// Relative dimming direction as on the KNX DPT 3.007 telegram.
enum class DimDirection {
    Up,
    Down
};

// A step the dimmer asks the service to transmit.
struct DimStep {
    dali::Address address;
    DimDirection direction;
    bool first;  // first step after start() (up uses ON AND STEP UP)
};

// Emitted when a running dim exceeds the safety limit (lost stop telegram).
struct DimExpiry {
    dali::Address address;
};

// Tracks running relative-dim operations for every addressable target and
// produces periodic UP/DOWN steps. Time is injected in milliseconds; unsigned
// subtraction makes comparisons wrap-around safe. Fixed 81 slots: 64 short
// addresses, 16 groups, broadcast.
class RelativeDimmer final {
public:
    static constexpr std::uint32_t step_interval_ms = 200;
    static constexpr std::uint32_t safety_limit_ms = 10'000;
    static constexpr std::size_t slot_count = 64 + 16 + 1;

    void start(dali::Address address, DimDirection direction, std::uint32_t now_ms) noexcept;
    // Returns true when a dim was running for the address.
    bool stop(dali::Address address) noexcept;
    bool running(dali::Address address) const noexcept;
    // Earliest time any slot needs attention (next step or safety expiry).
    std::optional<std::uint32_t> next_deadline() const noexcept;

    template <typename OnStep, typename OnExpiry>
    void poll(std::uint32_t now_ms, OnStep&& on_step, OnExpiry&& on_expiry) {
        for (std::size_t index = 0; index < slots_.size(); ++index) {
            auto& slot = slots_[index];
            if (!slot.running) {
                continue;
            }
            if (now_ms - slot.started_at >= safety_limit_ms) {
                slot.running = false;
                on_expiry(DimExpiry{address_at(index)});
                continue;
            }
            if (reached(now_ms, slot.next_step_at)) {
                on_step(DimStep{address_at(index), slot.direction, slot.first_step});
                slot.first_step = false;
                slot.next_step_at += step_interval_ms;
            }
        }
    }

private:
    struct Slot {
        bool running = false;
        bool first_step = false;
        DimDirection direction = DimDirection::Up;
        std::uint32_t started_at = 0;
        std::uint32_t next_step_at = 0;
    };

    static bool reached(std::uint32_t now_ms, std::uint32_t deadline_ms) noexcept {
        return static_cast<std::int32_t>(now_ms - deadline_ms) >= 0;
    }

    static std::size_t slot_index(dali::Address address) noexcept;
    static dali::Address address_at(std::size_t index) noexcept;

    std::array<Slot, slot_count> slots_{};
};

}
