#include <vector>

#include <unity.h>

#include "Fakes.hpp"
#include "commissioning/Commissioning.hpp"
#include "control/Command.hpp"
#include "dali/devices/DeviceFrames.hpp"
#include "inputs/InputMapper.hpp"
#include "lighting/LightingService.hpp"
#include "lighting/RelativeDimmer.hpp"

namespace {

using gateway::commissioning::Commissioning;
using gateway::commissioning::Phase;
using gateway::dali::CommissionMode;
using gateway::dali::TransportStatus;
using gateway::lighting::ControlError;
using gateway::lighting::LightingService;
using gateway::testing::FakeDaliTransport;
using gateway::testing::MemoryLevelStore;

gateway::dali::ShortAddress channel(unsigned value) {
    return *gateway::dali::ShortAddress::from(value);
}

gateway::inputs::InputChannel input_channel(unsigned value) {
    return *gateway::inputs::InputChannel::from(value);
}

struct Fixture {
    FakeDaliTransport bus;
    MemoryLevelStore store;
    LightingService service{bus, store};
    Commissioning commissioning{bus, service};
};

// Script a bus where `present` addresses answer QUERY CONTROL GEAR.
void script_bus(FakeDaliTransport& bus, const std::vector<unsigned>& present) {
    bus.presence_replies.clear();
    bus.default_presence = {TransportStatus::Timeout, false};
    for (const auto address : present) {
        bus.presence_replies[address] = {TransportStatus::Ok, true};
    }
}

void scan(Fixture& f) {
    const auto result = f.commissioning.scan();
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.result.error));
}

bool present(const Commissioning& c, unsigned address) {
    return c.present().test(address);
}

bool faulty(const Commissioning& c, unsigned address) {
    return c.faulty().test(address);
}

gateway::commissioning::Progress progress(const Commissioning& c) {
    return c.snapshot().progress;
}

void test_scan_probes_every_address_and_records_presence() {
    Fixture f;
    script_bus(f.bus, {0, 3, 63});
    scan(f);
    TEST_ASSERT_EQUAL_UINT32(64, f.bus.presence_queries.size());
    TEST_ASSERT_EQUAL_UINT8(0, f.bus.presence_queries.front().value());
    TEST_ASSERT_EQUAL_UINT8(63, f.bus.presence_queries.back().value());
    TEST_ASSERT_TRUE(present(f.commissioning, 0));
    TEST_ASSERT_TRUE(present(f.commissioning, 3));
    TEST_ASSERT_TRUE(present(f.commissioning, 63));
    TEST_ASSERT_FALSE(present(f.commissioning, 1));
    const auto& snap = progress(f.commissioning);
    TEST_ASSERT_EQUAL(static_cast<int>(Phase::Done), static_cast<int>(snap.phase));
    TEST_ASSERT_EQUAL_UINT8(100, snap.progress);
    TEST_ASSERT_EQUAL_UINT8(3, snap.found);
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Ok), static_cast<int>(snap.last_error));
}

void test_scan_bus_error_keeps_previous_state_and_reports_error() {
    Fixture f;
    script_bus(f.bus, {5});
    scan(f);
    TEST_ASSERT_TRUE(present(f.commissioning, 5));

    f.bus.presence_replies.clear();
    f.bus.default_presence = {TransportStatus::Failure, false};
    const auto result = f.commissioning.scan();
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::TransportFailure),
                      static_cast<int>(result.result.error));
    // The scan itself completed; the bus error rides in last_error.
    TEST_ASSERT_EQUAL(static_cast<int>(Phase::Done),
                      static_cast<int>(progress(f.commissioning).phase));
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Failure),
                      static_cast<int>(progress(f.commissioning).last_error));
    TEST_ASSERT_TRUE(present(f.commissioning, 5));  // old bitmap untouched
}

void test_scan_flags_a_vanished_gear_faulty() {
    Fixture f;
    script_bus(f.bus, {5});
    scan(f);
    script_bus(f.bus, {});
    const auto result = f.commissioning.scan();
    TEST_ASSERT_FALSE(present(f.commissioning, 5));
    TEST_ASSERT_TRUE(faulty(f.commissioning, 5));
    TEST_ASSERT_TRUE(result.fault_changes.test(5));
}

void test_assign_all_recommissions_everything_and_rescans() {
    Fixture f;
    script_bus(f.bus, {1});
    f.bus.commission_replies.push_back({TransportStatus::Ok, 8});
    const auto result = f.commissioning.assign(CommissionMode::All);
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.result.error));
    TEST_ASSERT_EQUAL_UINT32(1, f.bus.commission_calls.size());
    const auto& call = f.bus.commission_calls.front();
    TEST_ASSERT_EQUAL(static_cast<int>(CommissionMode::All), static_cast<int>(call.mode));
    TEST_ASSERT_EQUAL_UINT8(0, call.start.value());
    TEST_ASSERT_EQUAL_UINT32(64, call.max_devices);
    TEST_ASSERT_EQUAL_UINT32(64, f.bus.presence_queries.size());  // scanned afterwards
    TEST_ASSERT_EQUAL_UINT8(1, progress(f.commissioning).found);  // one gear answers the scan
}

void test_assign_unaddressed_starts_at_first_free_address() {
    Fixture f;
    script_bus(f.bus, {0, 1});
    scan(f);
    f.bus.presence_queries.clear();
    script_bus(f.bus, {0, 1});
    f.bus.commission_replies.push_back({TransportStatus::Ok, 1});
    const auto result = f.commissioning.assign(CommissionMode::Unaddressed);
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.result.error));
    const auto& call = f.bus.commission_calls.front();
    TEST_ASSERT_EQUAL(static_cast<int>(CommissionMode::Unaddressed), static_cast<int>(call.mode));
    TEST_ASSERT_EQUAL_UINT8(2, call.start.value());
    TEST_ASSERT_EQUAL_UINT32(64, call.max_devices);
}

void test_assign_failure_marks_failed() {
    Fixture f;
    script_bus(f.bus, {});
    f.bus.commission_replies.push_back({TransportStatus::Failure, 0});
    const auto result = f.commissioning.assign(CommissionMode::All);
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::TransportFailure),
                      static_cast<int>(result.result.error));
    TEST_ASSERT_EQUAL(static_cast<int>(Phase::Failed),
                      static_cast<int>(progress(f.commissioning).phase));
}

void test_replace_rejects_a_still_present_target() {
    Fixture f;
    script_bus(f.bus, {7});
    scan(f);
    const auto result = f.commissioning.replace(channel(7));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::TransportFailure),
                      static_cast<int>(result.result.error));
    TEST_ASSERT_EQUAL(static_cast<int>(Phase::Failed),
                      static_cast<int>(progress(f.commissioning).phase));
    TEST_ASSERT_EQUAL_UINT32(0, f.bus.commission_calls.size());
}

void test_replace_fails_on_zero_or_two_found() {
    for (const unsigned count : {0u, 2u}) {
        Fixture f;
        script_bus(f.bus, {});
        f.bus.commission_replies.push_back({TransportStatus::Ok, count});
        const auto result = f.commissioning.replace(channel(9));
        TEST_ASSERT_EQUAL(static_cast<int>(ControlError::TransportFailure),
                          static_cast<int>(result.result.error));
        TEST_ASSERT_EQUAL(static_cast<int>(Phase::Failed),
                          static_cast<int>(progress(f.commissioning).phase));
    }
}

void test_replace_success_reconciles_and_recovers_the_target() {
    Fixture f;
    script_bus(f.bus, {});
    f.bus.commission_replies.push_back({TransportStatus::Ok, 1});
    f.bus.next_groups = {TransportStatus::Ok, std::uint16_t{0}};
    f.bus.next_read = {TransportStatus::Ok, *gateway::dali::ArcLevel::from(120)};
    const auto result = f.commissioning.replace(channel(4));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.result.error));
    const auto& call = f.bus.commission_calls.front();
    TEST_ASSERT_EQUAL(static_cast<int>(CommissionMode::Unaddressed), static_cast<int>(call.mode));
    TEST_ASSERT_EQUAL_UINT8(4, call.start.value());
    TEST_ASSERT_EQUAL_UINT32(1, call.max_devices);
    TEST_ASSERT_EQUAL_UINT32(1, f.bus.group_reads.size());  // reconcile_groups
    TEST_ASSERT_EQUAL_UINT32(1, f.bus.reads.size());        // recover readback
    TEST_ASSERT_TRUE(present(f.commissioning, 4));
    TEST_ASSERT_EQUAL(static_cast<int>(Phase::Done), static_cast<int>(progress(f.commissioning).phase));
}

void test_replace_commission_transport_failure() {
    Fixture f;
    script_bus(f.bus, {});
    f.bus.commission_replies.push_back({TransportStatus::Timeout, 0});
    const auto result = f.commissioning.replace(channel(0));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::TransportFailure),
                      static_cast<int>(result.result.error));
    TEST_ASSERT_EQUAL(static_cast<int>(Phase::Failed),
                      static_cast<int>(progress(f.commissioning).phase));
}

void test_identify_invokes_the_transport() {
    Fixture f;
    script_bus(f.bus, {2});
    scan(f);
    const auto result = f.commissioning.identify(channel(2));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.result.error));
    TEST_ASSERT_EQUAL_UINT32(1, f.bus.identifies.size());
    TEST_ASSERT_EQUAL_UINT8(2, f.bus.identifies.front().value());
    TEST_ASSERT_EQUAL(static_cast<int>(Phase::Done), static_cast<int>(progress(f.commissioning).phase));
}

void test_poll_next_does_nothing_when_nothing_present() {
    Fixture f;
    TEST_ASSERT_FALSE(f.commissioning.poll_next().has_value());
    TEST_ASSERT_EQUAL_UINT32(0, f.bus.status_queries.size());
}

void test_poll_next_is_round_robin_over_present_addresses() {
    Fixture f;
    script_bus(f.bus, {5, 7});
    scan(f);
    // poll 5 → healthy, 7 → lamp failure, 5 → lamp failure.
    f.bus.status_replies = {{TransportStatus::Ok, std::uint8_t{0x04}},
                            {TransportStatus::Ok, std::uint8_t{0x02}},
                            {TransportStatus::Ok, std::uint8_t{0x02}}};

    TEST_ASSERT_FALSE(f.commissioning.poll_next().has_value());  // 5 healthy: no flag change
    TEST_ASSERT_EQUAL_UINT8(5, f.bus.status_queries[0].value());
    const auto flagged = f.commissioning.poll_next();            // 7 lamp failure
    TEST_ASSERT_TRUE(flagged.has_value());
    TEST_ASSERT_EQUAL_UINT8(7, flagged->value());
    TEST_ASSERT_TRUE(faulty(f.commissioning, 7));
    TEST_ASSERT_EQUAL_UINT8(7, f.bus.status_queries[1].value());
    const auto third = f.commissioning.poll_next();              // wraps to 5
    TEST_ASSERT_TRUE(third.has_value());
    TEST_ASSERT_EQUAL_UINT8(5, f.bus.status_queries[2].value());
    TEST_ASSERT_TRUE(faulty(f.commissioning, 5));
}

void test_poll_next_marks_missing_gear_faulty() {
    Fixture f;
    script_bus(f.bus, {0});
    scan(f);
    f.bus.status_replies.clear();
    f.bus.status_replies.push_back({TransportStatus::Timeout, std::nullopt});
    const auto change = f.commissioning.poll_next();
    TEST_ASSERT_TRUE(change.has_value());
    TEST_ASSERT_EQUAL_UINT8(0, change->value());
    TEST_ASSERT_TRUE(faulty(f.commissioning, 0));
}

void test_poll_next_clears_a_recovered_fault() {
    Fixture f;
    script_bus(f.bus, {0});
    scan(f);
    f.bus.status_replies = {{TransportStatus::Ok, std::uint8_t{0x02}}};
    const auto flagged = f.commissioning.poll_next();
    TEST_ASSERT_TRUE(flagged.has_value());
    TEST_ASSERT_TRUE(faulty(f.commissioning, 0));

    f.bus.status_replies = {{TransportStatus::Ok, std::uint8_t{0x04}}};
    const auto cleared = f.commissioning.poll_next();
    TEST_ASSERT_TRUE(cleared.has_value());
    TEST_ASSERT_EQUAL_UINT8(0, cleared->value());
    TEST_ASSERT_FALSE(faulty(f.commissioning, 0));
}

void test_job_progress_and_found_count_in_snapshot() {
    Fixture f;
    script_bus(f.bus, {0, 1});
    scan(f);
    TEST_ASSERT_EQUAL_UINT8(2, progress(f.commissioning).found);

    f.bus.commission_replies.push_back({TransportStatus::Ok, 5});
    script_bus(f.bus, {0, 1, 2, 3, 4});
    (void)f.commissioning.assign(CommissionMode::All);
    const auto& snap = progress(f.commissioning);
    TEST_ASSERT_EQUAL_UINT8(5, snap.found);
    TEST_ASSERT_EQUAL(static_cast<int>(Phase::Done), static_cast<int>(snap.phase));
}

void test_startup_scans_then_recovers_every_present_address() {
    Fixture f;
    script_bus(f.bus, {0, 2});
    f.bus.next_read = {TransportStatus::Ok, *gateway::dali::ArcLevel::from(50)};
    const auto result = f.commissioning.startup();
    TEST_ASSERT_EQUAL_UINT32(64, f.bus.presence_queries.size());
    TEST_ASSERT_EQUAL_UINT32(2, f.bus.reads.size());
    TEST_ASSERT_EQUAL_UINT32(2, result.result.changed_channels.count());
    TEST_ASSERT_TRUE(result.result.changed_channels.test(0));
    TEST_ASSERT_TRUE(result.result.changed_channels.test(2));
}

void test_reset_addresses_clears_then_rescans_empty() {
    Fixture f;
    script_bus(f.bus, {1, 2});
    scan(f);
    f.bus.presence_queries.clear();

    script_bus(f.bus, {});  // after the reset nothing answers a short address
    const auto result = f.commissioning.reset_addresses();
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.result.error));
    TEST_ASSERT_EQUAL_UINT32(1, f.bus.clear_calls);
    TEST_ASSERT_EQUAL_UINT32(64, f.bus.presence_queries.size());  // rescan afterwards
    TEST_ASSERT_EQUAL_UINT32(0, f.commissioning.present().count());
    // Gear that answered before and is now unaddressed counts as faulty until recommissioned.
    TEST_ASSERT_TRUE(faulty(f.commissioning, 1));
    TEST_ASSERT_TRUE(faulty(f.commissioning, 2));
}

void test_reset_addresses_reports_clear_failure_without_scanning() {
    Fixture f;
    f.bus.clear_status = TransportStatus::Failure;
    const auto result = f.commissioning.reset_addresses();
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::TransportFailure),
                      static_cast<int>(result.result.error));
    TEST_ASSERT_EQUAL(static_cast<int>(Phase::Failed),
                      static_cast<int>(progress(f.commissioning).phase));
    TEST_ASSERT_EQUAL_UINT32(0, f.bus.presence_queries.size());
}

void test_active_phases_gate_new_jobs() {
    TEST_ASSERT_FALSE(gateway::commissioning::active(Phase::Idle));
    TEST_ASSERT_TRUE(gateway::commissioning::active(Phase::Scanning));
    TEST_ASSERT_TRUE(gateway::commissioning::active(Phase::Addressing));
    TEST_ASSERT_TRUE(gateway::commissioning::active(Phase::Replacing));
    TEST_ASSERT_TRUE(gateway::commissioning::active(Phase::Identifying));
    TEST_ASSERT_FALSE(gateway::commissioning::active(Phase::Done));
    TEST_ASSERT_FALSE(gateway::commissioning::active(Phase::Failed));
}

// --- input devices (IEC 62386-103) ------------------------------------------

namespace device = gateway::dali::device;

// Part 103 bus: units carry a random address for INITIALISE/COMPARE-based
// commissioning (the transport's default commission_devices() runs
// DeviceAddressing over these frames), a programmed short address, and per
// instance a type, resolution and enable flag.
class InputBus final : public FakeDaliTransport {
public:
    struct Unit {
        std::uint32_t random;
        std::uint32_t search = 0;
        int short_address = -1;
        bool initialising = false;
        bool withdrawn = false;
        std::vector<std::uint8_t> instance_types;
        std::vector<std::uint8_t> resolutions;
        std::vector<bool> enabled;

        static Unit of(std::uint32_t random, std::vector<std::uint8_t> types,
                       std::vector<std::uint8_t> resolutions = {}) {
            Unit unit;
            unit.random = random;
            unit.instance_types = types;
            unit.resolutions = resolutions.empty()
                                   ? std::vector<std::uint8_t>(types.size(), 0)
                                   : resolutions;
            unit.enabled.assign(types.size(), false);
            return unit;
        }
    };

    std::vector<Unit> units;
    unsigned quiescent_starts = 0;
    unsigned quiescent_stops = 0;

    Unit* unit_at(int address) {
        for (auto& unit : units) {
            if (unit.short_address == address) {
                return &unit;
            }
        }
        return nullptr;
    }

    TransportStatus send_device_frame(std::uint32_t frame, bool) override {
        const auto address = static_cast<std::uint8_t>(frame >> 16);
        const auto operation = static_cast<std::uint8_t>(frame >> 8);
        const auto operand = static_cast<std::uint8_t>(frame);
        if (address == 0xC1) {
            switch (operation) {
            case 0x00:
                for (auto& unit : units) {
                    unit.initialising = false;
                }
                break;
            case 0x01:  // INITIALISE
                for (auto& unit : units) {
                    if (operand == 0x00 ||
                        (operand == 0xFF && unit.short_address < 0) ||
                        operand == device::special::encode_short_address(unit.short_address)) {
                        unit.initialising = true;
                        unit.withdrawn = false;
                    }
                }
                break;
            case 0x02:
                break;  // RANDOMISE: random values are preset
            case 0x04:  // WITHDRAW
                for (auto& unit : units) {
                    if (unit.initialising && unit.random == unit.search) {
                        unit.withdrawn = true;
                    }
                }
                break;
            case 0x05:
            case 0x06:
            case 0x07: {  // SEARCHADDRH/M/L
                const int shift = operation == 0x05 ? 16 : operation == 0x06 ? 8 : 0;
                for (auto& unit : units) {
                    unit.search = (unit.search & ~(0xFFu << shift)) |
                                  (static_cast<std::uint32_t>(operand) << shift);
                }
                break;
            }
            case 0x08:  // PROGRAM SHORT ADDRESS
                for (auto& unit : units) {
                    if (unit.initialising && unit.random == unit.search) {
                        unit.short_address = operand >> 1;
                    }
                }
                break;
            default:
                break;
            }
            return TransportStatus::Ok;
        }
        if (address == device::address_broadcast && operation == device::instance_device) {
            if (operand == device::cmd::start_quiescent_mode) {
                ++quiescent_starts;
            } else if (operand == device::cmd::stop_quiescent_mode) {
                ++quiescent_stops;
            }
            return TransportStatus::Ok;
        }
        if ((address & 0x81) == 0x01) {
            auto* unit = unit_at(address >> 1);
            if (unit && operation < unit->enabled.size()) {
                if (operand == device::inst::enable_instance) {
                    unit->enabled[operation] = true;
                } else if (operand == device::inst::disable_instance) {
                    unit->enabled[operation] = false;
                }
            }
        }
        return TransportStatus::Ok;
    }

    gateway::dali::ByteRead query_device_frame(std::uint32_t frame) override {
        const auto address = static_cast<std::uint8_t>(frame >> 16);
        const auto operation = static_cast<std::uint8_t>(frame >> 8);
        const auto operand = static_cast<std::uint8_t>(frame);
        if (address == 0xC1) {
            unsigned responders = 0;
            if (operation == 0x03) {  // COMPARE
                for (const auto& unit : units) {
                    if (unit.initialising && !unit.withdrawn && unit.random <= unit.search) {
                        ++responders;
                    }
                }
            } else if (operation == 0x09) {  // VERIFY SHORT ADDRESS
                for (const auto& unit : units) {
                    if (unit.initialising && unit.short_address == (operand >> 1)) {
                        ++responders;
                    }
                }
            }
            if (responders == 0) {
                return {TransportStatus::Timeout, std::nullopt};
            }
            if (responders > 1) {
                return {TransportStatus::Collision, std::nullopt};
            }
            return {TransportStatus::Ok, std::uint8_t{0xFF}};
        }
        if ((address & 0x81) != 0x01) {
            return {TransportStatus::Timeout, std::nullopt};
        }
        auto* unit = unit_at(address >> 1);
        if (!unit) {
            return {TransportStatus::Timeout, std::nullopt};
        }
        if (operation == device::instance_device &&
            operand == device::cmd::query_number_of_instances) {
            return {TransportStatus::Ok,
                    static_cast<std::uint8_t>(unit->instance_types.size())};
        }
        if (operation < unit->instance_types.size()) {
            if (operand == device::inst::query_instance_type) {
                return {TransportStatus::Ok, unit->instance_types[operation]};
            }
            if (operand == device::inst::query_resolution) {
                return {TransportStatus::Ok, unit->resolutions[operation]};
            }
        }
        return {TransportStatus::Timeout, std::nullopt};
    }
};

struct BoundList {
    std::vector<std::pair<std::uint8_t, std::uint8_t>> pairs;
    static bool test(std::uint8_t device, std::uint8_t instance, void* context) {
        const auto* list = static_cast<const BoundList*>(context);
        for (const auto& pair : list->pairs) {
            if (pair.first == device && pair.second == instance) {
                return true;
            }
        }
        return false;
    }
};

struct InputFixture {
    InputBus bus;
    MemoryLevelStore store;
    LightingService service{bus, store};
    Commissioning commissioning{bus, service};
};

void test_scan_inputs_discovers_devices_and_enables_bound_instances() {
    InputFixture f;
    f.bus.units = {InputBus::Unit::of(0x100, {device::type::push_button,
                                            device::type::push_button}),
                   InputBus::Unit::of(0x200, {device::type::light}, {12})};
    f.bus.units[0].short_address = 3;
    f.bus.units[1].short_address = 7;

    BoundList bound{{{3, 0}, {7, 0}}};
    const auto result = f.commissioning.scan_inputs(BoundList::test, &bound);
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.result.error));

    TEST_ASSERT_TRUE(f.commissioning.devices().present().test(3));
    TEST_ASSERT_TRUE(f.commissioning.devices().present().test(7));
    TEST_ASSERT_EQUAL_UINT8(2, *f.commissioning.devices().instance_count(3));
    TEST_ASSERT_TRUE(f.bus.units[0].enabled[0]);   // bound -> enabled
    TEST_ASSERT_FALSE(f.bus.units[0].enabled[1]);  // unbound -> disabled
    TEST_ASSERT_TRUE(f.bus.units[1].enabled[0]);

    const auto snapshot = f.commissioning.input_snapshot();
    TEST_ASSERT_EQUAL_UINT8(0x88, snapshot.present[0]);       // devices 3 and 7
    TEST_ASSERT_EQUAL_UINT8(0x20 | 2, snapshot.summary[3]);   // 2 instances, has button
    TEST_ASSERT_EQUAL_UINT8(0x80 | 1, snapshot.summary[7]);   // 1 instance, has light
    TEST_ASSERT_EQUAL_UINT8(0, snapshot.summary[0]);
}

void test_assign_inputs_commissions_unaddressed_then_configures() {
    InputFixture f;
    f.bus.units = {InputBus::Unit::of(0x0050, {device::type::push_button}),
                   InputBus::Unit::of(0x0030, {device::type::occupancy})};

    BoundList bound{{{0, 0}, {1, 0}}};
    const auto result = f.commissioning.assign_inputs(CommissionMode::Unaddressed,
                                                      BoundList::test, &bound);
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.result.error));
    // Lowest random address gets the first free short address.
    TEST_ASSERT_EQUAL(0, f.bus.units[1].short_address);
    TEST_ASSERT_EQUAL(1, f.bus.units[0].short_address);
    TEST_ASSERT_EQUAL_UINT32(1, f.bus.quiescent_starts);
    TEST_ASSERT_EQUAL_UINT32(1, f.bus.quiescent_stops);
    TEST_ASSERT_TRUE(f.commissioning.devices().present().test(0));
    TEST_ASSERT_TRUE(f.commissioning.devices().present().test(1));
    TEST_ASSERT_TRUE(f.bus.units[0].enabled[0]);
    TEST_ASSERT_TRUE(f.bus.units[1].enabled[0]);
}

void test_assign_inputs_with_no_free_address_reports_done() {
    InputFixture f;
    for (unsigned index = 0; index < 64; ++index) {
        f.bus.units.push_back(InputBus::Unit::of(0x100 + index, {device::type::push_button}));
        f.bus.units.back().short_address = static_cast<int>(index);
    }
    f.bus.units.push_back(InputBus::Unit::of(0x9000, {device::type::push_button}));
    BoundList bound{};
    const auto result = f.commissioning.assign_inputs(CommissionMode::Unaddressed,
                                                      BoundList::test, &bound);
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.result.error));
    TEST_ASSERT_EQUAL(-1, f.bus.units.back().short_address);  // never commissioned
}

void test_execute_dispatches_commissioning_commands() {
    Fixture f;
    script_bus(f.bus, {0});
    gateway::lighting::RelativeDimmer dimmer;
    gateway::lighting::RelativeDimmer colour_dimmer;
    gateway::inputs::InputMapper inputs;
    auto result = gateway::control::execute(f.service, dimmer, colour_dimmer, f.commissioning, inputs, 0,
                                            gateway::control::ScanBus{});
    TEST_ASSERT_EQUAL_UINT32(64, f.bus.presence_queries.size());
    TEST_ASSERT_TRUE(present(f.commissioning, 0));

    result = gateway::control::execute(f.service, dimmer, colour_dimmer, f.commissioning, inputs, 0,
                                       gateway::control::IdentifyDevice{channel(0)});
    TEST_ASSERT_EQUAL_UINT32(1, f.bus.identifies.size());
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.result.error));

    result = gateway::control::execute(f.service, dimmer, colour_dimmer, f.commissioning, inputs, 0,
                                       gateway::control::ResetAddresses{});
    TEST_ASSERT_EQUAL_UINT32(1, f.bus.clear_calls);
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.result.error));
}

void test_execute_dispatches_input_commands() {
    InputFixture f;
    f.bus.units = {InputBus::Unit::of(0x0050, {device::type::push_button})};
    gateway::lighting::RelativeDimmer dimmer;
    gateway::lighting::RelativeDimmer colour_dimmer;
    gateway::inputs::InputMapper inputs;
    auto config = gateway::config::default_configuration();
    config.inputs[0] = gateway::config::InputChannel{
        0, 0, gateway::config::InputType::PushButton, gateway::config::InputFunction::Toggle,
        1, 0};
    inputs.configure(config);

    auto result = gateway::control::execute(f.service, dimmer, colour_dimmer, f.commissioning,
                                            inputs, 0, gateway::control::ScanInputDevices{});
    // The unit is unaddressed: the inventory stays empty but succeeds.
    TEST_ASSERT_EQUAL_UINT32(0, f.commissioning.devices().present().count());

    result = gateway::control::execute(f.service, dimmer, colour_dimmer, f.commissioning,
                                       inputs, 0,
                                       gateway::control::AssignInputDevices{
                                           CommissionMode::Unaddressed});
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.result.error));
    TEST_ASSERT_EQUAL(0, f.bus.units[0].short_address);
    TEST_ASSERT_TRUE(f.commissioning.devices().present().test(0));
    TEST_ASSERT_TRUE(f.bus.units[0].enabled[0]);  // the bound channel's instance

    // KNX feedback on the channel's switch object reaches the mapper.
    result = gateway::control::execute(f.service, dimmer, colour_dimmer, f.commissioning,
                                       inputs, 0,
                                       gateway::control::InputFeedback{input_channel(0), true});
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.result.error));
    TEST_ASSERT_TRUE(inputs.toggle_state(0));
}

}

void run_commissioning_tests() {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_scan_probes_every_address_and_records_presence);
    RUN_TEST(test_scan_bus_error_keeps_previous_state_and_reports_error);
    RUN_TEST(test_scan_flags_a_vanished_gear_faulty);
    RUN_TEST(test_assign_all_recommissions_everything_and_rescans);
    RUN_TEST(test_assign_unaddressed_starts_at_first_free_address);
    RUN_TEST(test_assign_failure_marks_failed);
    RUN_TEST(test_replace_rejects_a_still_present_target);
    RUN_TEST(test_replace_fails_on_zero_or_two_found);
    RUN_TEST(test_replace_success_reconciles_and_recovers_the_target);
    RUN_TEST(test_replace_commission_transport_failure);
    RUN_TEST(test_identify_invokes_the_transport);
    RUN_TEST(test_poll_next_does_nothing_when_nothing_present);
    RUN_TEST(test_poll_next_is_round_robin_over_present_addresses);
    RUN_TEST(test_poll_next_marks_missing_gear_faulty);
    RUN_TEST(test_poll_next_clears_a_recovered_fault);
    RUN_TEST(test_job_progress_and_found_count_in_snapshot);
    RUN_TEST(test_startup_scans_then_recovers_every_present_address);
    RUN_TEST(test_reset_addresses_clears_then_rescans_empty);
    RUN_TEST(test_reset_addresses_reports_clear_failure_without_scanning);
    RUN_TEST(test_active_phases_gate_new_jobs);
    RUN_TEST(test_execute_dispatches_commissioning_commands);
    RUN_TEST(test_scan_inputs_discovers_devices_and_enables_bound_instances);
    RUN_TEST(test_assign_inputs_commissions_unaddressed_then_configures);
    RUN_TEST(test_assign_inputs_with_no_free_address_reports_done);
    RUN_TEST(test_execute_dispatches_input_commands);
}
