#include "lighting/LampFinder.hpp"

#include <array>
#include <cstdint>
#include <optional>

#include "dali/Address.hpp"
#include "dali/ArcLevel.hpp"

namespace gateway::lighting {
namespace {

// 8-step triangle/breathing wave (254 -> 200 -> 140 -> 80 -> 35 -> 80 -> 140 -> 200)
constexpr std::array<std::uint8_t, 8> smooth_wave{254, 200, 140, 80, 35, 80, 140, 200};

}  // namespace

std::uint32_t LampFinder::step_interval_ms() const noexcept {
    return mode_ == FinderMode::SmoothPulse ? smooth_period_ms : blink_period_ms;
}

dali::ArcLevel LampFinder::compute_step_level() const noexcept {
    if (mode_ == FinderMode::SmoothPulse) {
        return *dali::ArcLevel::from(smooth_wave[phase_index_ % smooth_wave.size()]);
    }
    return *dali::ArcLevel::from((phase_index_ & 1U) == 0 ? dali::ArcLevel::maximum : 0U);
}

std::optional<FinderStep> LampFinder::start(dali::Address address, FinderMode mode,
                                            std::optional<dali::ArcLevel> previous_level,
                                            std::uint32_t now_ms, std::uint32_t duration_ms) noexcept {
    if (mode == FinderMode::Stop) {
        return stop();
    }
    if (mode_ == FinderMode::Stop || !(address_ == address)) {
        saved_level_ = previous_level.value_or(*dali::ArcLevel::from(0));
    }
    address_ = address;
    mode_ = mode;
    phase_index_ = 0;
    const auto first_level = compute_step_level();
    phase_index_ = 1;
    next_step_ms_ = now_ms + step_interval_ms();
    stop_at_ms_ = now_ms + (duration_ms > 0 ? duration_ms : default_duration_ms);
    return FinderStep{address_, first_level, false};
}

std::optional<FinderStep> LampFinder::stop() noexcept {
    if (mode_ == FinderMode::Stop) {
        return std::nullopt;
    }
    mode_ = FinderMode::Stop;
    return FinderStep{address_, saved_level_, true};
}

std::optional<FinderStep> LampFinder::poll(std::uint32_t now_ms) noexcept {
    if (mode_ == FinderMode::Stop) {
        return std::nullopt;
    }
    if (static_cast<std::int32_t>(now_ms - stop_at_ms_) >= 0) {
        return stop();
    }
    if (static_cast<std::int32_t>(now_ms - next_step_ms_) < 0) {
        return std::nullopt;
    }
    const auto lvl = compute_step_level();
    ++phase_index_;
    next_step_ms_ = now_ms + step_interval_ms();
    return FinderStep{address_, lvl, false};
}

std::optional<std::uint32_t> LampFinder::next_deadline() const noexcept {
    if (mode_ == FinderMode::Stop) {
        return std::nullopt;
    }
    if (static_cast<std::int32_t>(stop_at_ms_ - next_step_ms_) < 0) {
        return stop_at_ms_;
    }
    return next_step_ms_;
}

}
