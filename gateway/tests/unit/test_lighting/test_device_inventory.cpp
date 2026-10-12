// Copyright (c) 2025 the knx-dali-gateway contributors
#include <algorithm>
#include <map>
#include <optional>
#include <vector>

#include <unity.h>

#include "dali/Transport.hpp"
#include "dali/devices/DeviceFrames.hpp"
#include "dali/devices/DeviceInventory.hpp"

namespace {

using gateway::dali::ByteRead;
using gateway::dali::CommissionMode;
using gateway::dali::CommissionResult;
using gateway::dali::DeviceInventory;
using gateway::dali::ShortAddress;
using gateway::dali::TransportStatus;
namespace device = gateway::dali::device;

struct SimInstance {
    std::uint8_t type;
    std::uint8_t resolution = 0;
    std::optional<std::uint8_t> scheme;
    std::optional<std::uint32_t> filter;
    bool enabled = false;

    static SimInstance of(std::uint8_t type, std::uint8_t resolution = 0) {
        SimInstance instance;
        instance.type = type;
        instance.resolution = resolution;
        return instance;
    }
};

// Answers the Part 103 queries used by DeviceInventory and records the
// configuration commands the inventory sends.
class InventoryBus final : public gateway::dali::Transport {
public:
    std::map<std::uint8_t, std::vector<SimInstance>> devices;
    std::vector<std::pair<std::uint32_t, bool>> frames;
    std::uint8_t dtr0 = 0;
    std::uint8_t dtr1 = 0;
    std::uint8_t dtr2 = 0;

    TransportStatus set_level(gateway::dali::Address, gateway::dali::ArcLevel) override {
        return TransportStatus::Ok;
    }
    TransportStatus send(gateway::dali::Address, gateway::dali::GearCommand) override {
        return TransportStatus::Ok;
    }
    TransportStatus send_command(gateway::dali::Address, std::uint8_t, bool) override {
        return TransportStatus::Ok;
    }
    TransportStatus set_dtr(gateway::dali::Dtr, std::uint8_t) override { return TransportStatus::Ok; }
    TransportStatus enable_device_type(std::uint8_t) override { return TransportStatus::Ok; }
    ByteRead query(gateway::dali::ShortAddress, std::uint8_t) override {
        return {TransportStatus::Timeout, std::nullopt};
    }
    TransportStatus go_to_scene(gateway::dali::Address, gateway::dali::Scene) override {
        return TransportStatus::Ok;
    }
    gateway::dali::LevelRead read_level(gateway::dali::ShortAddress) override {
        return {TransportStatus::Timeout, std::nullopt};
    }
    gateway::dali::GroupsRead read_groups(gateway::dali::ShortAddress) override {
        return {TransportStatus::Timeout, std::nullopt};
    }
    TransportStatus set_group_membership(gateway::dali::ShortAddress, gateway::dali::Group, bool) override {
        return TransportStatus::Ok;
    }
    gateway::dali::GearPresence query_present(gateway::dali::ShortAddress) override {
        return {TransportStatus::Timeout, false};
    }
    gateway::dali::GearStatusRead query_status(gateway::dali::ShortAddress) override {
        return {TransportStatus::Timeout, std::nullopt};
    }
    CommissionResult commission(CommissionMode, ShortAddress, unsigned) override {
        return {TransportStatus::Unsupported, 0};
    }
    TransportStatus identify(gateway::dali::ShortAddress) override { return TransportStatus::Ok; }
    TransportStatus clear_short_addresses() override { return TransportStatus::Ok; }

    TransportStatus send_device_frame(std::uint32_t frame, bool twice) override {
        frames.push_back({frame, twice});
        const auto address = static_cast<std::uint8_t>(frame >> 16);
        const auto instance = static_cast<std::uint8_t>(frame >> 8);
        const auto opcode = static_cast<std::uint8_t>(frame);
        if (address == 0xC1) {
            switch (instance) {
            case 0x30:
                dtr0 = opcode;
                break;
            case 0x31:
                dtr1 = opcode;
                break;
            case 0x32:
                dtr2 = opcode;
                break;
            default:
                break;
            }
            return TransportStatus::Ok;
        }
        const auto index = static_cast<std::uint8_t>(address >> 1);
        auto it = devices.find(index);
        if (it == devices.end() || instance >= it->second.size()) {
            return TransportStatus::Ok;
        }
        auto& sim = it->second[instance];
        switch (opcode) {
        case device::inst::set_event_scheme:
            sim.scheme = dtr0;
            break;
        case device::inst::set_event_filter:
            sim.filter = static_cast<std::uint32_t>(dtr0) | (static_cast<std::uint32_t>(dtr1) << 8) |
                         (static_cast<std::uint32_t>(dtr2) << 16);
            break;
        case device::inst::enable_instance:
            sim.enabled = true;
            break;
        case device::inst::disable_instance:
            sim.enabled = false;
            break;
        default:
            break;
        }
        return TransportStatus::Ok;
    }

    ByteRead query_device_frame(std::uint32_t frame) override {
        const auto address = static_cast<std::uint8_t>(frame >> 16);
        const auto instance = static_cast<std::uint8_t>(frame >> 8);
        const auto opcode = static_cast<std::uint8_t>(frame);
        if ((address & 0x81) != 0x01) {  // only short device addresses answer
            return {TransportStatus::Timeout, std::nullopt};
        }
        const auto index = static_cast<std::uint8_t>(address >> 1);
        const auto it = devices.find(index);
        if (it == devices.end()) {
            return {TransportStatus::Timeout, std::nullopt};
        }
        if (instance == device::instance_device && opcode == device::cmd::query_number_of_instances) {
            return {TransportStatus::Ok, static_cast<std::uint8_t>(it->second.size())};
        }
        if (instance < it->second.size()) {
            const auto& sim = it->second[instance];
            if (opcode == device::inst::query_instance_type) {
                return {TransportStatus::Ok, sim.type};
            }
            if (opcode == device::inst::query_resolution) {
                return {TransportStatus::Ok, sim.resolution};
            }
        }
        return {TransportStatus::Timeout, std::nullopt};
    }
};

bool bound_all(std::uint8_t, std::uint8_t, void*) {
    return true;
}

struct BoundList {
    std::vector<std::pair<std::uint8_t, std::uint8_t>> pairs;
    static bool test(std::uint8_t device, std::uint8_t instance, void* context) {
        const auto* list = static_cast<const BoundList*>(context);
        return std::find(list->pairs.begin(), list->pairs.end(), std::make_pair(device, instance)) !=
               list->pairs.end();
    }
};

void test_scan_finds_devices_and_instances() {
    InventoryBus bus;
    bus.devices[3] = {SimInstance::of(device::type::push_button), SimInstance::of(device::type::push_button)};
    bus.devices[7] = {SimInstance::of(device::type::light, 12)};
    DeviceInventory inventory{bus};

    TEST_ASSERT_EQUAL(TransportStatus::Ok, inventory.scan());
    TEST_ASSERT_EQUAL_UINT32(2, inventory.present().count());
    TEST_ASSERT_TRUE(inventory.present().test(3));
    TEST_ASSERT_TRUE(inventory.present().test(7));
    TEST_ASSERT_EQUAL_UINT8(2, *inventory.instance_count(3));
    TEST_ASSERT_EQUAL_UINT8(1, *inventory.instance_count(7));
    TEST_ASSERT_FALSE(inventory.instance_count(0).has_value());
    TEST_ASSERT_EQUAL_UINT8(device::type::push_button, *inventory.instance_type(3, 0));
    TEST_ASSERT_EQUAL_UINT8(device::type::push_button, *inventory.instance_type(3, 1));
    TEST_ASSERT_EQUAL_UINT8(device::type::light, *inventory.instance_type(7, 0));
    TEST_ASSERT_FALSE(inventory.instance_type(3, 2).has_value());
    TEST_ASSERT_EQUAL_UINT8(12, *inventory.resolution(7, 0));
    TEST_ASSERT_FALSE(inventory.resolution(3, 0).has_value());
    TEST_ASSERT_TRUE(inventory.has_type(3, device::type::push_button));
    TEST_ASSERT_TRUE(inventory.has_type(7, device::type::light));
    TEST_ASSERT_FALSE(inventory.has_type(3, device::type::occupancy));
}

void test_apply_configures_scheme_filter_and_enable() {
    InventoryBus bus;
    bus.devices[3] = {SimInstance::of(device::type::push_button)};
    bus.devices[7] = {SimInstance::of(device::type::light, 12)};
    DeviceInventory inventory{bus};
    TEST_ASSERT_EQUAL(TransportStatus::Ok, inventory.scan());
    TEST_ASSERT_EQUAL(TransportStatus::Ok, inventory.apply(bound_all, nullptr));

    const auto& button = bus.devices[3][0];
    TEST_ASSERT_TRUE(button.scheme.has_value());
    TEST_ASSERT_EQUAL_UINT8(device::event_scheme_device_instance, *button.scheme);
    TEST_ASSERT_TRUE(button.filter.has_value());
    TEST_ASSERT_EQUAL_UINT32(0xF4, *button.filter);
    TEST_ASSERT_TRUE(button.enabled);

    const auto& light = bus.devices[7][0];
    TEST_ASSERT_EQUAL_UINT8(device::event_scheme_device_instance, *light.scheme);
    TEST_ASSERT_EQUAL_UINT32(0x01, *light.filter);
    TEST_ASSERT_TRUE(light.enabled);

    // SET EVENT SCHEME / SET EVENT FILTER / ENABLE INSTANCE are send-twice
    // commands; the DTR loads are single-shot special commands.
    auto check_twice = [&](std::uint8_t opcode, unsigned expected) {
        unsigned count = 0;
        for (const auto& call : bus.frames) {
            if ((call.first & 0xFF) == opcode) {
                ++count;
                TEST_ASSERT_TRUE(call.second);
            }
        }
        TEST_ASSERT_EQUAL_UINT32(expected, count);
    };
    check_twice(device::inst::set_event_scheme, 2);
    check_twice(device::inst::set_event_filter, 2);
    check_twice(device::inst::enable_instance, 2);
}

void test_apply_disables_unbound_instances() {
    InventoryBus bus;
    bus.devices[3] = {SimInstance::of(device::type::push_button), SimInstance::of(device::type::occupancy)};
    DeviceInventory inventory{bus};
    TEST_ASSERT_EQUAL(TransportStatus::Ok, inventory.scan());
    BoundList bound{{std::make_pair(3, 0)}};
    TEST_ASSERT_EQUAL(TransportStatus::Ok, inventory.apply(BoundList::test, &bound));

    TEST_ASSERT_TRUE(bus.devices[3][0].enabled);
    TEST_ASSERT_EQUAL_UINT32(0xF4, *bus.devices[3][0].filter);
    TEST_ASSERT_FALSE(bus.devices[3][1].enabled);
    // Disabled instances still get the scheme set so their events, if the
    // device misbehaves, stay decodable.
    TEST_ASSERT_EQUAL_UINT8(device::event_scheme_device_instance, *bus.devices[3][1].scheme);
}

void test_scan_absent_devices_stay_absent() {
    InventoryBus bus;
    DeviceInventory inventory{bus};
    TEST_ASSERT_EQUAL(TransportStatus::Ok, inventory.scan());
    TEST_ASSERT_EQUAL_UINT32(0, inventory.present().count());
}

}

void run_device_inventory_tests() {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_scan_finds_devices_and_instances);
    RUN_TEST(test_apply_configures_scheme_filter_and_enable);
    RUN_TEST(test_apply_disables_unbound_instances);
    RUN_TEST(test_scan_absent_devices_stay_absent);
}
