#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <type_traits>
#include <utility>

#include <unity.h>

#include "Fakes.hpp"
#include "config/Configuration.hpp"
#include "control/Command.hpp"
#include "lighting/LightingService.hpp"

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
void run_dali_adapter_tests();
void run_config_tests();

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
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Collision), static_cast<int>(result.transport_status));
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
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::InvalidResponse), static_cast<int>(result.transport_status));
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

gateway::config::Configuration configured_groups(std::initializer_list<std::pair<unsigned, std::uint16_t>> masks) {
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

    auto result = gateway::control::execute(service, gateway::control::SetLevel{channel_address(1), level(33)});
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_UINT8(33, bus.writes.back().level.value());

    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None),
                      static_cast<int>(gateway::control::execute(service, gateway::control::SwitchOn{channel_address(1)}).error));
    TEST_ASSERT_EQUAL_UINT8(33, bus.writes.back().level.value());

    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None),
                      static_cast<int>(gateway::control::execute(service, gateway::control::SwitchOff{channel_address(1)}).error));
    TEST_ASSERT_EQUAL_UINT8(0, bus.writes.back().level.value());

    const auto reads_before = bus.reads.size();
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None),
                      static_cast<int>(gateway::control::execute(service, gateway::control::Refresh{channel(1)}).error));
    TEST_ASSERT_EQUAL_UINT32(reads_before + 1, bus.reads.size());

    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None),
                      static_cast<int>(gateway::control::execute(service, gateway::control::Recover{channel(1)}).error));
    TEST_ASSERT_EQUAL_UINT32(reads_before + 2, bus.reads.size());

    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None),
                      static_cast<int>(gateway::control::execute(service, gateway::control::ReconcileGroups{channel(1)}).error));
    TEST_ASSERT_EQUAL_UINT32(1, bus.group_reads.size());
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
    };
    for (const auto& command : commands) {
        const auto resolved = gateway::control::address_of(command);
        TEST_ASSERT_TRUE(resolved.as_short().has_value());
        TEST_ASSERT_EQUAL_UINT8(12, resolved.as_short()->value());
    }
    const auto grouped = gateway::control::SetLevel{Address::group(group(3)), level(1)};
    TEST_ASSERT_EQUAL_UINT8(3, gateway::control::address_of(grouped).as_group()->value());
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
    run_config_tests();
    run_storage_adapter_tests();
    run_dali_adapter_tests();
    return UNITY_END() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
