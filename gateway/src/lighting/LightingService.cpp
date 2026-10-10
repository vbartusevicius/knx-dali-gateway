#include "lighting/LightingService.hpp"

namespace gateway::lighting {
namespace {

storage::Slot slot(storage::SlotKind kind, dali::Address address) noexcept {
    return {kind, address};
}

}

LightingService::LightingService(dali::Transport& transport, storage::LevelStore& store) noexcept
    : transport_(transport), store_(store) {}

void LightingService::configure(const config::Configuration& configuration) noexcept {
    config_ = configuration;
}

ControlResult LightingService::set_level(dali::Address address, dali::ArcLevel target) {
    return apply_level(address, target);
}

ControlResult LightingService::switch_on(dali::Address address) {
    const auto& policy = switch_on_policy(address);
    if (policy.policy == config::SwitchOnPolicy::FixedLevel) {
        return set_level(address, policy.fixed);
    }
    const auto remembered = last_active(address);
    const auto target = remembered && remembered->value() > 0 ? *remembered : *dali::ArcLevel::from(dali::ArcLevel::maximum);
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
        return result;
    }

    const auto saved = store_.load(slot(storage::SlotKind::Target, dali::Address::short_address(address)));
    if (saved.status == storage::ReadStatus::NotFound) {
        return {ControlError::NoSavedTarget, result.transport_status, channel_states_[address.value()], {}, {}};
    }
    if (saved.status != storage::ReadStatus::Ok || !saved.level) {
        return {ControlError::StorageFailure, result.transport_status, channel_states_[address.value()], {}, {}};
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
        return {ControlError::TransportFailure,
                read.status == dali::TransportStatus::Ok ? dali::TransportStatus::InvalidResponse : read.status,
                channel_states_[address.value()], {}, {}};
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
    return {failed == dali::TransportStatus::Ok ? ControlError::None : ControlError::TransportFailure,
            failed, channel_states_[address.value()], {}, {}};
}

ControlResult LightingService::apply_level(dali::Address address, dali::ArcLevel target) {
    const auto status = transport_.set_level(address, target);
    ControlResult result{ControlError::None, status, {}, {}, {}};

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

    if (status != dali::TransportStatus::Ok) {
        result.error = ControlError::TransportFailure;
        result.state = address.as_short() ? channel_states_[address.as_short()->value()]
                       : address.as_group() ? group_states_[address.as_group()->value()]
                       : State{};
        return result;
    }

    for (std::size_t channel = 0; channel < channel_states_.size(); ++channel) {
        if (affected.test(channel)) {
            channel_states_[channel] = {target, StateSource::Commanded};
        }
    }
    if (const auto group = address.as_group()) {
        group_states_[group->value()] = {target, StateSource::Commanded};
        result.changed_groups.set(group->value());
    }
    if (address.kind() == dali::AddressKind::Broadcast) {
        for (auto& group_state : group_states_) {
            group_state = {target, StateSource::Commanded};
        }
        result.changed_groups.set();
    }
    result.changed_channels = affected;

    persist(address, target, result);
    remember_if_active(address, target);

    result.state = address.as_short() ? channel_states_[address.as_short()->value()]
                   : address.as_group() ? group_states_[address.as_group()->value()]
                   : State{target, StateSource::Commanded};
    return result;
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
    auto& entry = address.as_short() ? channel_last_active_[address.as_short()->value()]
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
    auto& entry = address.as_short() ? channel_last_active_[address.as_short()->value()]
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

}
