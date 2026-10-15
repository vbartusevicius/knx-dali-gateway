#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <type_traits>
#include <utility>

#include <unity.h>

#include "Fakes.hpp"
#include "commissioning/Commissioning.hpp"
#include "config/Configuration.hpp"
#include "control/Command.hpp"
#include "lighting/LightingService.hpp"
#include "lighting/RelativeDimmer.hpp"

using gateway::dali::Address;
using gateway::dali::AddressKind;
using gateway::dali::ArcLevel;
using gateway::dali::Group;
using gateway::dali::Scene;
using gateway::dali::ShortAddress;
using gateway::dali::TransportStatus;
using gateway::lighting::ControlError;
using gateway::lighting::LightingService;
using gateway::lighting::StateSource;
using gateway::storage::Slot;
using gateway::storage::SlotKind;
using gateway::testing::FakeDaliTransport;
using gateway::testing::MemoryLevelStore;

void setUp() {}
void tearDown() {}
void run_storage_adapter_tests();
void run_bus_tests();
void run_bus_transport_tests();
void run_gear_addressing_tests();
void run_config_tests();
void run_dimmer_tests();
void run_retry_tests();
void run_commissioning_tests();
void run_front_panel_tests();
void run_status_leds_tests();
void run_frame_tests();
void run_colour_tests();
void run_device_frame_tests();
void run_device_addressing_tests();
void run_device_inventory_tests();
void run_event_decoder_tests();
void run_input_mapper_tests();

namespace {

ShortAddress channel(unsigned value) {
    auto result = ShortAddress::from(value);
    TEST_ASSERT_TRUE(result.has_value());
    return result.value();
}

Group group(unsigned value) {
    auto result = Group::from(value);
    TEST_ASSERT_TRUE(result.has_value());
    return result.value();
}

Address channel_address(unsigned value) {
    return Address::short_address(channel(value));
}

ArcLevel level(unsigned value) {
    auto result = ArcLevel::from(value);
    TEST_ASSERT_TRUE(result.has_value());
    return result.value();
}

std::optional<ArcLevel> stored(const MemoryLevelStore& store, const char* key) {
    const auto found = store.values.find(key);
    return found != store.values.end() ? std::optional<ArcLevel>{found->second} : std::nullopt;
}

void test_short_address_boundaries() {
    TEST_ASSERT_EQUAL_UINT8(0, channel(0).value());
    TEST_ASSERT_EQUAL_UINT8(63, channel(63).value());
    TEST_ASSERT_FALSE(ShortAddress::from(64).has_value());
    TEST_ASSERT_FALSE(ShortAddress::from(std::numeric_limits<unsigned>::max()).has_value());
}

void test_group_boundaries() {
    TEST_ASSERT_EQUAL_UINT8(0, group(0).value());
    TEST_ASSERT_EQUAL_UINT8(15, group(15).value());
    TEST_ASSERT_FALSE(Group::from(16).has_value());
    TEST_ASSERT_FALSE(Group::from(std::numeric_limits<unsigned>::max()).has_value());
    TEST_ASSERT_EQUAL_UINT8(group(3).value(), group(3).value());
    TEST_ASSERT_NOT_EQUAL(group(3).value(), group(4).value());
}

void test_scene_boundaries() {
    auto scene = Scene::from(0);
    TEST_ASSERT_TRUE(scene.has_value());
    TEST_ASSERT_EQUAL_UINT8(0, scene->value());
    TEST_ASSERT_TRUE(Scene::from(15).has_value());
    TEST_ASSERT_FALSE(Scene::from(16).has_value());
}

void test_arc_level_rejects_mask_and_overflow() {
    TEST_ASSERT_EQUAL_UINT8(0, level(0).value());
    TEST_ASSERT_EQUAL_UINT8(254, level(254).value());
    TEST_ASSERT_FALSE(ArcLevel::from(255).has_value());
    TEST_ASSERT_FALSE(ArcLevel::from(std::numeric_limits<unsigned>::max()).has_value());
}

void test_address_kinds_and_accessors() {
    const auto short_address = channel_address(9);
    TEST_ASSERT_EQUAL(static_cast<int>(AddressKind::Short), static_cast<int>(short_address.kind()));
    TEST_ASSERT_TRUE(short_address.as_short().has_value());
    TEST_ASSERT_EQUAL_UINT8(9, short_address.as_short()->value());
    TEST_ASSERT_FALSE(short_address.as_group().has_value());

    const auto group_address = Address::group(group(4));
    TEST_ASSERT_EQUAL(static_cast<int>(AddressKind::Group), static_cast<int>(group_address.kind()));
    TEST_ASSERT_TRUE(group_address.as_group().has_value());
    TEST_ASSERT_EQUAL_UINT8(4, group_address.as_group()->value());
    TEST_ASSERT_FALSE(group_address.as_short().has_value());

    const auto broadcast = Address::broadcast();
    TEST_ASSERT_EQUAL(static_cast<int>(AddressKind::Broadcast), static_cast<int>(broadcast.kind()));
    TEST_ASSERT_FALSE(broadcast.as_short().has_value());
    TEST_ASSERT_FALSE(broadcast.as_group().has_value());
}

void test_address_equality_and_copyability() {
    TEST_ASSERT_TRUE(channel_address(9) == channel_address(9));
    TEST_ASSERT_FALSE(channel_address(9) == channel_address(10));
    TEST_ASSERT_TRUE(Address::group(group(2)) == Address::group(group(2)));
    TEST_ASSERT_TRUE(Address::broadcast() == Address::broadcast());
    TEST_ASSERT_TRUE(channel_address(9) != Address::broadcast());
    static_assert(std::is_trivially_copyable_v<Address>);
}

void test_successful_command_saves_target_but_does_not_claim_readback() {
    FakeDaliTransport bus;
    MemoryLevelStore store;
    LightingService service{bus, store};
    auto result = service.set_level(channel_address(63), level(254));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_UINT32(1, bus.writes.size());
    TEST_ASSERT_EQUAL_UINT8(63, bus.writes.front().address.as_short()->value());
    TEST_ASSERT_EQUAL_UINT8(254, bus.writes.front().level.value());
    const auto target = stored(store, "arc_63");
    TEST_ASSERT_TRUE(target.has_value());
    TEST_ASSERT_EQUAL_UINT8(254, target->value());
    TEST_ASSERT_EQUAL(static_cast<int>(StateSource::Commanded), static_cast<int>(result.state.source));
    TEST_ASSERT_EQUAL_UINT32(1, store.commits);
    TEST_ASSERT_TRUE(result.changed_channels.test(63));
    TEST_ASSERT_EQUAL_UINT32(1, result.changed_channels.count());
}

void test_off_is_a_valid_persisted_target() {
    FakeDaliTransport bus;
    MemoryLevelStore store;
    LightingService service{bus, store};
    auto result = service.set_level(channel_address(0), level(0));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.error));
    const auto target = stored(store, "arc_00");
    TEST_ASSERT_TRUE(target.has_value());
    TEST_ASSERT_EQUAL_UINT8(0, target->value());
}

void test_failed_transmission_does_not_persist_or_invent_state() {
    FakeDaliTransport bus;
    bus.write_status = TransportStatus::Collision;
    MemoryLevelStore store;
    LightingService service{bus, store};
    auto result = service.set_level(channel_address(0), level(120));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::TransportFailure), static_cast<int>(result.error));
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Collision),
                      static_cast<int>(result.transport_status));
    TEST_ASSERT_EQUAL_UINT32(0, store.saves);
    TEST_ASSERT_EQUAL_UINT32(0, store.commits);
    TEST_ASSERT_FALSE(result.state.level.has_value());
}

void test_storage_failure_reports_that_command_was_still_sent() {
    FakeDaliTransport bus;
    MemoryLevelStore store;
    store.save_succeeds = false;
    LightingService service{bus, store};
    auto result = service.set_level(channel_address(0), level(20));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::StorageFailure), static_cast<int>(result.error));
    TEST_ASSERT_EQUAL(static_cast<int>(StateSource::Commanded), static_cast<int>(result.state.source));
    TEST_ASSERT_TRUE(result.state.level.has_value());
    TEST_ASSERT_EQUAL_UINT8(20, result.state.level->value());
    TEST_ASSERT_EQUAL_UINT32(1, bus.writes.size());
}

void test_recovery_prefers_actual_state_over_stale_target() {
    FakeDaliTransport bus;
    bus.next_read = {TransportStatus::Ok, level(42)};
    MemoryLevelStore store;
    store.values.insert_or_assign("arc_00", level(200));
    LightingService service{bus, store};
    auto result = service.recover(channel(0));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.error));
    TEST_ASSERT_TRUE(result.state.level.has_value());
    TEST_ASSERT_EQUAL_UINT8(42, result.state.level->value());
    TEST_ASSERT_EQUAL(static_cast<int>(StateSource::Readback), static_cast<int>(result.state.source));
    TEST_ASSERT_EQUAL_UINT32(0, bus.writes.size());
    TEST_ASSERT_EQUAL_UINT32(0, store.loads);
    TEST_ASSERT_EQUAL_UINT8(200, stored(store, "arc_00")->value());
}

void test_timeout_is_not_unsupported_readback() {
    FakeDaliTransport bus;
    bus.next_read = {TransportStatus::Timeout, std::nullopt};
    MemoryLevelStore store;
    store.values.insert_or_assign("arc_00", level(200));
    LightingService service{bus, store};
    auto result = service.recover(channel(0));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::TransportFailure), static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_UINT32(0, bus.writes.size());
    TEST_ASSERT_EQUAL_UINT32(0, store.loads);
    TEST_ASSERT_FALSE(result.state.level.has_value());
}

void test_invalid_readback_does_not_restore_target() {
    FakeDaliTransport bus;
    bus.next_read = {TransportStatus::Ok, std::nullopt};
    MemoryLevelStore store;
    store.values.insert_or_assign("arc_00", level(200));
    LightingService service{bus, store};
    auto result = service.recover(channel(0));
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::InvalidResponse),
                      static_cast<int>(result.transport_status));
    TEST_ASSERT_EQUAL_UINT32(0, bus.writes.size());
    TEST_ASSERT_EQUAL_UINT32(0, store.loads);
}

void test_confirmed_unsupported_readback_restores_saved_target() {
    FakeDaliTransport bus;
    bus.next_read = {TransportStatus::Unsupported, std::nullopt};
    MemoryLevelStore store;
    store.values.insert_or_assign("arc_00", level(0));
    LightingService service{bus, store};
    auto result = service.recover(channel(0));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.error));
    TEST_ASSERT_EQUAL(static_cast<int>(StateSource::RestoredCommand), static_cast<int>(result.state.source));
    TEST_ASSERT_TRUE(result.state.level.has_value());
    TEST_ASSERT_EQUAL_UINT8(0, result.state.level->value());
    TEST_ASSERT_EQUAL_UINT32(1, bus.writes.size());
    TEST_ASSERT_TRUE(result.changed_channels.test(0));
}

void test_missing_saved_target_remains_unknown() {
    FakeDaliTransport bus;
    bus.next_read = {TransportStatus::Unsupported, std::nullopt};
    MemoryLevelStore store;
    LightingService service{bus, store};
    auto result = service.recover(channel(0));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::NoSavedTarget), static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_UINT32(0, bus.writes.size());
    TEST_ASSERT_FALSE(result.state.level.has_value());
}

void test_corrupt_or_unavailable_storage_does_not_restore() {
    for (bool malformed : {false, true}) {
        FakeDaliTransport bus;
        bus.next_read = {TransportStatus::Unsupported, std::nullopt};
        MemoryLevelStore store;
        store.malformed_read = malformed;
        store.read_failure = !malformed;
        LightingService service{bus, store};
        auto result = service.recover(channel(0));
        TEST_ASSERT_EQUAL(static_cast<int>(ControlError::StorageFailure), static_cast<int>(result.error));
        TEST_ASSERT_EQUAL_UINT32(0, bus.writes.size());
    }
}

void test_failed_restore_does_not_claim_a_restored_state() {
    FakeDaliTransport bus;
    bus.next_read = {TransportStatus::Unsupported, std::nullopt};
    bus.write_status = TransportStatus::Timeout;
    MemoryLevelStore store;
    store.values.insert_or_assign("arc_00", level(120));
    LightingService service{bus, store};
    auto result = service.recover(channel(0));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::TransportFailure), static_cast<int>(result.error));
    TEST_ASSERT_FALSE(result.state.level.has_value());
}

void test_channels_keep_independent_state_and_targets() {
    FakeDaliTransport bus;
    MemoryLevelStore store;
    LightingService service{bus, store};
    const auto first = service.set_level(channel_address(0), level(20));
    const auto last = service.set_level(channel_address(63), level(220));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(first.error));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(last.error));
    TEST_ASSERT_TRUE(service.state(channel(0)).level.has_value());
    TEST_ASSERT_TRUE(service.state(channel(63)).level.has_value());
    TEST_ASSERT_EQUAL_UINT8(20, service.state(channel(0)).level->value());
    TEST_ASSERT_EQUAL_UINT8(220, service.state(channel(63)).level->value());
    TEST_ASSERT_FALSE(service.state(channel(1)).level.has_value());
    TEST_ASSERT_EQUAL_UINT8(20, stored(store, "arc_00")->value());
    TEST_ASSERT_EQUAL_UINT8(220, stored(store, "arc_63")->value());
}

void test_switch_on_without_history_goes_to_full() {
    FakeDaliTransport bus;
    MemoryLevelStore store;
    LightingService service{bus, store};
    auto result = service.switch_on(channel_address(7));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_UINT32(1, bus.writes.size());
    TEST_ASSERT_EQUAL_UINT8(254, bus.writes.front().level.value());
    TEST_ASSERT_EQUAL_UINT8(254, stored(store, "arc_07")->value());
}

void test_switch_on_restores_last_active_level() {
    FakeDaliTransport bus;
    MemoryLevelStore store;
    LightingService service{bus, store};
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None),
                      static_cast<int>(service.set_level(channel_address(5), level(100)).error));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None),
                      static_cast<int>(service.switch_off(channel_address(5)).error));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None),
                      static_cast<int>(service.switch_on(channel_address(5)).error));
    TEST_ASSERT_EQUAL_UINT32(3, bus.writes.size());
    TEST_ASSERT_EQUAL_UINT8(0, bus.writes[1].level.value());
    TEST_ASSERT_EQUAL_UINT8(100, bus.writes[2].level.value());
}

void test_last_active_level_survives_a_service_restart() {
    FakeDaliTransport bus;
    MemoryLevelStore store;
    {
        LightingService service{bus, store};
        TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None),
                          static_cast<int>(service.set_level(channel_address(9), level(140)).error));
    }
    LightingService restarted{bus, store};
    const auto writes_before = bus.writes.size();
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None),
                      static_cast<int>(restarted.switch_on(channel_address(9)).error));
    TEST_ASSERT_EQUAL_UINT32(writes_before + 1, bus.writes.size());
    TEST_ASSERT_EQUAL_UINT8(140, bus.writes.back().level.value());
    TEST_ASSERT_TRUE(store.loads > 0);
}

void test_readback_level_becomes_the_switch_on_target() {
    FakeDaliTransport bus;
    bus.next_read = {TransportStatus::Ok, level(77)};
    MemoryLevelStore store;
    LightingService service{bus, store};
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None),
                      static_cast<int>(service.refresh(channel(9)).error));
    bus.writes.clear();
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None),
                      static_cast<int>(service.switch_on(channel_address(9)).error));
    TEST_ASSERT_EQUAL_UINT32(1, bus.writes.size());
    TEST_ASSERT_EQUAL_UINT8(77, bus.writes.front().level.value());
}

void test_switch_on_failure_keeps_the_remembered_level() {
    FakeDaliTransport bus;
    MemoryLevelStore store;
    LightingService service{bus, store};
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None),
                      static_cast<int>(service.set_level(channel_address(3), level(50)).error));
    bus.write_status = TransportStatus::Timeout;
    auto result = service.switch_on(channel_address(3));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::TransportFailure), static_cast<int>(result.error));
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Timeout), static_cast<int>(result.transport_status));
    bus.write_status = TransportStatus::Ok;
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None),
                      static_cast<int>(service.switch_on(channel_address(3)).error));
    TEST_ASSERT_EQUAL_UINT8(50, bus.writes.back().level.value());
}

void test_switch_off_persists_zero() {
    FakeDaliTransport bus;
    MemoryLevelStore store;
    LightingService service{bus, store};
    auto result = service.switch_off(channel_address(4));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_UINT32(1, bus.writes.size());
    TEST_ASSERT_EQUAL_UINT8(0, bus.writes.front().level.value());
    TEST_ASSERT_EQUAL_UINT8(0, stored(store, "arc_04")->value());
}

gateway::config::Configuration configured_groups(
    std::initializer_list<std::pair<unsigned, std::uint16_t>> masks) {
    auto configuration = gateway::config::default_configuration();
    for (const auto& entry : masks) {
        configuration.group_masks[entry.first] = entry.second;
    }
    return configuration;
}

void test_group_command_updates_only_configured_members() {
    FakeDaliTransport bus;
    MemoryLevelStore store;
    LightingService service{bus, store};
    service.configure(configured_groups({{2, 0x0005}, {4, 0x0001}}));  // group 0: channels 2 and 4
    const auto result = service.set_level(Address::group(group(0)), level(80));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_UINT32(1, bus.writes.size());
    TEST_ASSERT_EQUAL_UINT8(0, bus.writes.front().address.as_group()->value());
    TEST_ASSERT_TRUE(service.state(group(0)).level.has_value());
    TEST_ASSERT_EQUAL_UINT8(80, service.state(group(0)).level->value());
    TEST_ASSERT_EQUAL_UINT8(80, service.state(channel(2)).level->value());
    TEST_ASSERT_EQUAL_UINT8(80, service.state(channel(4)).level->value());
    TEST_ASSERT_FALSE(service.state(channel(3)).level.has_value());
    TEST_ASSERT_TRUE(result.changed_channels.test(2));
    TEST_ASSERT_TRUE(result.changed_channels.test(4));
    TEST_ASSERT_EQUAL_UINT32(2, result.changed_channels.count());
    TEST_ASSERT_TRUE(result.changed_groups.test(0));
    TEST_ASSERT_EQUAL_UINT32(1, result.changed_groups.count());
    TEST_ASSERT_EQUAL_UINT8(80, stored(store, "arc_02")->value());
    TEST_ASSERT_EQUAL_UINT8(80, stored(store, "arc_04")->value());
    TEST_ASSERT_FALSE(stored(store, "arc_03").has_value());
    TEST_ASSERT_EQUAL_UINT8(80, stored(store, "g00")->value());
    TEST_ASSERT_EQUAL_UINT32(1, store.commits);
}

void test_broadcast_command_updates_all_channels_and_groups() {
    FakeDaliTransport bus;
    MemoryLevelStore store;
    LightingService service{bus, store};
    const auto result = service.set_level(Address::broadcast(), level(60));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_UINT32(64, result.changed_channels.count());
    TEST_ASSERT_EQUAL_UINT32(16, result.changed_groups.count());
    TEST_ASSERT_EQUAL_UINT8(60, service.state(channel(63)).level->value());
    TEST_ASSERT_EQUAL_UINT8(60, service.state(group(15)).level->value());
    TEST_ASSERT_EQUAL_UINT8(60, stored(store, "arc_63")->value());
    TEST_ASSERT_EQUAL_UINT8(60, stored(store, "b")->value());
    TEST_ASSERT_EQUAL_UINT32(1, store.commits);
}

void test_fixed_switch_on_policy_uses_the_configured_level() {
    FakeDaliTransport bus;
    MemoryLevelStore store;
    LightingService service{bus, store};
    auto configuration = gateway::config::default_configuration();
    configuration.channels[3] = {gateway::config::SwitchOnPolicy::FixedLevel, level(111)};
    configuration.groups[2] = {gateway::config::SwitchOnPolicy::FixedLevel, level(90)};
    configuration.broadcast = {gateway::config::SwitchOnPolicy::FixedLevel, level(70)};
    configuration.group_masks[3] = 0x0004;  // channel 3 in group 2
    service.configure(configuration);

    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None),
                      static_cast<int>(service.switch_on(channel_address(3)).error));
    TEST_ASSERT_EQUAL_UINT8(111, bus.writes.back().level.value());
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None),
                      static_cast<int>(service.switch_on(Address::group(group(2))).error));
    TEST_ASSERT_EQUAL_UINT8(90, bus.writes.back().level.value());
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None),
                      static_cast<int>(service.switch_on(Address::broadcast()).error));
    TEST_ASSERT_EQUAL_UINT8(70, bus.writes.back().level.value());
}

void test_group_switch_on_recalls_the_group_last_active() {
    FakeDaliTransport bus;
    MemoryLevelStore store;
    LightingService service{bus, store};
    service.configure(configured_groups({{0, 0x0002}}));  // group 1: channel 0
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None),
                      static_cast<int>(service.set_level(Address::group(group(1)), level(130)).error));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None),
                      static_cast<int>(service.switch_off(Address::group(group(1))).error));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None),
                      static_cast<int>(service.switch_on(Address::group(group(1))).error));
    TEST_ASSERT_EQUAL_UINT8(130, bus.writes.back().level.value());
}

void test_broadcast_switch_on_recalls_the_broadcast_last_active() {
    FakeDaliTransport bus;
    MemoryLevelStore store;
    LightingService service{bus, store};
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None),
                      static_cast<int>(service.set_level(Address::broadcast(), level(150)).error));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None),
                      static_cast<int>(service.switch_off(Address::broadcast()).error));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None),
                      static_cast<int>(service.switch_on(Address::broadcast()).error));
    TEST_ASSERT_EQUAL_UINT8(150, bus.writes.back().level.value());
}

void test_reconcile_groups_writes_only_differing_memberships() {
    FakeDaliTransport bus;
    bus.next_groups = {TransportStatus::Ok, std::uint16_t{0x0006}};  // member of 1 and 2
    MemoryLevelStore store;
    LightingService service{bus, store};
    service.configure(configured_groups({{0, 0x0005}}));  // want: member of 0 and 2
    const auto result = service.reconcile_groups(channel(0));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_UINT32(1, bus.group_reads.size());
    TEST_ASSERT_EQUAL_UINT32(2, bus.membership_writes.size());
    TEST_ASSERT_EQUAL_UINT8(0, bus.membership_writes[0].group.value());
    TEST_ASSERT_TRUE(bus.membership_writes[0].member);
    TEST_ASSERT_EQUAL_UINT8(1, bus.membership_writes[1].group.value());
    TEST_ASSERT_FALSE(bus.membership_writes[1].member);
}

void test_reconcile_groups_writes_nothing_when_membership_matches() {
    FakeDaliTransport bus;
    bus.next_groups = {TransportStatus::Ok, std::uint16_t{0x0005}};
    MemoryLevelStore store;
    LightingService service{bus, store};
    service.configure(configured_groups({{0, 0x0005}}));
    const auto result = service.reconcile_groups(channel(0));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_UINT32(0, bus.membership_writes.size());
}

void test_reconcile_groups_reports_read_failure_without_writing() {
    FakeDaliTransport bus;
    bus.next_groups = {TransportStatus::Timeout, std::nullopt};
    MemoryLevelStore store;
    LightingService service{bus, store};
    service.configure(configured_groups({{0, 0xFFFF}}));
    const auto result = service.reconcile_groups(channel(0));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::TransportFailure), static_cast<int>(result.error));
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Timeout), static_cast<int>(result.transport_status));
    TEST_ASSERT_EQUAL_UINT32(0, bus.membership_writes.size());
}

void test_execute_dispatches_every_command() {
    FakeDaliTransport bus;
    bus.next_read = {TransportStatus::Ok, level(60)};
    bus.next_groups = {TransportStatus::Ok, std::uint16_t{0}};
    MemoryLevelStore store;
    LightingService service{bus, store};
    gateway::lighting::RelativeDimmer dimmer;
    gateway::lighting::RelativeDimmer colour_dimmer;
    gateway::commissioning::Commissioning commissioning{bus, service};
    gateway::inputs::InputMapper inputs;
    constexpr std::uint32_t now = 1'000;

    auto result = gateway::control::execute(service, dimmer, colour_dimmer, commissioning, inputs, now,
                                            gateway::control::SetLevel{channel_address(1), level(33)});
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.result.error));
    TEST_ASSERT_EQUAL_UINT8(33, bus.writes.back().level.value());

    TEST_ASSERT_EQUAL(
        static_cast<int>(ControlError::None),
        static_cast<int>(gateway::control::execute(service, dimmer, colour_dimmer, commissioning, inputs, now,
                                                   gateway::control::SwitchOn{channel_address(1)})
                             .result.error));
    TEST_ASSERT_EQUAL_UINT8(33, bus.writes.back().level.value());

    TEST_ASSERT_EQUAL(
        static_cast<int>(ControlError::None),
        static_cast<int>(gateway::control::execute(service, dimmer, colour_dimmer, commissioning, inputs, now,
                                                   gateway::control::SwitchOff{channel_address(1)})
                             .result.error));
    TEST_ASSERT_EQUAL_UINT8(0, bus.writes.back().level.value());

    const auto reads_before = bus.reads.size();
    TEST_ASSERT_EQUAL(
        static_cast<int>(ControlError::None),
        static_cast<int>(gateway::control::execute(service, dimmer, colour_dimmer, commissioning, inputs, now,
                                                   gateway::control::Refresh{channel(1)})
                             .result.error));
    TEST_ASSERT_EQUAL_UINT32(reads_before + 1, bus.reads.size());

    TEST_ASSERT_EQUAL(
        static_cast<int>(ControlError::None),
        static_cast<int>(gateway::control::execute(service, dimmer, colour_dimmer, commissioning, inputs, now,
                                                   gateway::control::Recover{channel(1)})
                             .result.error));
    TEST_ASSERT_EQUAL_UINT32(reads_before + 2, bus.reads.size());

    TEST_ASSERT_EQUAL(
        static_cast<int>(ControlError::None),
        static_cast<int>(gateway::control::execute(service, dimmer, colour_dimmer, commissioning, inputs, now,
                                                   gateway::control::ReconcileGroups{channel(1)})
                             .result.error));
    TEST_ASSERT_EQUAL_UINT32(1, bus.group_reads.size());

    // Commissioning commands reach the commissioning service.
    const auto scanned = gateway::control::execute(service, dimmer, colour_dimmer, commissioning, inputs, now,
                                                   gateway::control::ScanBus{});
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(scanned.result.error));
    TEST_ASSERT_EQUAL_UINT32(64, bus.presence_queries.size());

    bus.commission_replies.push_back({TransportStatus::Ok, 2});
    const auto assigned =
        gateway::control::execute(service, dimmer, colour_dimmer, commissioning, inputs, now,
                                  gateway::control::AssignAddresses{gateway::dali::CommissionMode::All});
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(assigned.result.error));
    TEST_ASSERT_EQUAL_UINT32(1, bus.commission_calls.size());
    TEST_ASSERT_EQUAL(static_cast<int>(gateway::dali::CommissionMode::All),
                      static_cast<int>(bus.commission_calls.front().mode));
}

void test_execute_routes_dim_commands_to_the_dimmer() {
    FakeDaliTransport bus;
    bus.next_read = {TransportStatus::Ok, level(60)};
    MemoryLevelStore store;
    LightingService service{bus, store};
    gateway::lighting::RelativeDimmer dimmer;
    gateway::lighting::RelativeDimmer colour_dimmer;
    gateway::commissioning::Commissioning commissioning{bus, service};
    gateway::inputs::InputMapper inputs;

    const auto started = gateway::control::execute(
        service, dimmer, colour_dimmer, commissioning, inputs, 0,
        gateway::control::DimStart{channel_address(2), gateway::lighting::DimDirection::Up});
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(started.result.error));
    TEST_ASSERT_TRUE(dimmer.running(channel_address(2)));

    std::vector<gateway::lighting::DimStep> steps;
    dimmer.poll(
        0, [&steps](gateway::lighting::DimStep step) { steps.push_back(step); },
        [](gateway::lighting::DimExpiry) {});
    TEST_ASSERT_EQUAL_UINT32(1, steps.size());
    const auto step = service.dim_step(steps.front().address, steps.front().direction, steps.front().first);
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(step.error));
    TEST_ASSERT_EQUAL_UINT32(1, bus.gear_commands.size());
    TEST_ASSERT_EQUAL(static_cast<int>(gateway::dali::GearCommand::OnAndStepUp),
                      static_cast<int>(bus.gear_commands.front().command));

    const auto stopped = gateway::control::execute(service, dimmer, colour_dimmer, commissioning, inputs, 0,
                                                   gateway::control::DimStop{channel_address(2)});
    TEST_ASSERT_FALSE(dimmer.running(channel_address(2)));
    TEST_ASSERT_EQUAL_UINT32(1, bus.reads.size());  // finish_dimming refreshed the channel
    TEST_ASSERT_TRUE(stopped.result.changed_channels.test(2));

    const auto idle_stop = gateway::control::execute(service, dimmer, colour_dimmer, commissioning, inputs, 0,
                                                     gateway::control::DimStop{channel_address(2)});
    TEST_ASSERT_EQUAL_UINT32(1, bus.reads.size());  // no second readback when nothing ran
    TEST_ASSERT_EQUAL_UINT32(0, idle_stop.result.changed_channels.count());
}

void test_dim_step_command_selection() {
    FakeDaliTransport bus;
    MemoryLevelStore store;
    LightingService service{bus, store};
    const auto target = channel_address(4);

    (void)service.dim_step(target, gateway::lighting::DimDirection::Up, true);
    (void)service.dim_step(target, gateway::lighting::DimDirection::Up, false);
    (void)service.dim_step(target, gateway::lighting::DimDirection::Down, true);
    (void)service.dim_step(target, gateway::lighting::DimDirection::Down, false);
    TEST_ASSERT_EQUAL_UINT32(4, bus.gear_commands.size());
    TEST_ASSERT_EQUAL(static_cast<int>(gateway::dali::GearCommand::OnAndStepUp),
                      static_cast<int>(bus.gear_commands[0].command));
    TEST_ASSERT_EQUAL(static_cast<int>(gateway::dali::GearCommand::Up),
                      static_cast<int>(bus.gear_commands[1].command));
    TEST_ASSERT_EQUAL(static_cast<int>(gateway::dali::GearCommand::Down),
                      static_cast<int>(bus.gear_commands[2].command));
    TEST_ASSERT_EQUAL(static_cast<int>(gateway::dali::GearCommand::Down),
                      static_cast<int>(bus.gear_commands[3].command));
    for (const auto& write : bus.gear_commands) {
        TEST_ASSERT_TRUE(write.address == target);
    }
}

void test_dim_step_reports_transport_failure() {
    FakeDaliTransport bus;
    bus.write_status = TransportStatus::Timeout;
    MemoryLevelStore store;
    LightingService service{bus, store};
    const auto result = service.dim_step(channel_address(0), gateway::lighting::DimDirection::Up, true);
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::TransportFailure), static_cast<int>(result.error));
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Timeout), static_cast<int>(result.transport_status));
}

void test_finish_dimming_refreshes_a_short_address() {
    FakeDaliTransport bus;
    bus.next_read = {TransportStatus::Ok, level(90)};
    MemoryLevelStore store;
    LightingService service{bus, store};
    const auto result = service.finish_dimming(channel_address(6));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_UINT32(1, bus.reads.size());
    TEST_ASSERT_EQUAL_UINT8(6, bus.reads.front().value());
    TEST_ASSERT_TRUE(result.changed_channels.test(6));
    TEST_ASSERT_EQUAL_UINT8(90, result.state.level->value());
}

void test_finish_dimming_refreshes_group_members_and_derives_group_state() {
    FakeDaliTransport bus;
    bus.next_read = {TransportStatus::Ok, level(70)};
    MemoryLevelStore store;
    LightingService service{bus, store};
    service.configure(configured_groups({{2, 0x0004}, {5, 0x0004}}));  // group 2: channels 2, 5
    const auto result = service.finish_dimming(Address::group(group(2)));
    TEST_ASSERT_EQUAL_UINT32(2, bus.reads.size());
    TEST_ASSERT_TRUE(result.changed_channels.test(2));
    TEST_ASSERT_TRUE(result.changed_channels.test(5));
    TEST_ASSERT_EQUAL_UINT32(2, result.changed_channels.count());
    TEST_ASSERT_TRUE(result.changed_groups.test(2));
    TEST_ASSERT_EQUAL_UINT8(70, service.state(group(2)).level->value());
    TEST_ASSERT_EQUAL_UINT8(70, service.state(channel(2)).level->value());
}

void test_finish_dimming_broadcast_refreshes_all_and_derives_each_group() {
    FakeDaliTransport bus;
    bus.next_read = {TransportStatus::Ok, level(40)};
    MemoryLevelStore store;
    LightingService service{bus, store};
    service.configure(configured_groups({{0, 0x0007}}));  // group 0,1,2 have channel 0
    const auto result = service.finish_dimming(Address::broadcast());
    TEST_ASSERT_EQUAL_UINT32(64, bus.reads.size());
    TEST_ASSERT_EQUAL_UINT32(64, result.changed_channels.count());
    TEST_ASSERT_EQUAL_UINT32(3, result.changed_groups.count());
    TEST_ASSERT_EQUAL_UINT8(40, service.state(group(0)).level->value());
    TEST_ASSERT_FALSE(service.state(group(15)).level.has_value());  // no members -> unchanged
}

void test_group_state_uses_lowest_numbered_answering_member() {
    // Channel 1 times out, channel 4 answers: group takes channel 4's level.
    MemoryLevelStore store;
    struct : gateway::dali::Transport {
        gateway::dali::TransportStatus set_level(gateway::dali::Address, ArcLevel) override {
            return gateway::dali::TransportStatus::Ok;
        }
        gateway::dali::TransportStatus send(gateway::dali::Address, gateway::dali::GearCommand) override {
            return gateway::dali::TransportStatus::Ok;
        }
        gateway::dali::TransportStatus go_to_scene(gateway::dali::Address, Scene) override {
            return gateway::dali::TransportStatus::Ok;
        }
        gateway::dali::TransportStatus send_command(gateway::dali::Address, std::uint8_t, bool) override {
            return gateway::dali::TransportStatus::Ok;
        }
        gateway::dali::TransportStatus set_dtr(gateway::dali::Dtr, std::uint8_t) override {
            return gateway::dali::TransportStatus::Ok;
        }
        gateway::dali::TransportStatus enable_device_type(std::uint8_t) override {
            return gateway::dali::TransportStatus::Ok;
        }
        gateway::dali::ByteRead query(ShortAddress, std::uint8_t) override {
            return {gateway::dali::TransportStatus::Timeout, std::nullopt};
        }
        gateway::dali::LevelRead read_level(ShortAddress address) override {
            return address.value() == 1 ? gateway::dali::LevelRead{TransportStatus::Timeout, std::nullopt}
                                        : gateway::dali::LevelRead{TransportStatus::Ok, ArcLevel::from(55)};
        }
        gateway::dali::GroupsRead read_groups(ShortAddress) override {
            return {TransportStatus::Ok, std::uint16_t{0}};
        }
        gateway::dali::TransportStatus set_group_membership(ShortAddress, Group, bool) override {
            return gateway::dali::TransportStatus::Ok;
        }
        gateway::dali::GearPresence query_present(ShortAddress) override {
            return {gateway::dali::TransportStatus::Timeout, false};
        }
        gateway::dali::GearStatusRead query_status(ShortAddress) override {
            return {gateway::dali::TransportStatus::Timeout, std::nullopt};
        }
        gateway::dali::CommissionResult commission(gateway::dali::CommissionMode, ShortAddress,
                                                   unsigned) override {
            return {gateway::dali::TransportStatus::Unsupported, 0};
        }
        gateway::dali::TransportStatus identify(ShortAddress) override {
            return gateway::dali::TransportStatus::Unsupported;
        }
        gateway::dali::TransportStatus clear_short_addresses() override {
            return gateway::dali::TransportStatus::Unsupported;
        }
    } selective_bus;
    LightingService selective{selective_bus, store};
    selective.configure(configured_groups({{1, 0x0002}, {4, 0x0002}}));  // group 1: channels 1, 4
    const auto finished = selective.finish_dimming(Address::group(group(1)));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::TransportFailure), static_cast<int>(finished.error));
    TEST_ASSERT_TRUE(finished.changed_groups.test(1));
    TEST_ASSERT_EQUAL_UINT8(55, selective.state(group(1)).level->value());
}

void test_recall_scene_sends_broadcast_scene_then_reads_everything() {
    FakeDaliTransport bus;
    bus.next_read = {TransportStatus::Ok, level(120)};
    MemoryLevelStore store;
    LightingService service{bus, store};
    service.configure(configured_groups({{0, 0x0001}}));
    const auto result = service.recall_scene(*Scene::from(3));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_UINT32(1, bus.scenes_recalled.size());
    TEST_ASSERT_EQUAL_UINT8(3, bus.scenes_recalled.front().value());
    TEST_ASSERT_EQUAL_UINT32(64, bus.reads.size());
    TEST_ASSERT_TRUE(result.changed_groups.test(0));
    TEST_ASSERT_EQUAL_UINT8(120, service.state(group(0)).level->value());
}

void test_recall_scene_failure_does_not_refresh() {
    FakeDaliTransport bus;
    bus.write_status = TransportStatus::Failure;
    MemoryLevelStore store;
    LightingService service{bus, store};
    const auto result = service.recall_scene(*Scene::from(0));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::TransportFailure), static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_UINT32(0, bus.reads.size());
}

void test_store_scene_sends_store_sequence_without_state_change() {
    FakeDaliTransport bus;
    MemoryLevelStore store;
    LightingService service{bus, store};
    const auto result = service.store_scene(*Scene::from(11));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.error));
    // STORE ACTUAL LEVEL IN DTR0 (twice), EDT8 + COPY REPORT TO TEMPORARY so
    // DT8 gear learns colour too, then STORE DTR AS SCENE 11 (twice).
    TEST_ASSERT_EQUAL_UINT32(3, bus.command_writes.size());
    TEST_ASSERT_EQUAL_UINT8(0x21, bus.command_writes[0].opcode);
    TEST_ASSERT_TRUE(bus.command_writes[0].twice);
    TEST_ASSERT_EQUAL_UINT8(0xEE, bus.command_writes[1].opcode);
    TEST_ASSERT_FALSE(bus.command_writes[1].twice);
    TEST_ASSERT_EQUAL_UINT8(0x40 + 11, bus.command_writes[2].opcode);
    TEST_ASSERT_TRUE(bus.command_writes[2].twice);
    TEST_ASSERT_EQUAL_UINT32(1, bus.device_type_enables.size());
    TEST_ASSERT_EQUAL_UINT8(8, bus.device_type_enables[0]);
    TEST_ASSERT_EQUAL_UINT32(0, bus.reads.size());
    TEST_ASSERT_EQUAL_UINT32(0, result.changed_channels.count());
}

void test_address_of_returns_each_command_address() {
    const auto target = channel(12);
    const gateway::control::Command commands[] = {
        gateway::control::SetLevel{channel_address(12), level(1)},
        gateway::control::SwitchOn{channel_address(12)},
        gateway::control::SwitchOff{channel_address(12)},
        gateway::control::Refresh{target},
        gateway::control::Recover{target},
        gateway::control::ReconcileGroups{target},
        gateway::control::DimStart{channel_address(12), gateway::lighting::DimDirection::Up},
        gateway::control::DimStop{channel_address(12)},
        gateway::control::ReplaceDevice{target},
        gateway::control::IdentifyDevice{target},
    };
    for (const auto& command : commands) {
        const auto resolved = gateway::control::address_of(command);
        TEST_ASSERT_TRUE(resolved.as_short().has_value());
        TEST_ASSERT_EQUAL_UINT8(12, resolved.as_short()->value());
    }
    const auto grouped = gateway::control::SetLevel{Address::group(group(3)), level(1)};
    TEST_ASSERT_EQUAL_UINT8(3, gateway::control::address_of(grouped).as_group()->value());
    const auto recall = gateway::control::Command{gateway::control::RecallScene{*Scene::from(0)}};
    TEST_ASSERT_TRUE(gateway::control::address_of(recall) == Address::broadcast());
    const auto store = gateway::control::Command{gateway::control::StoreScene{*Scene::from(0)}};
    TEST_ASSERT_TRUE(gateway::control::address_of(store) == Address::broadcast());
    const auto scan = gateway::control::Command{gateway::control::ScanBus{}};
    TEST_ASSERT_TRUE(gateway::control::address_of(scan) == Address::broadcast());
    const auto assign =
        gateway::control::Command{gateway::control::AssignAddresses{gateway::dali::CommissionMode::All}};
    TEST_ASSERT_TRUE(gateway::control::address_of(assign) == Address::broadcast());
}

void test_refresh_never_replays_saved_commands() {
    FakeDaliTransport bus;
    bus.next_read = {TransportStatus::Unsupported, std::nullopt};
    MemoryLevelStore store;
    store.values.insert_or_assign("arc_00", level(200));
    LightingService service{bus, store};
    auto result = service.refresh(channel(0));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::TransportFailure), static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_UINT32(0, store.loads);
    TEST_ASSERT_EQUAL_UINT32(0, bus.writes.size());
}

void test_failed_commit_is_reported_but_the_command_was_sent() {
    FakeDaliTransport bus;
    MemoryLevelStore store;
    store.commit_succeeds = false;
    LightingService service{bus, store};
    const auto result = service.set_level(channel_address(0), level(20));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::StorageFailure), static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_UINT32(1, bus.writes.size());
    TEST_ASSERT_EQUAL_UINT32(1, store.commits);
}

}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_short_address_boundaries);
    RUN_TEST(test_group_boundaries);
    RUN_TEST(test_scene_boundaries);
    RUN_TEST(test_address_kinds_and_accessors);
    RUN_TEST(test_address_equality_and_copyability);
    RUN_TEST(test_arc_level_rejects_mask_and_overflow);
    RUN_TEST(test_successful_command_saves_target_but_does_not_claim_readback);
    RUN_TEST(test_off_is_a_valid_persisted_target);
    RUN_TEST(test_failed_transmission_does_not_persist_or_invent_state);
    RUN_TEST(test_storage_failure_reports_that_command_was_still_sent);
    RUN_TEST(test_recovery_prefers_actual_state_over_stale_target);
    RUN_TEST(test_timeout_is_not_unsupported_readback);
    RUN_TEST(test_invalid_readback_does_not_restore_target);
    RUN_TEST(test_confirmed_unsupported_readback_restores_saved_target);
    RUN_TEST(test_missing_saved_target_remains_unknown);
    RUN_TEST(test_corrupt_or_unavailable_storage_does_not_restore);
    RUN_TEST(test_failed_restore_does_not_claim_a_restored_state);
    RUN_TEST(test_channels_keep_independent_state_and_targets);
    RUN_TEST(test_switch_on_without_history_goes_to_full);
    RUN_TEST(test_switch_on_restores_last_active_level);
    RUN_TEST(test_last_active_level_survives_a_service_restart);
    RUN_TEST(test_readback_level_becomes_the_switch_on_target);
    RUN_TEST(test_switch_on_failure_keeps_the_remembered_level);
    RUN_TEST(test_switch_off_persists_zero);
    RUN_TEST(test_group_command_updates_only_configured_members);
    RUN_TEST(test_broadcast_command_updates_all_channels_and_groups);
    RUN_TEST(test_fixed_switch_on_policy_uses_the_configured_level);
    RUN_TEST(test_group_switch_on_recalls_the_group_last_active);
    RUN_TEST(test_broadcast_switch_on_recalls_the_broadcast_last_active);
    RUN_TEST(test_reconcile_groups_writes_only_differing_memberships);
    RUN_TEST(test_reconcile_groups_writes_nothing_when_membership_matches);
    RUN_TEST(test_reconcile_groups_reports_read_failure_without_writing);
    RUN_TEST(test_execute_dispatches_every_command);
    RUN_TEST(test_address_of_returns_each_command_address);
    RUN_TEST(test_refresh_never_replays_saved_commands);
    RUN_TEST(test_failed_commit_is_reported_but_the_command_was_sent);
    RUN_TEST(test_execute_routes_dim_commands_to_the_dimmer);
    RUN_TEST(test_dim_step_command_selection);
    RUN_TEST(test_dim_step_reports_transport_failure);
    RUN_TEST(test_finish_dimming_refreshes_a_short_address);
    RUN_TEST(test_finish_dimming_refreshes_group_members_and_derives_group_state);
    RUN_TEST(test_finish_dimming_broadcast_refreshes_all_and_derives_each_group);
    RUN_TEST(test_group_state_uses_lowest_numbered_answering_member);
    RUN_TEST(test_recall_scene_sends_broadcast_scene_then_reads_everything);
    RUN_TEST(test_recall_scene_failure_does_not_refresh);
    RUN_TEST(test_store_scene_sends_store_sequence_without_state_change);
    run_dimmer_tests();
    run_retry_tests();
    run_commissioning_tests();
    run_front_panel_tests();
    run_status_leds_tests();
    run_frame_tests();
    run_colour_tests();
    run_config_tests();
    run_storage_adapter_tests();
    run_bus_tests();
    run_bus_transport_tests();
    run_gear_addressing_tests();
    run_device_frame_tests();
    run_device_addressing_tests();
    run_device_inventory_tests();
    run_event_decoder_tests();
    run_input_mapper_tests();
    void run_diagnostics_and_network_tests();
    run_diagnostics_and_network_tests();
    return UNITY_END() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
