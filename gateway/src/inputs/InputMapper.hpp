#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "config/Configuration.hpp"
#include "dali/devices/DeviceInventory.hpp"
#include "dali/devices/EventDecoder.hpp"

namespace gateway::inputs {

// One KNX group-object write produced for a DALI input event: the object
// number plus the raw value bytes (1-bit/4-bit/1-byte values in data[0],
// DPT 9 illuminance as a 2-byte float).
struct Action {
    std::uint16_t object;
    std::array<std::uint8_t, 2> data;
    std::uint8_t length;
};

// Maps decoded device/instance events to KNX actions per the ETS input
// channel configuration. Owns the per-channel toggle and dim-direction
// state the push-button functions need; KNX feedback writes on the switch
// object update the toggle state so the next short press sends the
// opposite value. Portable — no transport or stack types.
class InputMapper {
public:
    static constexpr std::uint8_t max_actions = 2;

    void configure(const config::Configuration& config) noexcept;
    // True when a configured channel binds (device, instance); used as the
    // commissioning bound-callback that decides which instances to enable.
    bool bound(std::uint8_t device, std::uint8_t instance) const noexcept;
    // Picks up each light channel's QUERY RESOLUTION answer from a fresh
    // inventory; 0 = unknown (the raw 10-bit value is published instead).
    void refresh_resolutions(const dali::DeviceInventory& inventory) noexcept;
    std::uint8_t resolution(std::uint8_t channel) const noexcept;
    // KNX write on the channel's switch object: feedback into toggle state.
    void switch_feedback(std::uint8_t channel, bool on) noexcept;
    bool toggle_state(std::uint8_t channel) const noexcept;
    // reported_type is the instance type the device inventory recorded for
    // this (device, instance) — the event is dropped when it disagrees with
    // the channel's declared type or when no channel binds the instance.
    // Returns the number of actions written to out.
    std::uint8_t map(const dali::device::InputEvent& event, std::optional<std::uint8_t> reported_type,
                     std::array<Action, max_actions>& out) noexcept;

private:
    struct ChannelState {
        bool on = false;
        bool dim_up = false;
        bool has_lux = false;
        float lux = 0;
    };

    std::optional<std::uint8_t> channel_for(const dali::device::InputEvent& event,
                                            std::optional<std::uint8_t> reported_type) const noexcept;
    std::uint8_t map_button(std::uint8_t channel, std::uint16_t info,
                            std::array<Action, max_actions>& out) noexcept;
    std::uint8_t map_occupancy(std::uint8_t channel, std::uint16_t info,
                               std::array<Action, max_actions>& out) noexcept;
    std::uint8_t map_light(std::uint8_t channel, std::uint16_t info,
                           std::array<Action, max_actions>& out) noexcept;

    std::array<config::InputChannel, config::input_count> channels_{};
    std::array<ChannelState, config::input_count> state_{};
    std::array<std::uint8_t, config::input_count> resolutions_{};
};

}
