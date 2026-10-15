#include "lighting/LightingService.hpp"

#include <algorithm>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "config/Configuration.hpp"
#include "dali/Address.hpp"
#include "dali/ArcLevel.hpp"
#include "dali/ColourControl.hpp"
#include "dali/Commands.hpp"
#include "dali/Group.hpp"
#include "dali/Scaling.hpp"
#include "dali/Scene.hpp"
#include "dali/ShortAddress.hpp"
#include "dali/Transport.hpp"
#include "lighting/RelativeDimmer.hpp"
#include "storage/LevelStore.hpp"

namespace gateway::lighting {
namespace {

storage::Slot slot(storage::SlotKind kind, dali::Address address) noexcept {
    return {kind, address};
}

// QUERY COLOUR VALUE selectors (IEC 62386-209 Table 11, matching
// python-dali's QueryColourValueDTR).
constexpr std::uint8_t sel_x_coordinate = 0;
constexpr std::uint8_t sel_y_coordinate = 1;
constexpr std::uint8_t sel_colour_temperature = 2;
constexpr std::uint8_t sel_red_dimlevel = 9;
constexpr std::uint8_t sel_green_dimlevel = 10;
constexpr std::uint8_t sel_blue_dimlevel = 11;
constexpr std::uint8_t sel_white_dimlevel = 12;

constexpr std::uint8_t no_change = 0xFF;

std::uint8_t scale_components(std::uint8_t value) noexcept {
    return dali::channel_from_scaling(value);
}

}

LightingService::LightingService(dali::Transport& transport, storage::LevelStore& store) noexcept
    : transport_(transport), store_(store), colour_{transport} {}

void LightingService::configure(const config::Configuration& configuration) noexcept {
    config_ = configuration;
    for (std::size_t i = 0; i < gear_details_.size(); ++i) {
        if (config_.ballasts[i].switch_only) {
            gear_details_[i].switch_only = true;
            gear_details_[i].device_type = 7;
        }
    }
}

ControlResult LightingService::set_level(dali::Address address, dali::ArcLevel target) {
    if (const auto ch = address.as_short()) {
        const auto& det = gear_details_[ch->value()];
        if ((det.switch_only || det.device_type == 7U) && target.value() > 0U) {
            target = *dali::ArcLevel::from(dali::ArcLevel::maximum);
        }
    }
    return apply_level(address, target);
}

ControlResult LightingService::switch_on(dali::Address address) {
    const auto& policy = switch_on_policy(address);
    if (policy.policy == config::SwitchOnPolicy::FixedLevel) {
        return set_level(address, policy.fixed);
    }
    const auto remembered = last_active(address);
    const auto target =
        remembered && remembered->value() > 0 ? *remembered : *dali::ArcLevel::from(dali::ArcLevel::maximum);
    return set_level(address, target);
}

ControlResult LightingService::switch_off(dali::Address address) {
    return set_level(address, *dali::ArcLevel::from(0));
}

ControlResult LightingService::refresh(dali::ShortAddress address) {
    auto reply = transport_.read_level(address);
    if (reply.status == dali::TransportStatus::Ok && !reply.level) {
        reply.status = dali::TransportStatus::InvalidResponse;
    }
    if (reply.status != dali::TransportStatus::Ok) {
        channel_states_[address.value()] = {};
        return {ControlError::TransportFailure, reply.status, channel_states_[address.value()], {}, {}};
    }

    channel_states_[address.value()] = {reply.level, StateSource::Readback};
    remember_if_active(dali::Address::short_address(address), *reply.level);
    std::bitset<dali::ShortAddress::capacity> changed;
    changed.set(address.value());
    auto error = ControlError::None;
    if (reply.level->value() > 0) {
        ControlResult ignored{};
        persist(dali::Address::short_address(address), *reply.level, ignored);
        error = ignored.error;
    }
    return {error, reply.status, channel_states_[address.value()], changed, {}};
}

ControlResult LightingService::recover(dali::ShortAddress address) {
    auto result = refresh(address);
    if (result.transport_status != dali::TransportStatus::Unsupported) {
        // The gear kept its colour while we were off (none is persisted);
        // read it back so statuses reflect the bus, not our memory.
        const auto colour = refresh_colour(address);
        result.changed_colour_channels |= colour.changed_colour_channels;
        result.changed_colour_groups |= colour.changed_colour_groups;
        return result;
    }

    const auto saved = store_.load(slot(storage::SlotKind::Target, dali::Address::short_address(address)));
    if (saved.status == storage::ReadStatus::NotFound) {
        return {
            ControlError::NoSavedTarget, result.transport_status, channel_states_[address.value()], {}, {}};
    }
    if (saved.status != storage::ReadStatus::Ok || !saved.level) {
        return {
            ControlError::StorageFailure, result.transport_status, channel_states_[address.value()], {}, {}};
    }

    const auto status = transport_.set_level(dali::Address::short_address(address), *saved.level);
    if (status != dali::TransportStatus::Ok) {
        return {ControlError::TransportFailure, status, channel_states_[address.value()], {}, {}};
    }

    channel_states_[address.value()] = {saved.level, StateSource::RestoredCommand};
    remember_if_active(dali::Address::short_address(address), *saved.level);
    std::bitset<dali::ShortAddress::capacity> changed;
    changed.set(address.value());
    return {ControlError::None, status, channel_states_[address.value()], changed, {}};
}

ControlResult LightingService::reconcile_groups(dali::ShortAddress address) {
    const auto read = transport_.read_groups(address);
    if (read.status != dali::TransportStatus::Ok || !read.groups) {
        return {
            ControlError::TransportFailure,
            read.status == dali::TransportStatus::Ok ? dali::TransportStatus::InvalidResponse : read.status,
            channel_states_[address.value()],
            {},
            {}};
    }
    const std::uint16_t wanted = config_.group_masks[address.value()];
    auto failed = dali::TransportStatus::Ok;
    for (unsigned group = 0; group < dali::Group::capacity; ++group) {
        const bool member = (wanted >> group & 1U) != 0;
        if (((*read.groups >> group & 1U) != 0) == member) {
            continue;
        }
        const auto status = transport_.set_group_membership(address, *dali::Group::from(group), member);
        if (status != dali::TransportStatus::Ok && failed == dali::TransportStatus::Ok) {
            failed = status;
        }
    }
    const auto& bp = config_.ballasts[address.value()];
    const bool has_custom_dt6 =
        (bp.min_level != 1U || bp.max_level != 254U || bp.power_on_level != 254U ||
         bp.system_failure_level != 254U || bp.fade_time != 0xFFU || bp.fade_rate != 0xFFU);
    if (failed == dali::TransportStatus::Ok && has_custom_dt6) {
        GearConfig gcfg{};
        gcfg.min_level = bp.min_level != 1U ? bp.min_level : 0xFFU;
        gcfg.max_level = bp.max_level != 254U ? bp.max_level : 0xFFU;
        gcfg.power_on_level = bp.power_on_level != 254U ? bp.power_on_level : 0xFFU;
        gcfg.system_failure_level = bp.system_failure_level != 254U ? bp.system_failure_level : 0xFFU;
        gcfg.fade_time = bp.fade_time;
        gcfg.fade_rate = bp.fade_rate;
        const auto dt6_res = configure_gear(address, gcfg);
        if (dt6_res.error != ControlError::None) {
            failed = dt6_res.transport_status;
        }
    }
    return {failed == dali::TransportStatus::Ok ? ControlError::None : ControlError::TransportFailure,
            failed,
            channel_states_[address.value()],
            {},
            {}};
}

std::bitset<dali::ShortAddress::capacity> LightingService::affected_channels(
    dali::Address address) const noexcept {
    std::bitset<dali::ShortAddress::capacity> affected;
    switch (address.kind()) {
    case dali::AddressKind::Short:
        affected.set(address.as_short()->value());
        break;
    case dali::AddressKind::Group: {
        const auto group = address.as_group()->value();
        for (std::size_t channel = 0; channel < channel_states_.size(); ++channel) {
            if ((config_.group_masks[channel] >> group & 1U) != 0) {
                affected.set(channel);
            }
        }
        break;
    }
    case dali::AddressKind::Broadcast:
        affected.set();
        break;
    }
    return affected;
}

void LightingService::record_level(dali::Address address, dali::ArcLevel target,
                                   ControlResult& result) noexcept {
    const auto affected = affected_channels(address);
    for (std::size_t channel = 0; channel < channel_states_.size(); ++channel) {
        if (affected.test(channel)) {
            channel_states_[channel] = {target, StateSource::Commanded};
            // RGB/RGBW and xyY statuses report colour x brightness: the
            // brightness half just changed, so the report did too.
            if (channel_colours_[channel].kind == ColourKind::Rgb ||
                channel_colours_[channel].kind == ColourKind::Rgbw ||
                channel_colours_[channel].kind == ColourKind::XyY) {
                result.changed_colour_channels.set(channel);
            }
        }
    }
    if (const auto group = address.as_group()) {
        group_states_[group->value()] = {target, StateSource::Commanded};
        result.changed_groups.set(group->value());
        if (group_colours_[group->value()].kind == ColourKind::Rgb ||
            group_colours_[group->value()].kind == ColourKind::Rgbw ||
            group_colours_[group->value()].kind == ColourKind::XyY) {
            result.changed_colour_groups.set(group->value());
        }
    }
    if (address.kind() == dali::AddressKind::Broadcast) {
        for (std::size_t group = 0; group < group_states_.size(); ++group) {
            group_states_[group] = {target, StateSource::Commanded};
            if (group_colours_[group].kind == ColourKind::Rgb ||
                group_colours_[group].kind == ColourKind::Rgbw ||
                group_colours_[group].kind == ColourKind::XyY) {
                result.changed_colour_groups.set(group);
            }
        }
        result.changed_groups.set();
    }
    result.changed_channels = affected;

    persist(address, target, result);
    remember_if_active(address, target);
}

ControlResult LightingService::apply_level(dali::Address address, dali::ArcLevel target) {
    const auto status = transport_.set_level(address, target);
    ControlResult result{ControlError::None, status, {}, {}, {}};

    if (status != dali::TransportStatus::Ok) {
        result.error = ControlError::TransportFailure;
        result.state = state_of(address);
        return result;
    }

    record_level(address, target, result);

    result.state = address.kind() == dali::AddressKind::Broadcast ? State{target, StateSource::Commanded}
                                                                  : state_of(address);
    return result;
}

ControlResult LightingService::dim_step(dali::Address address, DimDirection direction, bool first) {
    const auto command = direction == DimDirection::Up
                             ? (first ? dali::GearCommand::OnAndStepUp : dali::GearCommand::Up)
                             : dali::GearCommand::Down;
    const auto status = transport_.send(address, command);
    return {status == dali::TransportStatus::Ok ? ControlError::None : ControlError::TransportFailure,
            status,
            state_of(address),
            {},
            {}};
}

ControlResult LightingService::finish_dimming(dali::Address address) {
    ControlResult result{ControlError::None, dali::TransportStatus::Ok, {}, {}, {}};
    std::bitset<dali::ShortAddress::capacity> members;
    std::bitset<dali::Group::capacity> groups_to_derive;
    switch (address.kind()) {
    case dali::AddressKind::Short:
        members.set(address.as_short()->value());
        break;
    case dali::AddressKind::Group:
        for (std::size_t channel = 0; channel < members.size(); ++channel) {
            if ((config_.group_masks[channel] >> address.as_group()->value() & 1U) != 0) {
                members.set(channel);
            }
        }
        groups_to_derive.set(address.as_group()->value());
        break;
    case dali::AddressKind::Broadcast:
        members.set();
        groups_to_derive.set();
        break;
    }

    std::bitset<dali::ShortAddress::capacity> answered;
    for (std::size_t channel = 0; channel < members.size(); ++channel) {
        if (!members.test(channel)) {
            continue;
        }
        const auto readback = refresh(*dali::ShortAddress::from(channel));
        if (readback.transport_status == dali::TransportStatus::Ok && readback.state.level) {
            answered.set(channel);
            result.changed_channels.set(channel);
            // Brightness moved under a colour state: colour x brightness
            // changed even though the colour components did not.
            const auto kind = channel_colours_[channel].kind;
            if (kind == ColourKind::Rgb || kind == ColourKind::Rgbw || kind == ColourKind::XyY) {
                result.changed_colour_channels.set(channel);
            }
        } else if (result.transport_status == dali::TransportStatus::Ok) {
            result.error = ControlError::TransportFailure;
            result.transport_status = readback.transport_status;
        }
    }

    for (std::size_t group = 0; group < groups_to_derive.size(); ++group) {
        if (!groups_to_derive.test(group)) {
            continue;
        }
        for (std::size_t channel = 0; channel < members.size(); ++channel) {
            if (answered.test(channel) && (config_.group_masks[channel] >> group & 1U) != 0) {
                group_states_[group] = channel_states_[channel];
                result.changed_groups.set(group);
                const auto kind = group_colours_[group].kind;
                if (kind == ColourKind::Rgb || kind == ColourKind::Rgbw || kind == ColourKind::XyY) {
                    result.changed_colour_groups.set(group);
                }
                break;
            }
        }
    }

    result.state = state_of(address);
    return result;
}

ControlResult LightingService::recall_scene(dali::Scene scene) {
    const auto status = transport_.go_to_scene(dali::Address::broadcast(), scene);
    if (status != dali::TransportStatus::Ok) {
        return {ControlError::TransportFailure, status, {}, {}, {}};
    }
    auto result = finish_dimming(dali::Address::broadcast());
    // A scene also recalls colour on DT8 gear; read it back where probed.
    for (std::size_t channel = 0; channel < capabilities_.size(); ++channel) {
        if (!capabilities_[channel]) {
            continue;
        }
        const auto colour = refresh_colour(*dali::ShortAddress::from(channel));
        result.changed_colour_channels |= colour.changed_colour_channels;
    }
    return result;
}

ControlResult LightingService::store_scene(dali::Scene scene) {
    // The ColourControl sequence copies the reported colour into the
    // temporary registers before STORE DTR AS SCENE, so DT8 gear learns the
    // colour with the level; non-DT8 gear ignores the extended frames.
    const auto status = colour_.store_scene(dali::Address::broadcast(), scene);
    return {status == dali::TransportStatus::Ok ? ControlError::None : ControlError::TransportFailure,
            status,
            {},
            {},
            {}};
}

State LightingService::state_of(dali::Address address) const noexcept {
    if (const auto channel = address.as_short()) {
        return channel_states_[channel->value()];
    }
    if (const auto group = address.as_group()) {
        return group_states_[group->value()];
    }
    return {};
}

void LightingService::persist(dali::Address address, dali::ArcLevel level, ControlResult& result) noexcept {
    bool ok = true;
    for (std::size_t channel = 0; channel < channel_states_.size(); ++channel) {
        if (result.changed_channels.test(channel)) {
            const auto short_address = dali::Address::short_address(*dali::ShortAddress::from(channel));
            ok &= store_.save(slot(storage::SlotKind::Target, short_address), level);
            if (level.value() > 0) {
                ok &= store_.save(slot(storage::SlotKind::LastActive, short_address), level);
            }
        }
    }
    ok &= store_.save(slot(storage::SlotKind::LastActive, address), level);
    ok &= store_.commit();
    if (!ok) {
        result.error = ControlError::StorageFailure;
    }
}

std::optional<dali::ArcLevel> LightingService::last_active(dali::Address address) noexcept {
    auto& entry = address.as_short()   ? channel_last_active_[address.as_short()->value()]
                  : address.as_group() ? group_last_active_[address.as_group()->value()]
                                       : broadcast_last_active_;
    if (!entry.loaded) {
        entry.loaded = true;
        const auto read = store_.load(slot(storage::SlotKind::LastActive, address));
        if (read.status == storage::ReadStatus::Ok) {
            entry.level = read.level;
        }
    }
    return entry.level;
}

const config::SwitchOn& LightingService::switch_on_policy(dali::Address address) const noexcept {
    if (const auto group = address.as_group()) {
        return config_.groups[group->value()];
    }
    if (const auto channel = address.as_short()) {
        return config_.channels[channel->value()];
    }
    return config_.broadcast;
}

void LightingService::remember_if_active(dali::Address address, dali::ArcLevel level) noexcept {
    if (level.value() == 0) {
        return;
    }
    auto& entry = address.as_short()   ? channel_last_active_[address.as_short()->value()]
                  : address.as_group() ? group_last_active_[address.as_group()->value()]
                                       : broadcast_last_active_;
    entry.loaded = true;
    entry.level = level;
    if (const auto group = address.as_group()) {
        for (std::size_t channel = 0; channel < channel_states_.size(); ++channel) {
            if ((config_.group_masks[channel] >> group->value() & 1U) != 0) {
                channel_last_active_[channel].loaded = true;
                channel_last_active_[channel].level = level;
            }
        }
    }
    if (address.kind() == dali::AddressKind::Broadcast) {
        for (auto& channel : channel_last_active_) {
            channel.loaded = true;
            channel.level = level;
        }
        for (auto& group : group_last_active_) {
            group.loaded = true;
            group.level = level;
        }
    }
}

State LightingService::state(dali::ShortAddress address) const noexcept {
    return channel_states_[address.value()];
}

State LightingService::state(dali::Group group) const noexcept {
    return group_states_[group.value()];
}

void LightingService::set_capabilities(dali::ShortAddress address, dali::ColourCapabilities value) noexcept {
    capabilities_[address.value()] = value;
}

std::optional<dali::ColourCapabilities> LightingService::capabilities(
    dali::ShortAddress address) const noexcept {
    return capabilities_[address.value()];
}

ColourState LightingService::colour_state(dali::ShortAddress address) const noexcept {
    return channel_colours_[address.value()];
}

ColourState LightingService::colour_state(dali::Group group) const noexcept {
    return group_colours_[group.value()];
}

bool LightingService::colour_supported(dali::Address address, ColourKind kind) const noexcept {
    const auto channel = address.as_short();
    if (!channel) {
        return true;
    }
    const auto& caps = capabilities_[channel->value()];
    if (!caps) {
        return true;
    }
    switch (kind) {
    case ColourKind::Temperature:
        return caps->tc_capable;
    case ColourKind::Rgb:
        return caps->rgbwaf_channels >= 3;
    case ColourKind::Rgbw:
        return caps->rgbwaf_channels >= 4;
    case ColourKind::XyY:
        return caps->xy_capable;
    case ColourKind::None:
        break;
    }
    return false;
}

void LightingService::apply_colour_state(dali::Address address, const ColourState& colour,
                                         ControlResult& result) noexcept {
    const auto affected = affected_channels(address);
    for (std::size_t channel = 0; channel < channel_colours_.size(); ++channel) {
        if (affected.test(channel)) {
            channel_colours_[channel] = colour;
            result.changed_colour_channels.set(channel);
        }
    }
    if (const auto group = address.as_group()) {
        group_colours_[group->value()] = colour;
        result.changed_colour_groups.set(group->value());
    }
    if (address.kind() == dali::AddressKind::Broadcast) {
        for (auto& group_colour : group_colours_) {
            group_colour = colour;
        }
        result.changed_colour_groups.set();
    }
}

ControlResult LightingService::set_colour_temperature(dali::Address address, std::uint16_t kelvin) {
    const auto mirek = dali::mirek_from_kelvin(kelvin);
    if (!mirek) {
        return {ControlError::InvalidValue, dali::TransportStatus::Ok, state_of(address), {}, {}};
    }
    if (!colour_supported(address, ColourKind::Temperature)) {
        return {ControlError::Unsupported, dali::TransportStatus::Unsupported, state_of(address), {}, {}};
    }
    const auto status = colour_.set_colour_temperature(address, *mirek);
    ControlResult result{ControlError::None, status, {}, {}, {}};
    if (status != dali::TransportStatus::Ok) {
        result.error = ControlError::TransportFailure;
        result.state = state_of(address);
        return result;
    }
    ColourState colour{ColourKind::Temperature, StateSource::Commanded};
    colour.temperature_kelvin = kelvin;
    apply_colour_state(address, colour, result);
    result.state = state_of(address);
    if (const auto channel = address.as_short(); channel && capabilities_[channel->value()]) {
        const auto readback = refresh_colour(*channel);
        result.changed_colour_channels |= readback.changed_colour_channels;
        if (result.error == ControlError::None && readback.error != ControlError::None) {
            result.error = readback.error;
            result.transport_status = readback.transport_status;
        }
    }
    return result;
}

ControlResult LightingService::set_rgb(dali::Address address, std::uint8_t red, std::uint8_t green,
                                       std::uint8_t blue) {
    return apply_rgbwaf(address, red, green, blue, 0, no_change, no_change, 0b1110, ColourKind::Rgb);
}

ControlResult LightingService::set_rgbw(dali::Address address, std::uint8_t red, std::uint8_t green,
                                        std::uint8_t blue, std::uint8_t white, std::uint8_t validity) {
    return apply_rgbwaf(address, red, green, blue, white, no_change, no_change, validity & 0b1111,
                        ColourKind::Rgbw);
}

ControlResult LightingService::apply_rgbwaf(dali::Address address, std::uint8_t red, std::uint8_t green,
                                            std::uint8_t blue, std::uint8_t white, std::uint8_t amber,
                                            std::uint8_t freecolour, std::uint8_t validity, ColourKind kind) {
    if (!colour_supported(address, kind)) {
        return {ControlError::Unsupported, dali::TransportStatus::Unsupported, state_of(address), {}, {}};
    }
    ControlResult result{ControlError::None, dali::TransportStatus::Ok, {}, {}, {}};

    const bool r_valid = (validity & 0b1000) != 0;
    const bool g_valid = (validity & 0b0100) != 0;
    const bool b_valid = (validity & 0b0010) != 0;
    const bool w_valid = (validity & 0b0001) != 0;
    const std::uint8_t brightness =
        std::max({r_valid ? red : std::uint8_t{0}, g_valid ? green : std::uint8_t{0},
                  b_valid ? blue : std::uint8_t{0}, w_valid ? white : std::uint8_t{0}});

    const auto dali_channel = [](bool valid, std::uint8_t knx_value) {
        return valid ? scale_components(knx_value) : no_change;
    };
    const auto status =
        colour_.set_rgbwaf(address, dali_channel(r_valid, red), dali_channel(g_valid, green),
                           dali_channel(b_valid, blue), dali_channel(w_valid, white), amber, freecolour);
    if (status != dali::TransportStatus::Ok) {
        result.error = ControlError::TransportFailure;
        result.transport_status = status;
        result.state = state_of(address);
        return result;
    }
    // The colour command does not touch the actual level: drive the arc to
    // the brightest valid component so the lamp honours the KNX brightness.
    const auto level = dali::arc_from_scaling(brightness);
    const auto level_status = transport_.set_level(address, level);
    if (level_status != dali::TransportStatus::Ok) {
        result.error = ControlError::TransportFailure;
        result.transport_status = level_status;
        result.state = state_of(address);
        return result;
    }
    record_level(address, level, result);

    ColourState colour{kind, StateSource::Commanded};
    colour.r = r_valid ? red : std::uint8_t{0};
    colour.g = g_valid ? green : std::uint8_t{0};
    colour.b = b_valid ? blue : std::uint8_t{0};
    colour.w = w_valid ? white : std::uint8_t{0};
    colour.rgbw_validity = validity;
    colour.colour_scaling = brightness;
    apply_colour_state(address, colour, result);
    result.state = state_of(address);

    if (const auto channel = address.as_short(); channel && capabilities_[channel->value()]) {
        const auto readback = refresh_colour(*channel);
        result.changed_colour_channels |= readback.changed_colour_channels;
        if (result.error == ControlError::None && readback.error != ControlError::None) {
            result.error = readback.error;
            result.transport_status = readback.transport_status;
        }
    }
    return result;
}

ControlResult LightingService::set_xy(dali::Address address, std::uint16_t x, std::uint16_t y, bool xy_valid,
                                      std::uint8_t brightness, bool brightness_valid) {
    if (!colour_supported(address, ColourKind::XyY)) {
        return {ControlError::Unsupported, dali::TransportStatus::Unsupported, state_of(address), {}, {}};
    }
    ControlResult result{ControlError::None, dali::TransportStatus::Ok, {}, {}, {}};
    if (xy_valid) {
        const auto status =
            colour_.set_xy(address, *dali::XyCoordinate::from(x), *dali::XyCoordinate::from(y));
        if (status != dali::TransportStatus::Ok) {
            result.error = ControlError::TransportFailure;
            result.transport_status = status;
            result.state = state_of(address);
            return result;
        }
        ColourState colour{ColourKind::XyY, StateSource::Commanded};
        colour.x = x;
        colour.y = y;
        apply_colour_state(address, colour, result);
    }
    if (brightness_valid) {
        const auto level = dali::arc_from_scaling(brightness);
        const auto status = transport_.set_level(address, level);
        if (status != dali::TransportStatus::Ok) {
            result.error = ControlError::TransportFailure;
            result.transport_status = status;
            result.state = state_of(address);
            return result;
        }
        record_level(address, level, result);
    }
    result.state = state_of(address);
    if (const auto channel = address.as_short(); channel && capabilities_[channel->value()] && xy_valid) {
        const auto readback = refresh_colour(*channel);
        result.changed_colour_channels |= readback.changed_colour_channels;
        if (result.error == ControlError::None && readback.error != ControlError::None) {
            result.error = readback.error;
            result.transport_status = readback.transport_status;
        }
    }
    return result;
}

ControlResult LightingService::colour_temperature_step(dali::Address address, DimDirection direction) {
    if (!colour_supported(address, ColourKind::Temperature)) {
        return {ControlError::Unsupported, dali::TransportStatus::Unsupported, state_of(address), {}, {}};
    }
    // DPT 3.007 "increase" means a higher Kelvin value, i.e. a cooler white;
    // DALI steps in mirek so increase maps to COLOUR TEMPERATURE STEP COOLER.
    const auto status = colour_.step_colour_temperature(
        address, direction == DimDirection::Up ? dali::ColourStep::Cooler : dali::ColourStep::Warmer);
    return {status == dali::TransportStatus::Ok ? ControlError::None : ControlError::TransportFailure,
            status,
            state_of(address),
            {},
            {}};
}

ControlResult LightingService::finish_colour_temperature(dali::Address address) {
    ControlResult result{ControlError::None, dali::TransportStatus::Ok, {}, {}, {}};
    const auto affected = affected_channels(address);

    for (std::size_t channel = 0; channel < channel_colours_.size(); ++channel) {
        if (!affected.test(channel) || !capabilities_[channel]) {
            continue;
        }
        const auto refreshed = refresh_colour(*dali::ShortAddress::from(static_cast<std::uint8_t>(channel)));
        result.changed_colour_channels |= refreshed.changed_colour_channels;
        if (result.error == ControlError::None && refreshed.error != ControlError::None) {
            result.error = refreshed.error;
            result.transport_status = refreshed.transport_status;
        }
    }

    const auto derive_group = [&](std::size_t group) {
        for (std::size_t channel = 0; channel < channel_colours_.size(); ++channel) {
            if (result.changed_colour_channels.test(channel) &&
                (config_.group_masks[channel] >> group & 1U) != 0) {
                group_colours_[group] = channel_colours_[channel];
                result.changed_colour_groups.set(group);
                break;
            }
        }
    };
    if (const auto group = address.as_group()) {
        derive_group(group->value());
    }
    if (address.kind() == dali::AddressKind::Broadcast) {
        for (std::size_t group = 0; group < group_colours_.size(); ++group) {
            derive_group(group);
        }
    }
    result.state = state_of(address);
    return result;
}

ControlResult LightingService::refresh_capabilities(dali::ShortAddress address) {
    const auto read = colour_.query_colour_type_features(address);
    if (read.status != dali::TransportStatus::Ok || !read.value) {
        // No answer means the gear does not implement device type 8; record
        // "no capability" rather than leaving the slot unknown.
        capabilities_[address.value()] = dali::ColourCapabilities{};
        const auto error =
            read.status == dali::TransportStatus::Timeout || read.status == dali::TransportStatus::Collision
                ? ControlError::None
                : ControlError::TransportFailure;
        return {error, read.status, {}, {}, {}};
    }
    capabilities_[address.value()] = *read.value;
    return {ControlError::None, read.status, {}, {}, {}};
}

ControlResult LightingService::refresh_colour(dali::ShortAddress address) {
    ControlResult result{ControlError::None, dali::TransportStatus::Ok, {}, {}, {}};
    const auto caps = capabilities_[address.value()];
    if (!caps || (!caps->tc_capable && !caps->xy_capable && caps->rgbwaf_channels == 0)) {
        return result;
    }

    auto kind = channel_colours_[address.value()].kind;
    const auto status_read = colour_.query_colour_status(address);
    if (status_read.status == dali::TransportStatus::Ok && status_read.value) {
        if (status_read.value->tc_active && caps->tc_capable) {
            kind = ColourKind::Temperature;
        } else if (status_read.value->rgbwaf_active && caps->rgbwaf_channels >= 4) {
            kind = ColourKind::Rgbw;
        } else if (status_read.value->rgbwaf_active && caps->rgbwaf_channels >= 3) {
            kind = ColourKind::Rgb;
        } else if (status_read.value->xy_active && caps->xy_capable) {
            kind = ColourKind::XyY;
        }
    }
    if (kind == ColourKind::None) {
        if (caps->tc_capable) {
            kind = ColourKind::Temperature;
        } else if (caps->rgbwaf_channels >= 4) {
            kind = ColourKind::Rgbw;
        } else if (caps->rgbwaf_channels >= 3) {
            kind = ColourKind::Rgb;
        } else if (caps->xy_capable) {
            kind = ColourKind::XyY;
        } else {
            return result;
        }
    }

    ColourState colour{kind, StateSource::Readback};
    switch (kind) {
    case ColourKind::Temperature: {
        const auto read = colour_.query_colour_value(address, sel_colour_temperature);
        if (read.status != dali::TransportStatus::Ok || !read.msb || !read.lsb) {
            result.error = ControlError::TransportFailure;
            result.transport_status = read.status;
            return result;
        }
        const auto mirek = dali::Mirek::from(static_cast<std::uint16_t>(*read.msb << 8U | *read.lsb));
        colour.temperature_kelvin =
            mirek ? static_cast<std::uint16_t>(dali::kelvin_from_mirek(*mirek)) : std::uint16_t{0};
        break;
    }
    case ColourKind::Rgb:
    case ColourKind::Rgbw: {
        const auto read = [&](std::uint8_t selector) -> std::optional<std::uint8_t> {
            const auto byte = colour_.query_colour_value(address, selector);
            if (byte.status != dali::TransportStatus::Ok || !byte.msb) {
                result.error = ControlError::TransportFailure;
                result.transport_status = byte.status;
                return std::nullopt;
            }
            return byte.msb;
        };
        const auto r = read(sel_red_dimlevel);
        const auto g = read(sel_green_dimlevel);
        const auto b = read(sel_blue_dimlevel);
        const auto w = kind == ColourKind::Rgbw ? read(sel_white_dimlevel) : std::optional<std::uint8_t>{0};
        if (result.error != ControlError::None || !r || !g || !b || !w) {
            return result;
        }
        colour.r = dali::scaling_from_channel(*r);
        colour.g = dali::scaling_from_channel(*g);
        colour.b = dali::scaling_from_channel(*b);
        colour.w = dali::scaling_from_channel(*w);
        colour.rgbw_validity = kind == ColourKind::Rgbw ? std::uint8_t{0b1111} : std::uint8_t{0b1110};
        colour.colour_scaling = std::max({colour.r, colour.g, colour.b, colour.w, std::uint8_t{1}});
        break;
    }
    case ColourKind::XyY: {
        const auto read = [&](std::uint8_t selector) -> std::optional<std::uint16_t> {
            const auto word = colour_.query_colour_value(address, selector);
            if (word.status != dali::TransportStatus::Ok || !word.msb || !word.lsb) {
                result.error = ControlError::TransportFailure;
                result.transport_status = word.status;
                return std::nullopt;
            }
            return static_cast<std::uint16_t>(*word.msb << 8U | *word.lsb);
        };
        const auto x = read(sel_x_coordinate);
        const auto y = read(sel_y_coordinate);
        if (!x || !y) {
            return result;
        }
        colour.x = *x;
        colour.y = *y;
        break;
    }
    case ColourKind::None:
        return result;
    }

    channel_colours_[address.value()] = colour;
    result.changed_colour_channels.set(address.value());
    return result;
}

std::optional<ColourReport> LightingService::colour_report(dali::Address address) const noexcept {
    const ColourState* colour = nullptr;
    std::uint8_t scaling = 0;
    const auto scaling_of = [](const State& state) {
        return state.level ? dali::scaling_from_arc(*state.level) : std::uint8_t{0};
    };
    if (const auto channel = address.as_short()) {
        colour = &channel_colours_[channel->value()];
        scaling = scaling_of(channel_states_[channel->value()]);
    } else if (const auto group = address.as_group()) {
        colour = &group_colours_[group->value()];
        scaling = scaling_of(group_states_[group->value()]);
    } else {
        return std::nullopt;
    }
    if (colour->kind == ColourKind::None) {
        return std::nullopt;
    }

    ColourReport report{colour->kind, {}, {}, {}};
    switch (colour->kind) {
    case ColourKind::Temperature:
        report.kelvin = colour->temperature_kelvin;
        break;
    case ColourKind::Rgb:
    case ColourKind::Rgbw: {
        const auto rescale = [&](std::uint8_t component) -> std::uint8_t {
            if (colour->colour_scaling == 0) {
                return component;
            }
            return static_cast<std::uint8_t>((component * scaling + colour->colour_scaling / 2) /
                                             colour->colour_scaling);
        };
        report.r = rescale(colour->r);
        report.g = rescale(colour->g);
        report.b = rescale(colour->b);
        report.w = rescale(colour->w);
        report.rgbw_validity = colour->rgbw_validity;
        break;
    }
    case ColourKind::XyY:
        report.x = colour->x;
        report.y = colour->y;
        report.brightness = scaling;
        break;
    case ColourKind::None:
        break;
    }
    return report;
}

void LightingService::record_status_byte(dali::ShortAddress address, std::uint8_t status_byte) noexcept {
    gear_details_[address.value()].status_byte = status_byte;
}

const GearDetails& LightingService::gear_details(dali::ShortAddress address) const noexcept {
    return gear_details_[address.value()];
}

std::uint16_t LightingService::effective_power_deciwatts(dali::ShortAddress address) const noexcept {
    const auto& details = gear_details_[address.value()];
    if (details.d4i_power_supported && !details.use_manual_power_override) {
        return details.d4i_active_power_deciwatts;
    }
    if (!details.power_configured && details.full_power_deciwatts == 0 &&
        details.idle_power_centiwatts == 0) {
        return 0;
    }
    const auto st = channel_states_[address.value()];
    const std::uint8_t arc = st.level ? st.level->value() : 0U;
    if (arc == 0) {
        return static_cast<std::uint16_t>((details.idle_power_centiwatts + 5U) / 10U);
    }
    // Convert DALI logarithmic arc (1..254) to linear KNX scaling (1..255), then scale 100% lamp power:
    const unsigned lin = dali::scaling_from_arc(*dali::ArcLevel::from(arc));
    const unsigned lamp_centiwatts =
        (static_cast<unsigned>(details.full_power_deciwatts) * 10U * lin + 127U) / 255U;
    return static_cast<std::uint16_t>((details.idle_power_centiwatts + lamp_centiwatts + 5U) / 10U);
}

ControlResult LightingService::query_gear_details(dali::ShortAddress address) {
    ControlResult result{};
    auto& details = gear_details_[address.value()];

    const auto st = transport_.query_status(address);
    if (st.status != dali::TransportStatus::Ok || !st.gear_status) {
        result.error = ControlError::TransportFailure;
        result.transport_status = st.status;
        return result;
    }
    details.status_byte = *st.gear_status;

    if (const auto dev_type = transport_.query(address, dali::cmd::query_device_type);
        dev_type.status == dali::TransportStatus::Ok && dev_type.value) {
        details.device_type = *dev_type.value;
        if (details.device_type == 7U || config_.ballasts[address.value()].switch_only) {
            details.switch_only = true;
        }
    }
    if (const auto ver = transport_.query(address, dali::cmd::query_version_number);
        ver.status == dali::TransportStatus::Ok && ver.value) {
        details.version_number = *ver.value;
    }
    if (const auto phys = transport_.query(address, dali::cmd::query_physical_minimum);
        phys.status == dali::TransportStatus::Ok && phys.value) {
        details.physical_min = *phys.value;
    }
    if (const auto min_lvl = transport_.query(address, dali::cmd::query_min_level);
        min_lvl.status == dali::TransportStatus::Ok && min_lvl.value) {
        details.min_level = *min_lvl.value;
    }
    if (const auto max_lvl = transport_.query(address, dali::cmd::query_max_level);
        max_lvl.status == dali::TransportStatus::Ok && max_lvl.value) {
        details.max_level = *max_lvl.value;
    }
    if (const auto pon_lvl = transport_.query(address, dali::cmd::query_power_on_level);
        pon_lvl.status == dali::TransportStatus::Ok && pon_lvl.value) {
        details.power_on_level = *pon_lvl.value;
    }
    if (const auto sys_lvl = transport_.query(address, dali::cmd::query_system_failure_level);
        sys_lvl.status == dali::TransportStatus::Ok && sys_lvl.value) {
        details.system_failure_level = *sys_lvl.value;
    }
    if (const auto fade = transport_.query(address, dali::cmd::query_fade_time_rate);
        fade.status == dali::TransportStatus::Ok && fade.value) {
        details.fade_time = static_cast<std::uint8_t>((*fade.value >> 4U) & 0x0FU);
        details.fade_rate = static_cast<std::uint8_t>(*fade.value & 0x0FU);
    }
    // IEC 62386-207 (DT6 LED module): QUERY DIMMING CURVE (0xEE) preceded by ENABLE DEVICE TYPE 6
    if (details.device_type == 6U || details.device_type == 8U) {
        if (transport_.enable_device_type(6U) == dali::TransportStatus::Ok) {
            if (const auto curve = transport_.query(address, 0xEEU);
                curve.status == dali::TransportStatus::Ok && curve.value) {
                details.dimming_curve = (*curve.value == 1U) ? 1U : 0U;
            }
        }
    }
    if (const auto grp = transport_.read_groups(address);
        grp.status == dali::TransportStatus::Ok && grp.groups) {
        details.groups = *grp.groups;
    }
    for (unsigned s = 0; s < 16; ++s) {
        if (const auto sc =
                transport_.query(address, static_cast<std::uint8_t>(dali::cmd::query_scene_level_0 + s));
            sc.status == dali::TransportStatus::Ok && sc.value) {
            details.scenes[s] = *sc.value;
        }
    }

    // Probe IEC 62386-252 (D4i Memory Bank 202 — Active Energy & Active Power):
    // Bank ID at offset 0x02 must equal 202 (0xCA).
    if (transport_.set_dtr(dali::Dtr::Dtr1, 202U) == dali::TransportStatus::Ok &&
        transport_.set_dtr(dali::Dtr::Dtr0, 0x02U) == dali::TransportStatus::Ok) {
        const auto bank_id = transport_.query(address, dali::cmd::read_memory_location);
        if (bank_id.status == dali::TransportStatus::Ok && bank_id.value && *bank_id.value == 202U) {
            details.d4i_power_supported = true;
            const auto read_mb_byte = [&](std::uint8_t offset) -> std::optional<std::uint8_t> {
                if (transport_.set_dtr(dali::Dtr::Dtr0, offset) != dali::TransportStatus::Ok) {
                    return std::nullopt;
                }
                const auto r = transport_.query(address, dali::cmd::read_memory_location);
                if (r.status == dali::TransportStatus::Ok) {
                    return r.value;
                }
                return std::nullopt;
            };
            const auto decode_scale_factor = [](std::uint8_t raw) -> int {
                return raw >= 128U ? (static_cast<int>(raw) - 256) : static_cast<int>(raw);
            };
            // Active Power: scale factor at 0x0A (int8), 32-bit value at 0x0B..0x0E
            const auto p_scale = read_mb_byte(0x0AU);
            const auto p0 = read_mb_byte(0x0BU);
            const auto p1 = read_mb_byte(0x0CU);
            const auto p2 = read_mb_byte(0x0DU);
            const auto p3 = read_mb_byte(0x0EU);
            if (p_scale && p0 && p1 && p2 && p3) {
                const std::uint32_t raw_p =
                    (static_cast<std::uint32_t>(*p0) << 24U) | (static_cast<std::uint32_t>(*p1) << 16U) |
                    (static_cast<std::uint32_t>(*p2) << 8U) | static_cast<std::uint32_t>(*p3);
                // Convert raw_p * 10^sf W into 0.1 W (deciwatts = raw_p * 10^(sf + 1))
                std::uint32_t deciwatts = raw_p;
                int exp10 = decode_scale_factor(*p_scale) + 1;
                while (exp10 > 0 && deciwatts < 65535U) {
                    deciwatts *= 10U;
                    --exp10;
                }
                while (exp10 < 0) {
                    deciwatts = (deciwatts + 5U) / 10U;
                    ++exp10;
                }
                details.d4i_active_power_deciwatts =
                    deciwatts > 65535U ? std::uint16_t{65535U} : static_cast<std::uint16_t>(deciwatts);
            }
            // Active Energy: scale factor at 0x03 (int8), low 32 bits at 0x06..0x09
            const auto e_scale = read_mb_byte(0x03U);
            const auto e0 = read_mb_byte(0x06U);
            const auto e1 = read_mb_byte(0x07U);
            const auto e2 = read_mb_byte(0x08U);
            const auto e3 = read_mb_byte(0x09U);
            if (e_scale && e0 && e1 && e2 && e3) {
                std::uint32_t wh = (static_cast<std::uint32_t>(*e0) << 24U) |
                                   (static_cast<std::uint32_t>(*e1) << 16U) |
                                   (static_cast<std::uint32_t>(*e2) << 8U) | static_cast<std::uint32_t>(*e3);
                int exp10 = decode_scale_factor(*e_scale);
                while (exp10 > 0) {
                    wh *= 10U;
                    --exp10;
                }
                while (exp10 < 0) {
                    wh = (wh + 5U) / 10U;
                    ++exp10;
                }
                details.d4i_active_energy_wh = wh;
            }
        }
    }

    details.probed = true;
    (void)refresh(address);
    result.state = channel_states_[address.value()];
    return result;
}

ControlResult LightingService::configure_gear(dali::ShortAddress address, const GearConfig& cfg) {
    ControlResult result{};
    const auto target = dali::Address::short_address(address);
    const auto write_dtr_cmd = [&](std::uint8_t value, std::uint8_t opcode) -> bool {
        const auto dtr_st = transport_.set_dtr(dali::Dtr::Dtr0, value);
        if (dtr_st != dali::TransportStatus::Ok) {
            result.error = ControlError::TransportFailure;
            result.transport_status = dtr_st;
            return false;
        }
        const auto cmd_st = transport_.send_command(target, opcode, true);
        if (cmd_st != dali::TransportStatus::Ok) {
            result.error = ControlError::TransportFailure;
            result.transport_status = cmd_st;
            return false;
        }
        return true;
    };

    auto& details = gear_details_[address.value()];
    if (cfg.min_level <= dali::ArcLevel::maximum) {
        if (!write_dtr_cmd(cfg.min_level, dali::cmd::set_min_level)) {
            return result;
        }
        details.min_level = cfg.min_level;
    }
    if (cfg.max_level <= dali::ArcLevel::maximum) {
        if (!write_dtr_cmd(cfg.max_level, dali::cmd::set_max_level)) {
            return result;
        }
        details.max_level = cfg.max_level;
    }
    if (cfg.power_on_level <= dali::ArcLevel::maximum) {
        if (!write_dtr_cmd(cfg.power_on_level, dali::cmd::set_power_on_level)) {
            return result;
        }
        details.power_on_level = cfg.power_on_level;
    }
    if (cfg.system_failure_level <= dali::ArcLevel::maximum) {
        if (!write_dtr_cmd(cfg.system_failure_level, dali::cmd::set_system_failure_level)) {
            return result;
        }
        details.system_failure_level = cfg.system_failure_level;
    }
    if (cfg.fade_time <= 15) {
        if (!write_dtr_cmd(cfg.fade_time, dali::cmd::set_fade_time)) {
            return result;
        }
        details.fade_time = cfg.fade_time;
    }
    if (cfg.fade_rate >= 1 && cfg.fade_rate <= 15) {
        if (!write_dtr_cmd(cfg.fade_rate, dali::cmd::set_fade_rate)) {
            return result;
        }
        details.fade_rate = cfg.fade_rate;
    }
    // IEC 62386-207 (DT6 LED module): SELECT DIMMING CURVE (0xE3, send-twice) preceded by DTR0 + ENABLE
    // DEVICE TYPE 6
    if (cfg.dimming_curve <= 1U) {
        if (transport_.set_dtr(dali::Dtr::Dtr0, cfg.dimming_curve) == dali::TransportStatus::Ok &&
            transport_.enable_device_type(6U) == dali::TransportStatus::Ok) {
            (void)transport_.send_command(target, 0xE3U, true);
        }
        details.dimming_curve = cfg.dimming_curve;
    }
    if (cfg.full_power_deciwatts != 0xFFFFU) {
        details.full_power_deciwatts = cfg.full_power_deciwatts;
        details.power_configured = true;
    }
    if (cfg.idle_power_centiwatts != 0xFFFFU) {
        details.idle_power_centiwatts = cfg.idle_power_centiwatts;
        details.power_configured = true;
    }
    if (cfg.use_manual_power_override <= 1U) {
        details.use_manual_power_override = (cfg.use_manual_power_override != 0U);
    }
    if (cfg.switch_only <= 1U) {
        details.switch_only = (cfg.switch_only != 0U);
    }
    if (cfg.device_type_override <= 8U) {
        details.device_type = cfg.device_type_override;
        details.switch_only = (cfg.device_type_override == 7U);
        if (cfg.device_type_override != 8U && cfg.dt8_mask_override == 0xFFU) {
            capabilities_[address.value()] = dali::ColourCapabilities{};
        }
    }
    if (cfg.dt8_mask_override <= 0x0FU) {
        dali::ColourCapabilities cap{};
        cap.tc_capable = (cfg.dt8_mask_override & 0x01U) != 0;
        cap.xy_capable = (cfg.dt8_mask_override & 0x08U) != 0;
        if ((cfg.dt8_mask_override & 0x04U) != 0) {
            cap.rgbwaf_channels = 4;
        } else if ((cfg.dt8_mask_override & 0x02U) != 0) {
            cap.rgbwaf_channels = 3;
        }
        capabilities_[address.value()] = cap;
    }
    if (cfg.groups_mask <= 0xFFFFU) {
        const auto desired = static_cast<std::uint16_t>(cfg.groups_mask);
        std::uint16_t current = details.groups;
        if (const auto grp = transport_.read_groups(address);
            grp.status == dali::TransportStatus::Ok && grp.groups) {
            current = *grp.groups;
        }
        const auto diff = static_cast<std::uint16_t>(current ^ desired);
        for (unsigned g = 0; g < dali::Group::capacity; ++g) {
            if ((diff & (1U << g)) == 0) {
                continue;
            }
            const bool member = (desired & (1U << g)) != 0;
            const auto st = transport_.set_group_membership(address, *dali::Group::from(g), member);
            if (st != dali::TransportStatus::Ok) {
                result.error = ControlError::TransportFailure;
                result.transport_status = st;
                return result;
            }
        }
        details.groups = desired;
        config_.group_masks[address.value()] = desired;
    }
    details.probed = true;
    return result;
}

}
