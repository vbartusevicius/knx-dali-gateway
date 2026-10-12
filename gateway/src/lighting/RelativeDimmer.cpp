#include "lighting/RelativeDimmer.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

#include "dali/Address.hpp"

namespace gateway::lighting {

void RelativeDimmer::start(dali::Address address, DimDirection direction, std::uint32_t now_ms) noexcept {
    auto& slot = slots_[slot_index(address)];
    slot.running = true;
    slot.first_step = true;
    slot.direction = direction;
    slot.started_at = now_ms;
    slot.next_step_at = now_ms;
}

bool RelativeDimmer::stop(dali::Address address) noexcept {
    auto& slot = slots_[slot_index(address)];
    const bool was_running = slot.running;
    slot.running = false;
    return was_running;
}

bool RelativeDimmer::running(dali::Address address) const noexcept {
    return slots_[slot_index(address)].running;
}

std::optional<std::uint32_t> RelativeDimmer::next_deadline() const noexcept {
    std::optional<std::uint32_t> earliest;
    for (const auto& slot : slots_) {
        if (!slot.running) {
            continue;
        }
        const std::uint32_t expires_at = slot.started_at + safety_limit_ms;
        const std::uint32_t deadline =
            reached(expires_at, slot.next_step_at) ? slot.next_step_at : expires_at;
        if (!earliest || reached(*earliest, deadline)) {
            earliest = deadline;
        }
    }
    return earliest;
}

std::size_t RelativeDimmer::slot_index(dali::Address address) noexcept {
    if (const auto short_address = address.as_short()) {
        return short_address->value();
    }
    if (const auto group = address.as_group()) {
        return dali::ShortAddress::capacity + group->value();
    }
    return dali::ShortAddress::capacity + dali::Group::capacity;
}

dali::Address RelativeDimmer::address_at(std::size_t index) noexcept {
    if (index < dali::ShortAddress::capacity) {
        return dali::Address::short_address(*dali::ShortAddress::from(index));
    }
    if (index < dali::ShortAddress::capacity + dali::Group::capacity) {
        return dali::Address::group(*dali::Group::from(index - dali::ShortAddress::capacity));
    }
    return dali::Address::broadcast();
}

}
