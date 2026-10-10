#pragma once

#include <bitset>

#include "commissioning/Commissioning.hpp"
#include "control/Command.hpp"
#include "dali/Address.hpp"
#include "dali/ShortAddress.hpp"
#include "lighting/LightingService.hpp"

namespace gateway::control {

// Shared worker-to-KNX fan-out, used by the firmware task and the lab worker.
// `push` is a callable `bool(const StatusEvent&)`; returning false drops the
// event (bounded queues) without aborting the batch.

template <typename Push>
void publish_changes(Push&& push, const lighting::LightingService& lighting,
                     const lighting::ControlResult& result) {
    for (std::size_t channel = 0; channel < dali::ShortAddress::capacity; ++channel) {
        if (result.changed_channels.test(channel)) {
            push(StatusUpdate{dali::Address::short_address(*dali::ShortAddress::from(channel)),
                              lighting.state(*dali::ShortAddress::from(channel))});
        }
    }
    for (std::size_t group = 0; group < dali::Group::capacity; ++group) {
        if (result.changed_groups.test(group)) {
            push(StatusUpdate{dali::Address::group(*dali::Group::from(group)),
                              lighting.state(*dali::Group::from(group))});
        }
    }
}

// Pushes one ColourUpdate per channel/group whose colour state changed and
// has a reportable colour (channels whose colour was never commanded or
// read back have nothing to publish).
template <typename Push>
void publish_colour_changes(Push&& push, const lighting::LightingService& lighting,
                            const lighting::ControlResult& result) {
    for (std::size_t channel = 0; channel < dali::ShortAddress::capacity; ++channel) {
        if (!result.changed_colour_channels.test(channel)) {
            continue;
        }
        const auto address =
            dali::Address::short_address(*dali::ShortAddress::from(channel));
        if (const auto report = lighting.colour_report(address)) {
            push(ColourUpdate{address, *report});
        }
    }
    for (std::size_t group = 0; group < dali::Group::capacity; ++group) {
        if (!result.changed_colour_groups.test(group)) {
            continue;
        }
        const auto address = dali::Address::group(*dali::Group::from(group));
        if (const auto report = lighting.colour_report(address)) {
            push(ColourUpdate{address, *report});
        }
    }
}

template <typename Push>
void publish_fault_changes(Push&& push, const commissioning::Commissioning& commissioning,
                           const std::bitset<dali::ShortAddress::capacity>& changes) {
    for (std::size_t channel = 0; channel < dali::ShortAddress::capacity; ++channel) {
        if (changes.test(channel)) {
            push(FaultUpdate{*dali::ShortAddress::from(channel),
                             commissioning.faulty().test(channel)});
        }
    }
}

template <typename Push>
void publish_job_result(Push&& push, const lighting::LightingService& lighting,
                        const commissioning::Commissioning& commissioning,
                        const commissioning::JobResult& result) {
    publish_changes(push, lighting, result.result);
    publish_colour_changes(push, lighting, result.result);
    publish_fault_changes(push, commissioning, result.fault_changes);
}

}
