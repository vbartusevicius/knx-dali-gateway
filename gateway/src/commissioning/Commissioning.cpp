#include "commissioning/Commissioning.hpp"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "dali/ShortAddress.hpp"
#include "dali/Transport.hpp"
#include "dali/devices/DeviceFrames.hpp"
#include "lighting/LightingService.hpp"

namespace gateway::commissioning {
namespace {

constexpr std::uint8_t lamp_failure_mask = 0x02;

template <typename Bitmap>
std::array<std::uint8_t, 8> encode(const Bitmap& bits) {
    std::array<std::uint8_t, 8> bytes{};
    for (unsigned index = 0; index < dali::ShortAddress::capacity; ++index) {
        if (bits.test(index)) {
            bytes[index / 8] = static_cast<std::uint8_t>(bytes[index / 8] | (1u << (index % 8)));
        }
    }
    return bytes;
}

}

Commissioning::Commissioning(dali::Transport& transport, lighting::LightingService& service) noexcept
    : transport_(transport), service_(service), devices_(transport) {}

void Commissioning::set_listener(SnapshotListener* listener) noexcept {
    listener_ = listener;
}

Snapshot Commissioning::snapshot() const noexcept {
    return {progress_, encode(present_), encode(faulty_)};
}

const std::bitset<dali::ShortAddress::capacity>& Commissioning::present() const noexcept {
    return present_;
}

const std::bitset<dali::ShortAddress::capacity>& Commissioning::faulty() const noexcept {
    return faulty_;
}

void Commissioning::emit(Phase phase, std::uint8_t progress, std::uint8_t found,
                         dali::TransportStatus error) {
    progress_ = {phase, progress, found, error};
    if (listener_) {
        listener_->on_snapshot(snapshot());
    }
}

void Commissioning::mark_fault(unsigned index, bool faulty, JobResult& output) {
    if (faulty_.test(index) != faulty) {
        faulty_.set(index, faulty);
        output.fault_changes.set(index);
    }
}

void Commissioning::merge(JobResult& output, const lighting::ControlResult& result) {
    if (result.error != lighting::ControlError::None && output.result.error == lighting::ControlError::None) {
        output.result.error = result.error;
        output.result.transport_status = result.transport_status;
    }
    output.result.changed_channels |= result.changed_channels;
    output.result.changed_groups |= result.changed_groups;
    output.result.changed_colour_channels |= result.changed_colour_channels;
    output.result.changed_colour_groups |= result.changed_colour_groups;
}

JobResult Commissioning::scan() {
    JobResult output{};
    emit(Phase::Scanning, 0, 0, dali::TransportStatus::Ok);
    auto error = dali::TransportStatus::Ok;
    for (unsigned index = 0; index < dali::ShortAddress::capacity; ++index) {
        const auto address = *dali::ShortAddress::from(index);
        const auto reply = transport_.query_present(address);
        if (reply.status == dali::TransportStatus::Ok || reply.status == dali::TransportStatus::Timeout) {
            const bool was = present_.test(index);
            const bool here = reply.status == dali::TransportStatus::Ok && reply.present;
            present_.set(index, here);
            if (here) {
                mark_fault(index, false, output);
                // Probe DT8 capabilities so colour commands can gate on them;
                // a non-DT8 gear records "no capability" without failing.
                merge(output, service_.refresh_capabilities(address));
            } else if (was) {
                // A gear that answered before and is now silent is faulty.
                mark_fault(index, true, output);
            }
        } else if (error == dali::TransportStatus::Ok) {
            // Bus errors keep the previous presence state: a collision does
            // not tell us whether the gear is there.
            error = reply.status;
        }
        emit(Phase::Scanning,
             static_cast<std::uint8_t>((std::size_t{index + 1} * 100) / dali::ShortAddress::capacity),
             static_cast<std::uint8_t>(present_.count()), error);
    }
    emit(Phase::Done, 100, static_cast<std::uint8_t>(present_.count()), error);
    output.result.transport_status = error;
    output.result.error = error == dali::TransportStatus::Ok ? lighting::ControlError::None
                                                             : lighting::ControlError::TransportFailure;
    return output;
}

JobResult Commissioning::assign(dali::CommissionMode mode) {
    JobResult output{};
    emit(Phase::Addressing, 0, 0, dali::TransportStatus::Ok);
    auto start = dali::ShortAddress::from(0);
    if (mode == dali::CommissionMode::Unaddressed) {
        unsigned free_index = dali::ShortAddress::capacity;
        for (unsigned index = 0; index < dali::ShortAddress::capacity; ++index) {
            if (!present_.test(index)) {
                free_index = index;
                break;
            }
        }
        if (free_index >= dali::ShortAddress::capacity) {
            emit(Phase::Done, 100, 0, dali::TransportStatus::Ok);
            return output;
        }
        start = dali::ShortAddress::from(free_index);
    }
    const auto result = transport_.commission(mode, *start, dali::ShortAddress::capacity);
    if (result.status != dali::TransportStatus::Ok) {
        emit(Phase::Failed, 0, result.count, result.status);
        output.result = {lighting::ControlError::TransportFailure, result.status, {}, {}, {}};
        return output;
    }
    emit(Phase::Addressing, 50, static_cast<std::uint8_t>(result.count), dali::TransportStatus::Ok);
    return scan();
}

JobResult Commissioning::replace(dali::ShortAddress target) {
    JobResult output{};
    if (present_.test(target.value())) {
        emit(Phase::Failed, 0, 0, dali::TransportStatus::Failure);
        output.result = {
            lighting::ControlError::TransportFailure, dali::TransportStatus::Failure, {}, {}, {}};
        return output;
    }
    emit(Phase::Replacing, 0, 0, dali::TransportStatus::Ok);
    const auto result = transport_.commission(dali::CommissionMode::Unaddressed, target, 1);
    if (result.status != dali::TransportStatus::Ok) {
        emit(Phase::Failed, 0, result.count, result.status);
        output.result = {lighting::ControlError::TransportFailure, result.status, {}, {}, {}};
        return output;
    }
    if (result.count != 1) {
        emit(Phase::Failed, 0, static_cast<std::uint8_t>(result.count), dali::TransportStatus::Failure);
        output.result = {
            lighting::ControlError::TransportFailure, dali::TransportStatus::Failure, {}, {}, {}};
        return output;
    }
    present_.set(target.value());
    mark_fault(target.value(), false, output);
    emit(Phase::Replacing, 50, 1, dali::TransportStatus::Ok);
    merge(output, service_.refresh_capabilities(target));
    merge(output, service_.reconcile_groups(target));
    merge(output, service_.recover(target));
    const auto error = output.result.error;
    emit(error == lighting::ControlError::None ? Phase::Done : Phase::Failed, 100, 1,
         output.result.transport_status);
    return output;
}

JobResult Commissioning::identify(dali::ShortAddress address) {
    JobResult output{};
    emit(Phase::Identifying, 0, 0, dali::TransportStatus::Ok);
    const auto status = transport_.identify(address);
    if (status == dali::TransportStatus::Ok) {
        emit(Phase::Done, 100, 0, dali::TransportStatus::Ok);
    } else {
        emit(Phase::Failed, 0, 0, status);
        output.result = {lighting::ControlError::TransportFailure, status, {}, {}, {}};
    }
    return output;
}

JobResult Commissioning::reset_addresses() {
    emit(Phase::Addressing, 0, 0, dali::TransportStatus::Ok);
    const auto status = transport_.clear_short_addresses();
    if (status != dali::TransportStatus::Ok) {
        emit(Phase::Failed, 0, 0, status);
        JobResult output{};
        output.result = {lighting::ControlError::TransportFailure, status, {}, {}, {}};
        return output;
    }
    return scan();
}

JobResult Commissioning::startup(InstanceBound bound, void* context) {
    auto output = scan();
    for (unsigned index = 0; index < dali::ShortAddress::capacity; ++index) {
        if (present_.test(index)) {
            merge(output, service_.recover(*dali::ShortAddress::from(index)));
        }
    }
    if (bound != nullptr) {
        const auto inputs = scan_inputs(bound, context);
        merge(output, inputs.result);
    }
    return output;
}

InputSnapshot Commissioning::input_snapshot() const noexcept {
    InputSnapshot snapshot{};
    for (unsigned index = 0; index < dali::ShortAddress::capacity; ++index) {
        if (!devices_.present().test(index)) {
            continue;
        }
        snapshot.present[index / 8] =
            static_cast<std::uint8_t>(snapshot.present[index / 8] | (1u << (index % 8)));
        auto byte = static_cast<std::uint8_t>(
            devices_.instance_count(static_cast<std::uint8_t>(index)).value_or(0) & 0x1F);
        if (devices_.has_type(static_cast<std::uint8_t>(index), dali::device::type::push_button)) {
            byte = static_cast<std::uint8_t>(byte | 0x20);
        }
        if (devices_.has_type(static_cast<std::uint8_t>(index), dali::device::type::occupancy)) {
            byte = static_cast<std::uint8_t>(byte | 0x40);
        }
        if (devices_.has_type(static_cast<std::uint8_t>(index), dali::device::type::light)) {
            byte = static_cast<std::uint8_t>(byte | 0x80);
        }
        snapshot.summary[index] = byte;
    }
    return snapshot;
}

JobResult Commissioning::scan_inputs(InstanceBound bound, void* context) {
    JobResult output{};
    emit(Phase::Scanning, 0, 0, dali::TransportStatus::Ok);
    auto error = devices_.scan();
    emit(Phase::Scanning, 50, static_cast<std::uint8_t>(devices_.present().count()), error);
    const auto applied = devices_.apply(bound, context);
    if (error == dali::TransportStatus::Ok) {
        error = applied;
    }
    emit(Phase::Done, 100, static_cast<std::uint8_t>(devices_.present().count()), error);
    output.result.transport_status = error;
    output.result.error = error == dali::TransportStatus::Ok ? lighting::ControlError::None
                                                             : lighting::ControlError::TransportFailure;
    return output;
}

JobResult Commissioning::assign_inputs(dali::CommissionMode mode, InstanceBound bound, void* context) {
    JobResult output{};
    emit(Phase::Addressing, 0, 0, dali::TransportStatus::Ok);
    auto start = dali::ShortAddress::from(0);
    if (mode == dali::CommissionMode::Unaddressed) {
        const auto scan_error = devices_.scan();
        if (scan_error != dali::TransportStatus::Ok) {
            emit(Phase::Failed, 0, 0, scan_error);
            output.result = {lighting::ControlError::TransportFailure, scan_error, {}, {}, {}};
            return output;
        }
        unsigned free_index = dali::ShortAddress::capacity;
        for (unsigned index = 0; index < dali::ShortAddress::capacity; ++index) {
            if (!devices_.present().test(index)) {
                free_index = index;
                break;
            }
        }
        if (free_index >= dali::ShortAddress::capacity) {
            emit(Phase::Done, 100, 0, dali::TransportStatus::Ok);
            return output;
        }
        start = dali::ShortAddress::from(free_index);
    }
    const auto result = transport_.commission_devices(mode, *start, dali::ShortAddress::capacity);
    if (result.status != dali::TransportStatus::Ok) {
        emit(Phase::Failed, 0, result.count, result.status);
        output.result = {lighting::ControlError::TransportFailure, result.status, {}, {}, {}};
        return output;
    }
    emit(Phase::Addressing, 50, static_cast<std::uint8_t>(result.count), dali::TransportStatus::Ok);
    return scan_inputs(bound, context);
}

std::optional<dali::ShortAddress> Commissioning::poll_next() {
    if (present_.none()) {
        return std::nullopt;
    }
    for (unsigned offset = 0; offset < dali::ShortAddress::capacity; ++offset) {
        const unsigned index = (poll_index_ + offset) % dali::ShortAddress::capacity;
        if (!present_.test(index)) {
            continue;
        }
        poll_index_ = index + 1;
        const auto address = *dali::ShortAddress::from(index);
        const auto reply = transport_.query_status(address);
        if (reply.status == dali::TransportStatus::Ok && reply.gear_status) {
            service_.record_status_byte(address, *reply.gear_status);
        }
        const bool faulty = reply.status != dali::TransportStatus::Ok ||
                            (reply.gear_status && (*reply.gear_status & lamp_failure_mask) != 0);
        if (faulty_.test(index) != faulty) {
            faulty_.set(index, faulty);
            return address;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

}
