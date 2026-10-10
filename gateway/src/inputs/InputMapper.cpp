#include "inputs/InputMapper.hpp"

#include "dali/devices/DeviceFrames.hpp"
#include "knx/ObjectMap.hpp"

namespace gateway::inputs {

namespace {

constexpr std::uint8_t dim_step = 1;  // DPT 3.007 finest step = "start dimming"

// DPT 9.004 2-octet float: 0.01 * mantissa * 2^exponent, sign always
// positive for illuminance.
std::uint16_t encode_illuminance(float lux) {
    if (!(lux > 0)) {
        return 0;
    }
    unsigned mantissa = static_cast<unsigned>(lux * 100.0F + 0.5F);
    std::uint8_t exponent = 0;
    while (mantissa > 0x7FF && exponent < 15) {
        mantissa = (mantissa + 1) >> 1;
        ++exponent;
    }
    if (mantissa > 0x7FF) {
        mantissa = 0x7FF;
    }
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(exponent) << 11) | mantissa);
}

Action switch_action(std::uint8_t channel, bool on) {
    return {knx::input_object_number(channel, knx::InputObjectFunction::Switch),
            {static_cast<std::uint8_t>(on ? 1 : 0), 0}, 1};
}

}  // namespace

void InputMapper::configure(const config::Configuration& config) noexcept {
    channels_ = config.inputs;
    // Keep accumulated state: a re-download changes bindings, not the
    // remembered toggle/dim direction.
}

bool InputMapper::bound(std::uint8_t device, std::uint8_t instance) const noexcept {
    for (const auto& channel : channels_) {
        if (channel.type != config::InputType::Unused && channel.device == device &&
            channel.instance == instance) {
            return true;
        }
    }
    return false;
}

void InputMapper::refresh_resolutions(const dali::DeviceInventory& inventory) noexcept {
    for (std::uint8_t channel = 0; channel < config::input_count; ++channel) {
        if (channels_[channel].type != config::InputType::Light) {
            continue;
        }
        resolutions_[channel] =
            inventory.resolution(channels_[channel].device, channels_[channel].instance)
                .value_or(0);
        state_[channel].has_lux = false;
    }
}

std::uint8_t InputMapper::resolution(std::uint8_t channel) const noexcept {
    return channel < config::input_count ? resolutions_[channel] : 0;
}

void InputMapper::switch_feedback(std::uint8_t channel, bool on) noexcept {
    if (channel < config::input_count) {
        state_[channel].on = on;
    }
}

bool InputMapper::toggle_state(std::uint8_t channel) const noexcept {
    return channel < config::input_count && state_[channel].on;
}

std::optional<std::uint8_t>
InputMapper::channel_for(const dali::device::InputEvent& event,
                         std::optional<std::uint8_t> reported_type) const noexcept {
    for (std::uint8_t index = 0; index < config::input_count; ++index) {
        const auto& channel = channels_[index];
        if (channel.type == config::InputType::Unused ||
            channel.device != event.address || channel.instance != event.instance) {
            continue;
        }
        if (reported_type) {
            const auto expected = static_cast<std::uint8_t>(channel.type);
            // Declared types Unused/Occupancy/Light map to Part 103 types
            // 1/3/4; Unused channels never reach this point.
            const auto part_type = expected == static_cast<std::uint8_t>(config::InputType::PushButton)
                                       ? dali::device::type::push_button
                                       : expected == static_cast<std::uint8_t>(config::InputType::Occupancy)
                                             ? dali::device::type::occupancy
                                             : dali::device::type::light;
            if (*reported_type != part_type) {
                return std::nullopt;
            }
        }
        return index;
    }
    return std::nullopt;
}

std::uint8_t InputMapper::map(const dali::device::InputEvent& event,
                              std::optional<std::uint8_t> reported_type,
                              std::array<Action, max_actions>& out) noexcept {
    const auto channel = channel_for(event, reported_type);
    if (!channel) {
        return 0;
    }
    switch (channels_[*channel].type) {
    case config::InputType::PushButton:
        return map_button(*channel, event.info, out);
    case config::InputType::Occupancy:
        return map_occupancy(*channel, event.info, out);
    case config::InputType::Light:
        return map_light(*channel, event.info, out);
    case config::InputType::Unused:
        break;
    }
    return 0;
}

std::uint8_t InputMapper::map_button(std::uint8_t channel, std::uint16_t info,
                                     std::array<Action, max_actions>& out) noexcept {
    const auto button = dali::device::decode_button(info);
    if (!button) {
        return 0;
    }
    const auto& config = channels_[channel];
    auto& state = state_[channel];
    std::uint8_t count = 0;
    switch (config.function) {
    case config::InputFunction::Toggle:
        if (*button == dali::device::ButtonEvent::ShortPress) {
            state.on = !state.on;
            out[count++] = switch_action(channel, state.on);
        }
        break;
    case config::InputFunction::On:
        if (*button == dali::device::ButtonEvent::ShortPress) {
            state.on = true;
            out[count++] = switch_action(channel, true);
        }
        break;
    case config::InputFunction::Off:
        if (*button == dali::device::ButtonEvent::ShortPress) {
            state.on = false;
            out[count++] = switch_action(channel, false);
        }
        break;
    case config::InputFunction::ToggleDim:
        if (*button == dali::device::ButtonEvent::ShortPress) {
            state.on = !state.on;
            out[count++] = switch_action(channel, state.on);
        } else if (*button == dali::device::ButtonEvent::LongStart) {
            state.dim_up = !state.dim_up;
            out[count++] = Action{
                knx::input_object_number(channel, knx::InputObjectFunction::Dimming),
                {static_cast<std::uint8_t>((state.dim_up ? 0x08 : 0x00) | dim_step), 0}, 1};
        } else if (*button == dali::device::ButtonEvent::LongStop) {
            out[count++] = Action{
                knx::input_object_number(channel, knx::InputObjectFunction::Dimming),
                {0x00, 0}, 1};
        }
        break;
    case config::InputFunction::Scene:
        if (*button == dali::device::ButtonEvent::ShortPress) {
            out[count++] = Action{
                knx::input_object_number(channel, knx::InputObjectFunction::Scene),
                {static_cast<std::uint8_t>(config.scene - 1), 0}, 1};
        }
        break;
    }
    return count;
}

std::uint8_t InputMapper::map_occupancy(std::uint8_t channel, std::uint16_t info,
                                        std::array<Action, max_actions>& out) noexcept {
    const auto occupancy = dali::device::decode_occupancy(info);
    if (!occupancy) {
        return 0;
    }
    state_[channel].on = occupancy->occupied;
    out[0] = switch_action(channel, occupancy->occupied);
    return 1;
}

std::uint8_t InputMapper::map_light(std::uint8_t channel, std::uint16_t info,
                                    std::array<Action, max_actions>& out) noexcept {
    const auto light = dali::device::decode_light(info);
    // Part 304 reports a relative illuminance; the instance resolution is
    // the lux step. Unknown resolution publishes the raw value (units of
    // one step) — documented in the manual.
    const auto step = resolutions_[channel];
    const float lux = static_cast<float>(light.illuminance) *
                      static_cast<float>(step != 0 ? step : 1);
    auto& state = state_[channel];
    const auto threshold = channels_[channel].threshold;
    if (state.has_lux && threshold > 0 && state.lux > 0) {
        const float change = (lux - state.lux) / state.lux * 100.0F;
        if (change < static_cast<float>(threshold) &&
            change > -static_cast<float>(threshold)) {
            return 0;
        }
    }
    state.has_lux = true;
    state.lux = lux;
    const auto encoded = encode_illuminance(lux);
    out[0] = {knx::input_object_number(channel, knx::InputObjectFunction::Illuminance),
              {static_cast<std::uint8_t>(encoded >> 8), static_cast<std::uint8_t>(encoded)},
              2};
    return 1;
}

}
