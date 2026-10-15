#pragma once

#include <cstdint>
#include <optional>

#include "dali/Address.hpp"
#include "dali/ArcLevel.hpp"

namespace gateway::lighting {

enum class FinderMode : std::uint8_t {
    Stop = 0,
    BlinkOnOff = 1,
    SmoothPulse = 2
};

struct FinderStep {
    dali::Address address = dali::Address::broadcast();
    dali::ArcLevel level = *dali::ArcLevel::from(dali::ArcLevel::maximum);
    bool restore_final = false;
};

class LampFinder final {
public:
    static constexpr std::uint32_t blink_period_ms = 600;
    static constexpr std::uint32_t smooth_period_ms = 250;
    static constexpr std::uint32_t default_duration_ms = 30'000;

    std::optional<FinderStep> start(dali::Address address, FinderMode mode,
                                    std::optional<dali::ArcLevel> previous_level, std::uint32_t now_ms,
                                    std::uint32_t duration_ms = default_duration_ms) noexcept;

    std::optional<FinderStep> stop() noexcept;

    std::optional<FinderStep> poll(std::uint32_t now_ms) noexcept;

    [[nodiscard]] std::optional<std::uint32_t> next_deadline() const noexcept;
    [[nodiscard]] bool active() const noexcept { return mode_ != FinderMode::Stop; }
    [[nodiscard]] FinderMode mode() const noexcept { return mode_; }
    [[nodiscard]] dali::Address target() const noexcept { return address_; }

private:
    std::uint32_t step_interval_ms() const noexcept;
    dali::ArcLevel compute_step_level() const noexcept;

    dali::Address address_ = dali::Address::broadcast();
    FinderMode mode_ = FinderMode::Stop;
    dali::ArcLevel saved_level_ = *dali::ArcLevel::from(0);
    std::uint32_t next_step_ms_ = 0;
    std::uint32_t stop_at_ms_ = 0;
    std::uint8_t phase_index_ = 0;
};

}
