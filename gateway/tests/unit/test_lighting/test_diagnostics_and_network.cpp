#include <cstring>
#include <vector>

#include <unity.h>

#include "dali/TracingTransport.hpp"
#include "device/DaliPsuMonitor.hpp"
#include "device/OledDisplay.hpp"
#include "diagnostics/DaliDebugTrace.hpp"
#include "diagnostics/DiagnosticLog.hpp"
#include "diagnostics/ErrorCode.hpp"
#include "lighting/LampFinder.hpp"
#include "lighting/LightingService.hpp"
#include "network/ApiCodec.hpp"
#include "network/HaDiscovery.hpp"
#include "network/NetworkConfig.hpp"
#include "unit/test_lighting/Fakes.hpp"

namespace {

using namespace gateway;

class MemoryLogBackend final : public diagnostics::LogBackend {
public:
    std::vector<std::uint8_t> blob;
    unsigned saves = 0;

    bool load(void* data, std::size_t size) noexcept override {
        if (blob.size() != size) {
            return false;
        }
        std::memcpy(data, blob.data(), size);
        return true;
    }

    bool save(const void* data, std::size_t size) noexcept override {
        blob.resize(size);
        std::memcpy(blob.data(), data, size);
        ++saves;
        return true;
    }
};

void test_error_catalog_has_descriptive_entries() {
    std::size_t count = 0;
    const auto* items = diagnostics::error_catalog(count);
    TEST_ASSERT_NOT_NULL(items);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(25, count);
    const auto& lamp = diagnostics::describe_error(diagnostics::ErrorCode::DaliLampFailure);
    TEST_ASSERT_EQUAL_STRING("DALI_LAMP_FAILURE", lamp.symbol);
    TEST_ASSERT_TRUE(std::strlen(lamp.description) > 15);
    TEST_ASSERT_TRUE(std::strlen(lamp.remedy) > 15);
}

void test_diagnostic_log_persists_coalesces_and_backfills_epoch() {
    MemoryLogBackend backend;
    diagnostics::DiagnosticLog log{&backend};

    log.record(diagnostics::LogSource::Dali, diagnostics::LogSeverity::Error,
               diagnostics::ErrorCode::DaliLampFailure, 7, "A07 lamp open", 1'000);
    TEST_ASSERT_EQUAL_UINT32(1, log.size());
    TEST_ASSERT_EQUAL_UINT32(1, backend.saves);
    TEST_ASSERT_EQUAL_UINT64(0, log.latest()->epoch_ms);

    // Identical error within 30 s coalesces into the same entry and increments count.
    log.record(diagnostics::LogSource::Dali, diagnostics::LogSeverity::Error,
               diagnostics::ErrorCode::DaliLampFailure, 7, "A07 lamp open retry", 5'000);
    TEST_ASSERT_EQUAL_UINT32(1, log.size());
    TEST_ASSERT_EQUAL_UINT16(2, log.latest()->count);

    // Syncing wall clock backfills epoch_ms on earlier unsynced boot entries.
    log.sync_wall_clock(1'728'000'010'000ULL, 10'000);
    TEST_ASSERT_TRUE(log.clock_synced());
    TEST_ASSERT_EQUAL_UINT64(1'728'000'005'000ULL, log.latest()->epoch_ms);
    (void)log.flush_to_storage();

    // Reload into a fresh instance to verify power-loss persistence.
    diagnostics::DiagnosticLog restored{&backend};
    TEST_ASSERT_TRUE(restored.load_from_storage());
    TEST_ASSERT_EQUAL_UINT32(1, restored.size());
    TEST_ASSERT_EQUAL_UINT16(2, restored.latest()->count);
    TEST_ASSERT_EQUAL_UINT16(7, restored.latest()->address);
    TEST_ASSERT_EQUAL(static_cast<int>(diagnostics::ErrorCode::DaliLampFailure),
                      static_cast<int>(restored.latest()->code));
}

void test_diagnostic_log_ring_buffer_wraps_cleanly() {
    diagnostics::DiagnosticLog log;
    for (unsigned i = 0; i < 80; ++i) {
        log.record(diagnostics::LogSource::Knx, diagnostics::LogSeverity::Warning,
                   diagnostics::ErrorCode::KnxCommandQueueFull, static_cast<std::uint16_t>(i), "burst",
                   i * 100);
    }
    TEST_ASSERT_EQUAL_UINT32(diagnostics::DiagnosticLog::capacity, log.size());
    TEST_ASSERT_EQUAL_UINT16(16, log.entry_oldest_first(0).address);
    TEST_ASSERT_EQUAL_UINT16(79, log.latest()->address);
}

void test_dali_debug_trace_decodes_frames_and_auto_triggers() {
    diagnostics::DaliDebugTrace trace;
    trace.stop_session();
    trace.set_auto_trigger_from_dali(true);
    TEST_ASSERT_FALSE(trace.active());

    // Normal OK command while stopped does not trigger capture.
    trace.record(diagnostics::TraceKind::Command16, 0xFE80, 16, false, dali::TransportStatus::Ok, false, 0, 0,
                 100);
    TEST_ASSERT_FALSE(trace.active());

    // Unsolicited 24-bit DALI-2 input event automatically starts the debug session!
    trace.record(diagnostics::TraceKind::UnsolicitedEvent24, 0x060155, 24, false, dali::TransportStatus::Ok,
                 false, 0, 1'728'000'000'000ULL, 250);
    TEST_ASSERT_TRUE(trace.active());
    TEST_ASSERT_EQUAL_UINT32(1, trace.size());

    diagnostics::DecodedTrace dec{};
    diagnostics::decode_trace_entry(trace.entry_oldest_first(0), dec);
    TEST_ASSERT_EQUAL_STRING("DEV03/I01", dec.target.data());
    TEST_ASSERT_EQUAL_STRING("PART103_EVENT", dec.mnemonic.data());

    // Record a QUERY_STATUS with lamp failure bit set (0x02).
    trace.record(diagnostics::TraceKind::Query16, 0x0F90, 16, false, dali::TransportStatus::Ok, true, 0x06,
                 1'728'000'000'100ULL, 350);
    diagnostics::decode_trace_entry(trace.entry_oldest_first(1), dec);
    TEST_ASSERT_EQUAL_STRING("A07", dec.target.data());
    TEST_ASSERT_EQUAL_STRING("QUERY_STATUS", dec.mnemonic.data());
    TEST_ASSERT_NOT_NULL(std::strstr(dec.detail.data(), "lampFail=1"));
}

void test_tracing_transport_logs_collisions_and_gear_faults() {
    testing::FakeDaliTransport raw;
    diagnostics::DaliDebugTrace trace;
    diagnostics::DiagnosticLog log;
    dali::TracingTransport traced{raw, trace, log, []() -> std::uint32_t { return 4200; }};

    raw.next_status = {dali::TransportStatus::Ok, std::uint8_t{0x03}};  // gear + lamp fault
    const auto st = traced.query_status(*dali::ShortAddress::from(3));
    TEST_ASSERT_EQUAL(static_cast<int>(dali::TransportStatus::Ok), static_cast<int>(st.status));
    TEST_ASSERT_EQUAL_UINT32(2, log.size());
    TEST_ASSERT_EQUAL(static_cast<int>(diagnostics::ErrorCode::DaliGearFailure),
                      static_cast<int>(log.latest()->code));
    TEST_ASSERT_EQUAL_UINT32(1, trace.size());
}

void test_oled_display_renders_both_128x64_and_128x32_geometries() {
    device::OledSystemState state{};
    state.knx_individual_address = 0x1114;  // 1.1.20
    state.present_gear_count = 12;
    state.active_lamp_count = 8;
    state.average_brightness_pct = 75;

    device::OledDisplay disp64{device::OledGeometry::Oled128x64};
    TEST_ASSERT_EQUAL_UINT8(64, disp64.height());
    TEST_ASSERT_EQUAL_UINT32(1024, disp64.framebuffer_bytes());
    disp64.render(state, 100);
    // Header bar at (5, 5) is filled on 128x64
    TEST_ASSERT_TRUE(disp64.pixel(1, 0));

    device::OledDisplay disp32{device::OledGeometry::Oled128x32};
    TEST_ASSERT_EQUAL_UINT8(32, disp32.height());
    TEST_ASSERT_EQUAL_UINT32(512, disp32.framebuffer_bytes());
    for (unsigned p = 0; p < device::oled_page_count; ++p) {
        disp32.render(state, 200 + p * 10);
        disp32.next_page(200 + p * 10);
    }
    TEST_ASSERT_EQUAL(static_cast<int>(device::OledPage::Overview), static_cast<int>(disp32.page()));
}

void test_ha_discovery_and_mqtt_command_codec() {
    char topic[128]{};
    char payload[640]{};
    const auto ch = *dali::ShortAddress::from(5);
    dali::ColourCapabilities caps{};
    caps.tc_capable = true;
    caps.rgbwaf_channels = 3;

    TEST_ASSERT_GREATER_THAN_UINT32(
        0, network::format_ha_channel_discovery_topic("homeassistant", "gw01", ch, topic, sizeof(topic)));
    TEST_ASSERT_EQUAL_STRING("homeassistant/light/gw01_ch05/config", topic);

    TEST_ASSERT_GREATER_THAN_UINT32(0, network::format_ha_channel_discovery_payload(
                                           "knx_dali", "gw01", ch, caps, payload, sizeof(payload)));
    TEST_ASSERT_NOT_NULL(std::strstr(payload, "\"rgb\",\"color_temp\",\"brightness\""));

    const char* msg = "{\"state\":\"ON\",\"brightness\":190}";
    const auto cmd =
        network::parse_mqtt_command("knx_dali", "knx_dali/light/ch05/set", msg, std::strlen(msg));
    TEST_ASSERT_TRUE(cmd.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<control::SetLevel>(*cmd));
    const auto& set_lvl = std::get<control::SetLevel>(*cmd);
    TEST_ASSERT_EQUAL_UINT8(5, set_lvl.address.as_short()->value());
    TEST_ASSERT_EQUAL_UINT8(190, set_lvl.level.value());
}

void test_api_codec_and_reconnect_backoff() {
    const char* json =
        "{\"action\":\"configure_gear\",\"index\":4,\"min_level\":15,\"max_level\":240,\"fade_time\":3}";
    const auto cmd = network::parse_api_command_json(json, std::strlen(json));
    TEST_ASSERT_TRUE(cmd.has_value());
    TEST_ASSERT_TRUE(std::holds_alternative<control::ConfigureGear>(*cmd));
    const auto& cfg_cmd = std::get<control::ConfigureGear>(*cmd);
    TEST_ASSERT_EQUAL_UINT8(4, cfg_cmd.address.value());
    TEST_ASSERT_EQUAL_UINT8(15, cfg_cmd.config.min_level);
    TEST_ASSERT_EQUAL_UINT8(240, cfg_cmd.config.max_level);
    TEST_ASSERT_EQUAL_UINT8(3, cfg_cmd.config.fade_time);

    network::ReconnectBackoff backoff;
    backoff.on_disconnected(1000);
    TEST_ASSERT_FALSE(backoff.should_attempt(1500));
    TEST_ASSERT_TRUE(backoff.should_attempt(2000));
    backoff.mark_attempt(2000);
    TEST_ASSERT_EQUAL_UINT32(2000, backoff.current_delay_ms());
}

void test_lighting_service_queries_and_configures_gear_details() {
    testing::FakeDaliTransport bus;
    testing::MemoryLevelStore store;
    lighting::LightingService service{bus, store};
    const auto ch = *dali::ShortAddress::from(2);
    bus.next_status = {dali::TransportStatus::Ok, std::uint8_t{0x04}};  // arc on

    lighting::GearConfig cfg{};
    cfg.min_level = 20;
    cfg.max_level = 250;
    cfg.fade_time = 4;
    const auto res = service.configure_gear(ch, cfg);
    TEST_ASSERT_EQUAL(static_cast<int>(lighting::ControlError::None), static_cast<int>(res.error));
    TEST_ASSERT_EQUAL_UINT8(20, service.gear_details(ch).min_level);
    TEST_ASSERT_EQUAL_UINT8(250, service.gear_details(ch).max_level);
    TEST_ASSERT_EQUAL_UINT8(4, service.gear_details(ch).fade_time);
}

void test_dali_psu_monitor_latches_off_on_short_until_explicit_reset() {
    device::DaliPsuMonitor mon;
    // Normal idle high bus: REM3 stays ON (supply_off_gpio = false).
    auto s = mon.sample(true, 100);
    TEST_ASSERT_FALSE(s.supply_off_gpio);
    TEST_ASSERT_EQUAL(static_cast<int>(device::DaliState::Ok), static_cast<int>(s.dali_led_state));

    // Short starts at t=200 ms; brief 40 ms dip (like a frame) does not trip.
    s = mon.sample(false, 200);
    TEST_ASSERT_FALSE(s.supply_off_gpio);
    s = mon.sample(false, 240);
    TEST_ASSERT_FALSE(s.supply_off_gpio);

    // At t=280 ms (80 ms continuous low), REM3 CTRL latches OFF immediately!
    s = mon.sample(false, 280);
    TEST_ASSERT_TRUE(s.supply_off_gpio);
    TEST_ASSERT_TRUE(s.just_tripped_short);
    TEST_ASSERT_EQUAL(static_cast<int>(device::DaliState::Short), static_cast<int>(s.dali_led_state));
    TEST_ASSERT_EQUAL_UINT32(1, mon.short_trip_count());

    // Even minutes later, it stays latched OFF and never pulses/bashes the PSU.
    s = mon.sample(false, 60'000);
    TEST_ASSERT_TRUE(s.supply_off_gpio);
    TEST_ASSERT_EQUAL(static_cast<int>(device::PsuProtectionState::ShortTripped), static_cast<int>(s.state));

    // Explicit reset_trip() re-arms the PSU once the user clears the short.
    mon.reset_trip();
    s = mon.sample(true, 60'100);
    TEST_ASSERT_FALSE(s.supply_off_gpio);
    TEST_ASSERT_EQUAL(static_cast<int>(device::DaliState::Ok), static_cast<int>(s.dali_led_state));
}

void test_lamp_finder_blinks_pulses_and_restores_previous_level() {
    lighting::LampFinder finder;
    const auto target = dali::Address::short_address(*dali::ShortAddress::from(9));
    const auto prev = dali::ArcLevel::from(115);

    const auto first = finder.start(target, lighting::FinderMode::BlinkOnOff, prev, 1000, 3000);
    TEST_ASSERT_TRUE(first.has_value());
    TEST_ASSERT_EQUAL_UINT8(254, first->level.value());
    TEST_ASSERT_FALSE(first->restore_final);

    // Next blink step at t=1600 ms -> OFF (0)
    const auto second = finder.poll(1600);
    TEST_ASSERT_TRUE(second.has_value());
    TEST_ASSERT_EQUAL_UINT8(0, second->level.value());

    // Switching to SmoothPulse on the same lamp keeps the original 115 saved level!
    const auto pulse = finder.start(target, lighting::FinderMode::SmoothPulse, second->level, 1800, 3000);
    TEST_ASSERT_TRUE(pulse.has_value());
    TEST_ASSERT_EQUAL_UINT8(254, pulse->level.value());
    const auto pulse2 = finder.poll(2050);
    TEST_ASSERT_TRUE(pulse2.has_value());
    TEST_ASSERT_EQUAL_UINT8(200, pulse2->level.value());

    // Stopping restores the original 115 level!
    const auto stopped = finder.stop();
    TEST_ASSERT_TRUE(stopped.has_value());
    TEST_ASSERT_TRUE(stopped->restore_final);
    TEST_ASSERT_EQUAL_UINT8(115, stopped->level.value());
    TEST_ASSERT_FALSE(finder.active());
}

void test_oled_strategy_supplies_ssd1306_registers_and_dimensions() {
    const auto& s64 = device::select_oled_strategy(device::OledGeometry::Oled128x64);
    TEST_ASSERT_EQUAL_UINT8(64, s64.height());
    TEST_ASSERT_EQUAL_UINT8(0x3F, s64.ssd1306_mux_ratio());
    TEST_ASSERT_EQUAL_UINT8(0x12, s64.ssd1306_com_pins());

    const auto& s32 = device::select_oled_strategy(device::OledGeometry::Oled128x32);
    TEST_ASSERT_EQUAL_UINT8(32, s32.height());
    TEST_ASSERT_EQUAL_UINT8(0x1F, s32.ssd1306_mux_ratio());
    TEST_ASSERT_EQUAL_UINT8(0x02, s32.ssd1306_com_pins());
}

}  // namespace

void run_diagnostics_and_network_tests() {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_error_catalog_has_descriptive_entries);
    RUN_TEST(test_diagnostic_log_persists_coalesces_and_backfills_epoch);
    RUN_TEST(test_diagnostic_log_ring_buffer_wraps_cleanly);
    RUN_TEST(test_dali_debug_trace_decodes_frames_and_auto_triggers);
    RUN_TEST(test_tracing_transport_logs_collisions_and_gear_faults);
    RUN_TEST(test_oled_display_renders_both_128x64_and_128x32_geometries);
    RUN_TEST(test_ha_discovery_and_mqtt_command_codec);
    RUN_TEST(test_api_codec_and_reconnect_backoff);
    RUN_TEST(test_lighting_service_queries_and_configures_gear_details);
    RUN_TEST(test_dali_psu_monitor_latches_off_on_short_until_explicit_reset);
    RUN_TEST(test_lamp_finder_blinks_pulses_and_restores_previous_level);
    RUN_TEST(test_oled_strategy_supplies_ssd1306_registers_and_dimensions);
}
