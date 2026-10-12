#include <optional>

#include <unity.h>

#include "Fakes.hpp"
#include "control/Command.hpp"
#include "dali/ColourControl.hpp"
#include "dali/Scaling.hpp"
#include "lighting/LightingService.hpp"
#include "lighting/RelativeDimmer.hpp"

namespace {

using gateway::control::Command;
using gateway::dali::Address;
using gateway::dali::ArcLevel;
using gateway::dali::ColourCapabilities;
using gateway::dali::ColourControl;
using gateway::dali::ColourStep;
using gateway::dali::Dtr;
using gateway::dali::Group;
using gateway::dali::Mirek;
using gateway::dali::Scene;
using gateway::dali::ShortAddress;
using gateway::dali::TransportStatus;
using gateway::dali::XyCoordinate;
using gateway::lighting::ColourKind;
using gateway::lighting::ControlError;
using gateway::lighting::DimDirection;
using gateway::lighting::LightingService;
using gateway::lighting::RelativeDimmer;
using gateway::lighting::StateSource;
using gateway::testing::FakeDaliTransport;
using gateway::testing::MemoryLevelStore;
using gateway::testing::TransportOp;

ShortAddress channel(unsigned value) {
    return *ShortAddress::from(value);
}

Group group(unsigned value) {
    return *Group::from(value);
}

Address channel_address(unsigned value) {
    return Address::short_address(channel(value));
}

Address group_address(unsigned value) {
    return Address::group(group(value));
}

ArcLevel level(unsigned value) {
    return *ArcLevel::from(value);
}

using Kind = TransportOp::Kind;

TransportOp dtr(Dtr which, std::uint8_t value) {
    return {Kind::Dtr, Address::broadcast(), value, static_cast<std::uint8_t>(which)};
}

TransportOp enable() {
    return {Kind::EnableDeviceType, Address::broadcast(), 8};
}

TransportOp command_op(Address address, std::uint8_t opcode, bool twice = false) {
    return {Kind::Command, address, opcode, 0, twice};
}

TransportOp query_op(unsigned short_address, std::uint8_t opcode) {
    return {Kind::Query, channel_address(short_address), opcode};
}

bool ops_equal(const FakeDaliTransport& bus, std::initializer_list<TransportOp> expected) {
    TEST_ASSERT_EQUAL_UINT32(expected.size(), bus.ops.size());
    std::size_t index = 0;
    for (const auto& op : expected) {
        const auto& actual = bus.ops[index++];
        TEST_ASSERT_EQUAL(static_cast<int>(op.kind), static_cast<int>(actual.kind));
        TEST_ASSERT_TRUE(op.address == actual.address);
        TEST_ASSERT_EQUAL_UINT8(op.opcode, actual.opcode);
        TEST_ASSERT_EQUAL_UINT8(op.value, actual.value);
        TEST_ASSERT_EQUAL(op.flag, actual.flag);
    }
    return true;
}

// --- mirek / kelvin conversion ----------------------------------------------

void test_mirek_kelvin_roundtrip_and_bounds() {
    TEST_ASSERT_FALSE(gateway::dali::Mirek::from(0).has_value());
    TEST_ASSERT_FALSE(gateway::dali::Mirek::from(65535).has_value());
    TEST_ASSERT_TRUE(gateway::dali::Mirek::from(1).has_value());
    TEST_ASSERT_TRUE(gateway::dali::Mirek::from(65534).has_value());

    TEST_ASSERT_FALSE(gateway::dali::mirek_from_kelvin(0).has_value());
    // 1'000'000/2700 = 370.37 -> 370; 1'000'000/6500 = 153.85 -> 154
    TEST_ASSERT_EQUAL_UINT16(370, gateway::dali::mirek_from_kelvin(2700)->value());
    TEST_ASSERT_EQUAL_UINT16(154, gateway::dali::mirek_from_kelvin(6500)->value());
    TEST_ASSERT_EQUAL_UINT16(1, gateway::dali::mirek_from_kelvin(1'000'000)->value());
    TEST_ASSERT_EQUAL_UINT32(2703, gateway::dali::kelvin_from_mirek(*Mirek::from(370)));
    TEST_ASSERT_EQUAL_UINT32(6494, gateway::dali::kelvin_from_mirek(*Mirek::from(154)));
}

// --- ColourControl frame sequences ------------------------------------------

void test_set_colour_temperature_sequence() {
    FakeDaliTransport bus;
    ColourControl colour{bus};
    // 2700 K = 370 mirek = 0x0172: DTR0=LSB, DTR1=MSB.
    const auto status = colour.set_colour_temperature(channel_address(5), *Mirek::from(370));
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Ok), static_cast<int>(status));
    ops_equal(bus, {
                       dtr(Dtr::Dtr0, 0x72), dtr(Dtr::Dtr1, 0x01), enable(),
                       command_op(channel_address(5), 0xE7),            // SET TEMPORARY TC
                       enable(), command_op(channel_address(5), 0xE2),  // ACTIVATE
                   });
}

void test_set_xy_sequence() {
    FakeDaliTransport bus;
    ColourControl colour{bus};
    const auto status =
        colour.set_xy(channel_address(3), *XyCoordinate::from(0x3227), *XyCoordinate::from(0x3341));
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Ok), static_cast<int>(status));
    ops_equal(bus, {
                       dtr(Dtr::Dtr0, 0x27), dtr(Dtr::Dtr1, 0x32), enable(),
                       command_op(channel_address(3), 0xE0),  // SET TEMPORARY x
                       dtr(Dtr::Dtr0, 0x41), dtr(Dtr::Dtr1, 0x33), enable(),
                       command_op(channel_address(3), 0xE1),            // SET TEMPORARY y
                       enable(), command_op(channel_address(3), 0xE2),  // ACTIVATE
                   });
}

void test_set_rgbwaf_sequence() {
    FakeDaliTransport bus;
    ColourControl colour{bus};
    const auto status = colour.set_rgbwaf(group_address(0), 10, 20, 30, 40, 0xFF, 0xFF);
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Ok), static_cast<int>(status));
    ops_equal(bus, {
                       dtr(Dtr::Dtr0, 10), dtr(Dtr::Dtr1, 20), dtr(Dtr::Dtr2, 30), enable(),
                       command_op(group_address(0), 0xEB),  // SET TEMPORARY RGB DIMLEVEL
                       dtr(Dtr::Dtr0, 40), dtr(Dtr::Dtr1, 0xFF), dtr(Dtr::Dtr2, 0xFF), enable(),
                       command_op(group_address(0), 0xEC),            // SET TEMPORARY WAF DIMLEVEL
                       enable(), command_op(group_address(0), 0xE2),  // ACTIVATE
                   });
}

void test_step_and_tc_limit_sequences() {
    FakeDaliTransport bus;
    ColourControl colour{bus};

    auto status = colour.step_colour_temperature(channel_address(1), ColourStep::Cooler);
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Ok), static_cast<int>(status));
    ops_equal(bus, {enable(), command_op(channel_address(1), 0xE8)});

    bus.ops.clear();
    status = colour.step_colour_temperature(channel_address(1), ColourStep::Warmer);
    ops_equal(bus, {enable(), command_op(channel_address(1), 0xE9)});

    bus.ops.clear();
    // 250 mirek = 0x00FA, warmest limit = DTR2 selector 1; send-twice.
    status = colour.store_colour_temperature_limit(channel_address(1), gateway::dali::TcLimit::Warmest,
                                                   *Mirek::from(250));
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Ok), static_cast<int>(status));
    ops_equal(bus, {
                       dtr(Dtr::Dtr0, 0xFA),
                       dtr(Dtr::Dtr1, 0x00),
                       dtr(Dtr::Dtr2, 0x01),
                       enable(),
                       command_op(channel_address(1), 0xF2, true),
                   });
}

void test_colour_queries_decode_and_query_dtr0_for_lsb() {
    FakeDaliTransport bus;
    ColourControl colour{bus};

    // QUERY COLOUR TYPE FEATURES: bit0 xy, bit1 Tc, bits2-4 primaries,
    // bits5-7 RGBWAF channels.
    bus.next_query = {TransportStatus::Ok, std::uint8_t{0b1010'0011}};
    const auto features = colour.query_colour_type_features(channel(2));
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Ok), static_cast<int>(features.status));
    TEST_ASSERT_TRUE(features.value->xy_capable);
    TEST_ASSERT_TRUE(features.value->tc_capable);
    TEST_ASSERT_EQUAL_UINT8(0, features.value->primary_count);
    TEST_ASSERT_EQUAL_UINT8(5, features.value->rgbwaf_channels);
    ops_equal(bus, {enable(), query_op(2, 0xF9)});

    // QUERY COLOUR STATUS: bits 4/5 = xy/Tc active.
    bus.ops.clear();
    bus.next_query = {TransportStatus::Ok, std::uint8_t{0x30}};
    const auto status = colour.query_colour_status(channel(2));
    TEST_ASSERT_TRUE(status.value->xy_active);
    TEST_ASSERT_TRUE(status.value->tc_active);
    TEST_ASSERT_FALSE(status.value->rgbwaf_active);
    ops_equal(bus, {enable(), query_op(2, 0xF8)});

    // QUERY COLOUR VALUE: DTR0 = selector, reply = MSB, QUERY CONTENT DTR0
    // reads the LSB.
    bus.ops.clear();
    bus.query_replies = {
        {TransportStatus::Ok, std::uint8_t{0x01}},  // QUERY COLOUR VALUE -> MSB
        {TransportStatus::Ok, std::uint8_t{0x72}},  // QUERY CONTENT DTR0 -> LSB
    };
    const auto value = colour.query_colour_value(channel(2), 2);
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Ok), static_cast<int>(value.status));
    TEST_ASSERT_EQUAL_UINT8(0x01, *value.msb);
    TEST_ASSERT_EQUAL_UINT8(0x72, *value.lsb);
    ops_equal(bus,
              {
                  dtr(Dtr::Dtr0, 2), enable(), query_op(2, 0xFA), query_op(2, 0x98),  // QUERY CONTENT DTR0
              });
}

void test_scene_store_captures_colour_before_storing() {
    FakeDaliTransport bus;
    ColourControl colour{bus};
    const auto status = colour.store_scene(Address::broadcast(), *Scene::from(11));
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Ok), static_cast<int>(status));
    ops_equal(bus, {
                       command_op(Address::broadcast(), 0x21, true),      // STORE ACTUAL LEVEL
                       enable(), command_op(Address::broadcast(), 0xEE),  // COPY REPORT TO TEMPORARY
                       command_op(Address::broadcast(), 0x4B, true),      // STORE DTR AS SCENE 11
                   });
}

void test_set_colour_temperature_stops_on_first_dtr_failure() {
    FakeDaliTransport bus;
    bus.write_status = TransportStatus::Timeout;
    ColourControl colour{bus};
    const auto status = colour.set_colour_temperature(channel_address(5), *Mirek::from(300));
    TEST_ASSERT_EQUAL(static_cast<int>(TransportStatus::Timeout), static_cast<int>(status));
    // The DTR pre-loads both ran (their statuses combine); no EDT8, no
    // extended command.
    ops_equal(bus, {dtr(Dtr::Dtr0, 0x2C), dtr(Dtr::Dtr1, 0x01)});
    TEST_ASSERT_EQUAL_UINT32(0, bus.device_type_enables.size());
}

// --- LightingService colour behaviour ----------------------------------------

void test_set_colour_temperature_state_and_readback() {
    FakeDaliTransport bus;
    MemoryLevelStore store;
    LightingService service{bus, store};
    service.set_capabilities(channel(5), ColourCapabilities{.tc_capable = true});

    // refresh_colour after the command: colour status says Tc active, the
    // colour-value query answers 370 mirek (MSB first, LSB via DTR0).
    bus.query_replies = {
        {TransportStatus::Ok, std::uint8_t{0x20}},  // QUERY COLOUR STATUS
        {TransportStatus::Ok, std::uint8_t{0x01}},  // QUERY COLOUR VALUE -> MSB
        {TransportStatus::Ok, std::uint8_t{0x72}},  // QUERY CONTENT DTR0 -> LSB
    };
    const auto result = service.set_colour_temperature(channel_address(5), 2700);
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.error));
    TEST_ASSERT_TRUE(result.changed_colour_channels.test(5));

    const auto colour = service.colour_state(channel(5));
    TEST_ASSERT_EQUAL(static_cast<int>(ColourKind::Temperature), static_cast<int>(colour.kind));
    TEST_ASSERT_EQUAL(static_cast<int>(StateSource::Readback), static_cast<int>(colour.source));
    TEST_ASSERT_EQUAL_UINT16(2703, colour.temperature_kelvin);  // 370 mirek read back

    const auto report = service.colour_report(channel_address(5));
    TEST_ASSERT_TRUE(report.has_value());
    TEST_ASSERT_EQUAL(static_cast<int>(ColourKind::Temperature), static_cast<int>(report->kind));
    TEST_ASSERT_EQUAL_UINT16(2703, report->kelvin);
}

void test_colour_command_to_unsupported_gear_sends_nothing() {
    FakeDaliTransport bus;
    MemoryLevelStore store;
    LightingService service{bus, store};
    // Tc-only gear: an RGB write must be rejected without touching the bus.
    service.set_capabilities(channel(0), ColourCapabilities{.tc_capable = true});

    const auto result = service.set_rgb(channel_address(0), 10, 20, 30);
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::Unsupported), static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_UINT32(0, bus.ops.size());
    TEST_ASSERT_FALSE(result.changed_colour_channels.any());
    TEST_ASSERT_EQUAL(static_cast<int>(ColourKind::None),
                      static_cast<int>(service.colour_state(channel(0)).kind));

    // A gear probed as not-DT8 at all rejects Tc too.
    service.set_capabilities(channel(1), ColourCapabilities{});
    const auto tc = service.set_colour_temperature(channel_address(1), 4000);
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::Unsupported), static_cast<int>(tc.error));
    TEST_ASSERT_EQUAL_UINT32(0, bus.ops.size());
}

void test_colour_command_with_unknown_capabilities_sends() {
    FakeDaliTransport bus;
    MemoryLevelStore store;
    LightingService service{bus, store};
    // Never probed: no entry -> send and do not attempt a readback.
    const auto result = service.set_colour_temperature(channel_address(7), 5000);
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_UINT32(6, bus.ops.size());  // DTR0, DTR1, EDT+cmd, EDT+activate
    TEST_ASSERT_EQUAL_UINT32(0, bus.queries.size());
    const auto colour = service.colour_state(channel(7));
    TEST_ASSERT_EQUAL(static_cast<int>(StateSource::Commanded), static_cast<int>(colour.source));
    TEST_ASSERT_EQUAL_UINT16(5000, colour.temperature_kelvin);
}

void test_set_rgb_scales_channels_and_drives_brightness() {
    FakeDaliTransport bus;
    MemoryLevelStore store;
    LightingService service{bus, store};
    service.set_capabilities(channel(1), ColourCapabilities{.rgbwaf_channels = 4});
    // Readback after the command: RGBWAF active, dimlevels 254/127/64.
    bus.query_replies = {
        {TransportStatus::Ok, std::uint8_t{0x80}},  // RGBWAF active
        {TransportStatus::Ok, std::uint8_t{254}},   // red dimlevel
        {TransportStatus::Ok, std::uint8_t{0}},     // QUERY CONTENT DTR0
        {TransportStatus::Ok, std::uint8_t{127}},   // green dimlevel
        {TransportStatus::Ok, std::uint8_t{0}},    {TransportStatus::Ok, std::uint8_t{64}},  // blue dimlevel
        {TransportStatus::Ok, std::uint8_t{0}},    {TransportStatus::Ok, std::uint8_t{0}},   // white dimlevel
        {TransportStatus::Ok, std::uint8_t{0}},
    };
    const auto result = service.set_rgb(channel_address(1), 255, 128, 64);
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.error));

    // round(v*254/255): 255 -> 254, 128 -> 127, 64 -> 64; white/amber/free
    // stay 0xFF "no change"; the arc goes to the brightest component.
    ops_equal(bus, {
                       dtr(Dtr::Dtr0, 254),
                       dtr(Dtr::Dtr1, 127),
                       dtr(Dtr::Dtr2, 64),
                       enable(),
                       command_op(channel_address(1), 0xEB),
                       dtr(Dtr::Dtr0, 0xFF),
                       dtr(Dtr::Dtr1, 0xFF),
                       dtr(Dtr::Dtr2, 0xFF),
                       enable(),
                       command_op(channel_address(1), 0xEC),
                       enable(),
                       command_op(channel_address(1), 0xE2),
                       {Kind::Level, channel_address(1), 0, 254},
                       enable(),
                       query_op(1, 0xF8),  // QUERY COLOUR STATUS
                       dtr(Dtr::Dtr0, 9),
                       enable(),
                       query_op(1, 0xFA),
                       query_op(1, 0x98),
                       dtr(Dtr::Dtr0, 10),
                       enable(),
                       query_op(1, 0xFA),
                       query_op(1, 0x98),
                       dtr(Dtr::Dtr0, 11),
                       enable(),
                       query_op(1, 0xFA),
                       query_op(1, 0x98),
                       dtr(Dtr::Dtr0, 12),
                       enable(),
                       query_op(1, 0xFA),
                       query_op(1, 0x98),
                   });

    const auto report = service.colour_report(channel_address(1));
    TEST_ASSERT_TRUE(report.has_value());
    TEST_ASSERT_EQUAL(static_cast<int>(ColourKind::Rgbw), static_cast<int>(report->kind));
    // Readback: dimlevel 254 -> scaling 255, 127 -> 128, 64 -> 64, 0 -> 0.
    TEST_ASSERT_EQUAL_UINT8(255, report->r);
    TEST_ASSERT_EQUAL_UINT8(128, report->g);
    TEST_ASSERT_EQUAL_UINT8(64, report->b);
    TEST_ASSERT_EQUAL_UINT8(0, report->w);
}

void test_set_rgbw_honours_validity_mask() {
    FakeDaliTransport bus;
    MemoryLevelStore store;
    LightingService service{bus, store};
    // Only green and white valid (bits 2 and 0): invalid channels are sent as
    // 0xFF "no change" and the brightness uses the valid components only.
    const auto result = service.set_rgbw(group_address(0), 100, 200, 50, 128, 0b0101);
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.error));

    const auto& dtrs = bus.dtr_writes;
    TEST_ASSERT_EQUAL_UINT32(6, dtrs.size());
    TEST_ASSERT_EQUAL_UINT8(0xFF, dtrs[0].value);  // red = no change
    TEST_ASSERT_EQUAL_UINT8(199, dtrs[1].value);   // 200 -> 199
    TEST_ASSERT_EQUAL_UINT8(0xFF, dtrs[2].value);  // blue = no change
    TEST_ASSERT_EQUAL_UINT8(127, dtrs[3].value);   // white 128 -> 127
    // Brightness = max(200, 128) = 200 -> arc.
    const auto& writes = bus.writes;
    TEST_ASSERT_EQUAL_UINT32(1, writes.size());
    TEST_ASSERT_EQUAL_UINT8(gateway::dali::arc_from_scaling(200).value(), writes[0].level.value());

    const auto colour = service.colour_state(group(0));
    TEST_ASSERT_EQUAL_UINT8(0b0101, colour.rgbw_validity);
    TEST_ASSERT_EQUAL_UINT8(0, colour.r);  // invalid channels are not remembered
    TEST_ASSERT_EQUAL_UINT8(200, colour.g);
    TEST_ASSERT_EQUAL_UINT8(128, colour.w);
}

void test_set_xy_with_and_without_brightness() {
    FakeDaliTransport bus;
    MemoryLevelStore store;
    LightingService service{bus, store};

    // xy + brightness: colour frames plus one DAPC.
    auto result = service.set_xy(channel_address(2), 0x3227, 0x3341, true, 200, true);
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_UINT32(1, bus.writes.size());
    TEST_ASSERT_TRUE(result.changed_colour_channels.test(2));
    TEST_ASSERT_EQUAL_UINT16(0x3341, service.colour_state(channel(2)).y);

    // Brightness-only write: no colour frames at all.
    bus.ops.clear();
    bus.writes.clear();
    bus.device_type_enables.clear();
    result = service.set_xy(channel_address(2), 0x4000, 0x4000, false, 100, true);
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_UINT32(1, bus.writes.size());
    TEST_ASSERT_EQUAL_UINT32(0, bus.device_type_enables.size());
    // The level moved under an active xyY state: the xyY status embeds
    // brightness, so it republishes.
    TEST_ASSERT_TRUE(result.changed_colour_channels.test(2));
    TEST_ASSERT_EQUAL_UINT16(0x3341, service.colour_state(channel(2)).y);  // untouched

    // xy-only write: no level write.
    bus.ops.clear();
    bus.writes.clear();
    bus.device_type_enables.clear();
    result = service.set_xy(channel_address(2), 0x4000, 0x4000, true, 0, false);
    TEST_ASSERT_EQUAL_UINT32(0, bus.writes.size());
    TEST_ASSERT_EQUAL_UINT16(0x4000, service.colour_state(channel(2)).x);
}

void test_colour_step_maps_up_to_cooler_and_down_to_warmer() {
    FakeDaliTransport bus;
    MemoryLevelStore store;
    LightingService service{bus, store};
    service.set_capabilities(channel(0), ColourCapabilities{.tc_capable = true});

    auto result = service.colour_temperature_step(channel_address(0), DimDirection::Up);
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.error));
    ops_equal(bus, {enable(), command_op(channel_address(0), 0xE8)});

    bus.ops.clear();
    result = service.colour_temperature_step(channel_address(0), DimDirection::Down);
    ops_equal(bus, {enable(), command_op(channel_address(0), 0xE9)});

    service.set_capabilities(channel(1), ColourCapabilities{});
    const auto rejected = service.colour_temperature_step(channel_address(1), DimDirection::Up);
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::Unsupported), static_cast<int>(rejected.error));
}

void test_finish_colour_temperature_reads_back_members() {
    FakeDaliTransport bus;
    MemoryLevelStore store;
    LightingService service{bus, store};
    gateway::config::Configuration config{};
    config.group_masks[0] = 0b0001;
    config.group_masks[1] = 0b0001;
    service.configure(config);
    service.set_capabilities(channel(0), ColourCapabilities{.tc_capable = true});
    service.set_capabilities(channel(1), ColourCapabilities{.tc_capable = true});

    // Each member's readback: colour status + colour value (2 queries + DTR0).
    bus.query_replies = {
        {TransportStatus::Ok, std::uint8_t{0x20}}, {TransportStatus::Ok, std::uint8_t{0x01}},
        {TransportStatus::Ok, std::uint8_t{0x72}}, {TransportStatus::Ok, std::uint8_t{0x20}},
        {TransportStatus::Ok, std::uint8_t{0x01}}, {TransportStatus::Ok, std::uint8_t{0x72}},
    };
    const auto result = service.finish_colour_temperature(group_address(0));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.error));
    TEST_ASSERT_TRUE(result.changed_colour_channels.test(0));
    TEST_ASSERT_TRUE(result.changed_colour_channels.test(1));
    TEST_ASSERT_TRUE(result.changed_colour_groups.test(0));
    // Group colour state was derived from a member.
    TEST_ASSERT_EQUAL_UINT16(2703, service.colour_state(group(0)).temperature_kelvin);
}

void test_group_colour_command_fans_out_to_members() {
    FakeDaliTransport bus;
    MemoryLevelStore store;
    LightingService service{bus, store};
    gateway::config::Configuration config{};
    config.group_masks[0] = 0b0001;
    config.group_masks[1] = 0b0001;
    config.group_masks[2] = 0b0010;  // member of group 1, not group 0
    service.configure(config);

    const auto result = service.set_colour_temperature(group_address(0), 4000);
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.error));
    TEST_ASSERT_TRUE(result.changed_colour_channels.test(0));
    TEST_ASSERT_TRUE(result.changed_colour_channels.test(1));
    TEST_ASSERT_FALSE(result.changed_colour_channels.test(2));
    TEST_ASSERT_TRUE(result.changed_colour_groups.test(0));
    TEST_ASSERT_EQUAL_UINT16(4000, service.colour_state(channel(0)).temperature_kelvin);
    TEST_ASSERT_EQUAL_UINT16(4000, service.colour_state(channel(1)).temperature_kelvin);
    TEST_ASSERT_EQUAL_UINT16(4000, service.colour_state(group(0)).temperature_kelvin);
}

void test_refresh_capabilities_records_probe_result() {
    FakeDaliTransport bus;
    MemoryLevelStore store;
    LightingService service{bus, store};

    bus.next_query = {TransportStatus::Ok, std::uint8_t{0x82}};  // Tc + 4 RGBWAF channels
    auto result = service.refresh_capabilities(channel(3));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.error));
    const auto caps = service.capabilities(channel(3));
    TEST_ASSERT_TRUE(caps.has_value());
    TEST_ASSERT_TRUE(caps->tc_capable);
    TEST_ASSERT_EQUAL_UINT8(4, caps->rgbwaf_channels);
    TEST_ASSERT_FALSE(caps->xy_capable);

    // A non-DT8 gear does not answer: recorded as "no capability".
    bus.next_query = {TransportStatus::Timeout, std::nullopt};
    result = service.refresh_capabilities(channel(4));
    const auto none = service.capabilities(channel(4));
    TEST_ASSERT_TRUE(none.has_value());
    TEST_ASSERT_FALSE(none->tc_capable || none->xy_capable || none->rgbwaf_channels > 0);
}

void test_colour_report_scales_rgb_with_brightness() {
    FakeDaliTransport bus;
    MemoryLevelStore store;
    LightingService service{bus, store};

    auto result = service.set_rgb(channel_address(9), 255, 128, 0);  // colour_scaling = 255
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.error));
    // Halve the lamp brightness; the report scales the channels accordingly.
    result = service.set_level(channel_address(9), level(127));
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(result.error));
    const auto report = service.colour_report(channel_address(9));
    TEST_ASSERT_TRUE(report.has_value());
    const auto current = gateway::dali::scaling_from_arc(level(127));
    TEST_ASSERT_EQUAL_UINT8((255 * current + 127) / 255, report->r);
    TEST_ASSERT_EQUAL_UINT8((128 * current + 127) / 255, report->g);
    TEST_ASSERT_EQUAL_UINT8(0, report->b);
}

void test_execute_routes_colour_commands() {
    FakeDaliTransport bus;
    MemoryLevelStore store;
    LightingService service{bus, store};
    RelativeDimmer dimmer;
    RelativeDimmer colour_dimmer;
    // Commissioning needs a service&; reuse the same one.
    gateway::commissioning::Commissioning commissioning{bus, service};
    gateway::inputs::InputMapper inputs;

    const auto set =
        gateway::control::execute(service, dimmer, colour_dimmer, commissioning, inputs, 0,
                                  Command{gateway::control::SetColourTemperature{channel_address(0), 3000}});
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(set.result.error));
    TEST_ASSERT_EQUAL_UINT32(6, bus.ops.size());

    TEST_ASSERT_FALSE(colour_dimmer.running(channel_address(0)));
    auto outcome = gateway::control::execute(
        service, dimmer, colour_dimmer, commissioning, inputs, 0,
        Command{gateway::control::ColourDimStart{channel_address(0), DimDirection::Up}});
    TEST_ASSERT_EQUAL(static_cast<int>(ControlError::None), static_cast<int>(outcome.result.error));
    TEST_ASSERT_TRUE(colour_dimmer.running(channel_address(0)));
    TEST_ASSERT_FALSE(dimmer.running(channel_address(0)));

    // A stop ends the dim and triggers a readback (capabilities unknown here,
    // so finish_colour_temperature has nothing to refresh).
    outcome = gateway::control::execute(service, dimmer, colour_dimmer, commissioning, inputs, 100,
                                        Command{gateway::control::ColourDimStop{channel_address(0)}});
    TEST_ASSERT_FALSE(colour_dimmer.running(channel_address(0)));
}

}

void run_colour_tests() {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_mirek_kelvin_roundtrip_and_bounds);
    RUN_TEST(test_set_colour_temperature_sequence);
    RUN_TEST(test_set_xy_sequence);
    RUN_TEST(test_set_rgbwaf_sequence);
    RUN_TEST(test_step_and_tc_limit_sequences);
    RUN_TEST(test_colour_queries_decode_and_query_dtr0_for_lsb);
    RUN_TEST(test_scene_store_captures_colour_before_storing);
    RUN_TEST(test_set_colour_temperature_stops_on_first_dtr_failure);
    RUN_TEST(test_set_colour_temperature_state_and_readback);
    RUN_TEST(test_colour_command_to_unsupported_gear_sends_nothing);
    RUN_TEST(test_colour_command_with_unknown_capabilities_sends);
    RUN_TEST(test_set_rgb_scales_channels_and_drives_brightness);
    RUN_TEST(test_set_rgbw_honours_validity_mask);
    RUN_TEST(test_set_xy_with_and_without_brightness);
    RUN_TEST(test_colour_step_maps_up_to_cooler_and_down_to_warmer);
    RUN_TEST(test_finish_colour_temperature_reads_back_members);
    RUN_TEST(test_group_colour_command_fans_out_to_members);
    RUN_TEST(test_refresh_capabilities_records_probe_result);
    RUN_TEST(test_colour_report_scales_rgb_with_brightness);
    RUN_TEST(test_execute_routes_colour_commands);
}
