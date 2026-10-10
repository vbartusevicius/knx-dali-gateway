#pragma once

#include <array>
#include <bitset>
#include <cstdint>
#include <optional>
#include <type_traits>

#include "dali/ShortAddress.hpp"
#include "dali/Transport.hpp"
#include "dali/devices/DeviceInventory.hpp"
#include "lighting/LightingService.hpp"

namespace gateway::commissioning {

enum class Phase : std::uint8_t {
    Idle,
    Scanning,
    Addressing,
    Replacing,
    Identifying,
    Done,
    Failed
};

// True while a serialized commissioning job runs; ETS commands during that
// window are answered "busy".
constexpr bool active(Phase phase) noexcept {
    return phase == Phase::Scanning || phase == Phase::Addressing || phase == Phase::Replacing ||
           phase == Phase::Identifying;
}

struct Progress {
    Phase phase = Phase::Idle;
    std::uint8_t progress = 0;  // 0..100
    std::uint8_t found = 0;
    dali::TransportStatus last_error = dali::TransportStatus::Ok;
};

// Snapshot published to the KNX task; bitmaps are LSB-first (bit 0 of byte 0
// = short address 0) matching the wire encoding in CommissioningProtocol.
struct Snapshot {
    Progress progress;
    std::array<std::uint8_t, 8> present{};
    std::array<std::uint8_t, 8> faulty{};
};

static_assert(std::is_trivially_copyable_v<Snapshot>, "Snapshot travels through FreeRTOS queues");

// Function-property state 2: input-device presence bitmap (LSB = device
// address 0) plus one summary byte per device — only meaningful when the
// presence bit is set: bits 0-4 instance count, bit 5 has push button,
// bit 6 has occupancy, bit 7 has light sensor.
struct InputSnapshot {
    std::array<std::uint8_t, 8> present{};
    std::array<std::uint8_t, 64> summary{};
};

static_assert(std::is_trivially_copyable_v<InputSnapshot>,
              "InputSnapshot travels through FreeRTOS queues");

// Reports whether an ETS input channel binds (device, instance); the
// callback and context come from the worker's InputMapper.
using InstanceBound = bool (*)(std::uint8_t device, std::uint8_t instance, void* context);

struct JobResult {
    lighting::ControlResult result;
    // Channels whose gear-fault flag flipped during the operation.
    std::bitset<dali::ShortAddress::capacity> fault_changes;
};

// Receives the published snapshot (on the worker) whenever it changes.
class SnapshotListener {
public:
    virtual ~SnapshotListener() = default;
    virtual void on_snapshot(const Snapshot& snapshot) = 0;
};

// Serialized DALI commissioning and gear health tracking: presence scan,
// address assignment, device replacement, identify, and round-robin fault
// polling. Runs on the DALI worker; pure C++, hardware independent.
class Commissioning final {
public:
    Commissioning(dali::Transport& transport, lighting::LightingService& service) noexcept;
    Commissioning(const Commissioning&) = delete;
    Commissioning& operator=(const Commissioning&) = delete;

    void set_listener(SnapshotListener* listener) noexcept;
    Snapshot snapshot() const noexcept;
    const std::bitset<dali::ShortAddress::capacity>& present() const noexcept;
    const std::bitset<dali::ShortAddress::capacity>& faulty() const noexcept;

    // QUERY CONTROL GEAR on all 64 addresses; Timeout means absent. A gear
    // that vanished since the last scan becomes faulty, an answering gear
    // loses its fault flag and gets its DT8 colour capabilities refreshed.
    JobResult scan();
    // Part 102 address assignment, then a presence scan. Unaddressed starts
    // at the first free short address; All re-addresses starting at 0.
    JobResult assign(dali::CommissionMode mode);
    // Address one unaddressed gear at the (currently absent) target, then
    // reconcile its groups and recover its level.
    JobResult replace(dali::ShortAddress target);
    JobResult identify(dali::ShortAddress address);
    // Broadcasts DTR0 = 0xFF + SET SHORT ADDRESS, wiping all short addresses;
    // the trailing scan then finds no addressed gear.
    JobResult reset_addresses();
    // Boot sequence: scan, recover every present channel readback-first,
    // then (bound != nullptr) inventory the input devices and reapply the
    // configured instance scheme/filters.
    JobResult startup(InstanceBound bound = nullptr, void* context = nullptr);
    // IEC 62386-103 inventory of all 64 device short addresses, then the
    // instance configuration pass: device/instance event scheme, default
    // per-type filters, enable on bound instances, disable elsewhere.
    JobResult scan_inputs(InstanceBound bound, void* context);
    // Part 103 address assignment, then scan_inputs. Unaddressed starts at
    // the first device short address without an inventoried device.
    JobResult assign_inputs(dali::CommissionMode mode, InstanceBound bound, void* context);
    const dali::DeviceInventory& devices() const noexcept { return devices_; }
    InputSnapshot input_snapshot() const noexcept;
    // Checks one previously-present channel (round-robin): a missing answer
    // or a set lamp-failure bit marks it faulty. Returns the channel whose
    // fault flag changed, if any.
    std::optional<dali::ShortAddress> poll_next();

private:
    void emit(Phase phase, std::uint8_t progress, std::uint8_t found, dali::TransportStatus error);
    void mark_fault(unsigned index, bool faulty, JobResult& output);
    static void merge(JobResult& output, const lighting::ControlResult& result);

    dali::Transport& transport_;
    lighting::LightingService& service_;
    dali::DeviceInventory devices_;
    std::bitset<dali::ShortAddress::capacity> present_{};
    std::bitset<dali::ShortAddress::capacity> faulty_{};
    Progress progress_{};
    unsigned poll_index_ = 0;
    SnapshotListener* listener_ = nullptr;
};

}
