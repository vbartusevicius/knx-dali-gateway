#include "firmware/RuntimeState.hpp"

#include <cstdio>
#include <cstring>

#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <nvs_flash.h>

namespace gateway::firmware {
namespace {

const char* source_str(lighting::StateSource src) noexcept {
    switch (src) {
    case lighting::StateSource::Readback:
        return "readback";
    case lighting::StateSource::Commanded:
        return "commanded";
    case lighting::StateSource::RestoredCommand:
        return "fallback";
    case lighting::StateSource::Unknown:
        return "unknown";
    }
    return "unknown";
}

const char* phase_str(commissioning::Phase phase) noexcept {
    switch (phase) {
    case commissioning::Phase::Idle:
        return "idle";
    case commissioning::Phase::Scanning:
        return "scanning";
    case commissioning::Phase::Addressing:
        return "addressing";
    case commissioning::Phase::Replacing:
        return "replacing";
    case commissioning::Phase::Identifying:
        return "identifying";
    case commissioning::Phase::Done:
        return "done";
    case commissioning::Phase::Failed:
        return "failed";
    }
    return "idle";
}

}  // namespace

bool NvsLogBackend::open() noexcept {
    if (handle_ != 0) {
        return true;
    }
    return nvs_open("diag_v1", NVS_READWRITE, &handle_) == ESP_OK;
}

bool NvsLogBackend::load(void* data, std::size_t size) noexcept {
    if (!open()) {
        return false;
    }
    std::size_t actual = size;
    return nvs_get_blob(handle_, "log_ring", data, &actual) == ESP_OK && actual == size;
}

bool NvsLogBackend::save(const void* data, std::size_t size) noexcept {
    if (!open()) {
        return false;
    }
    if (nvs_set_blob(handle_, "log_ring", data, size) != ESP_OK) {
        return false;
    }
    return nvs_commit(handle_) == ESP_OK;
}

RuntimeState::RuntimeState() noexcept : log_(&log_backend_) {
    mutex_ = xSemaphoreCreateMutexStatic(&mutex_buf_);
    net_cfg_ = network::default_network_config();
}

void RuntimeState::lock() const noexcept {
    if (mutex_) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
}

void RuntimeState::unlock() const noexcept {
    if (mutex_) {
        xSemaphoreGive(mutex_);
    }
}

void RuntimeState::init_storage() noexcept {
    lock();
    (void)log_.load_from_storage();

    nvs_handle_t h = 0;
    if (nvs_open("net_cfg", NVS_READWRITE, &h) == ESP_OK) {
        network::NetworkConfig loaded{};
        std::size_t len = sizeof(loaded);
        if (nvs_get_blob(h, "cfg", &loaded, &len) == ESP_OK && len == sizeof(loaded) &&
            loaded.magic == 0x4B444E43u) {
            network::sanitize_network_config(loaded);
            net_cfg_ = loaded;
        }
        nvs_close(h);
    }

    oled_.set_geometry(net_cfg_.oled_geometry);
    oled_.set_sleep_timeout_ms(static_cast<std::uint32_t>(net_cfg_.oled_sleep_seconds) * 1000U);
    oled_state_.dali_psu_enabled = net_cfg_.internal_dali_psu_enabled;
    oled_state_.ha_discovery_enabled = net_cfg_.ha_discovery_enabled;
    std::strncpy(oled_state_.ssid.data(),
                 net_cfg_.wifi_ssid[0] != '\0' ? net_cfg_.wifi_ssid.data() : "AP-Portal",
                 oled_state_.ssid.size() - 1);

    const auto reason = esp_reset_reason();
    if (reason == ESP_RST_TASK_WDT || reason == ESP_RST_INT_WDT || reason == ESP_RST_WDT) {
        log_.record(diagnostics::LogSource::System, diagnostics::LogSeverity::Error,
                    diagnostics::ErrorCode::WatchdogResetRecovered, diagnostics::no_address,
                    "reboot reason=watchdog", 0);
    } else if (reason == ESP_RST_BROWNOUT) {
        log_.record(diagnostics::LogSource::System, diagnostics::LogSeverity::Warning,
                    diagnostics::ErrorCode::BrownoutResetRecovered, diagnostics::no_address,
                    "reboot reason=brownout", 0);
    } else if (reason == ESP_RST_PANIC) {
        log_.record(diagnostics::LogSource::System, diagnostics::LogSeverity::Critical,
                    diagnostics::ErrorCode::PanicResetRecovered, diagnostics::no_address,
                    "reboot reason=panic", 0);
    }
    unlock();
}

void RuntimeState::save_network_config(const network::NetworkConfig& cfg) noexcept {
    lock();
    net_cfg_ = cfg;
    network::sanitize_network_config(net_cfg_);
    oled_.set_geometry(net_cfg_.oled_geometry);
    oled_.set_sleep_timeout_ms(static_cast<std::uint32_t>(net_cfg_.oled_sleep_seconds) * 1000U);
    oled_state_.dali_psu_enabled = net_cfg_.internal_dali_psu_enabled;
    oled_state_.ha_discovery_enabled = net_cfg_.ha_discovery_enabled;
    std::strncpy(oled_state_.ssid.data(),
                 net_cfg_.wifi_ssid[0] != '\0' ? net_cfg_.wifi_ssid.data() : "AP-Portal",
                 oled_state_.ssid.size() - 1);

    nvs_handle_t h = 0;
    if (nvs_open("net_cfg", NVS_READWRITE, &h) == ESP_OK) {
        (void)nvs_set_blob(h, "cfg", &net_cfg_, sizeof(net_cfg_));
        (void)nvs_commit(h);
        nvs_close(h);
    }
    unlock();
}

network::NetworkConfig RuntimeState::network_config() const noexcept {
    lock();
    const auto copy = net_cfg_;
    unlock();
    return copy;
}

void RuntimeState::sync_from_worker(const lighting::LightingService& lighting,
                                    const commissioning::Commissioning& comm, std::uint32_t now_ms) noexcept {
    lock();
    const auto& present_bits = comm.present();
    const auto& faulty_bits = comm.faulty();
    const auto snap = comm.snapshot();
    inputs_ = comm.input_snapshot();

    oled_state_.gear_present = present_bits;
    oled_state_.gear_faulty = faulty_bits;
    oled_state_.gear_on.reset();
    oled_state_.present_gear_count = static_cast<std::uint8_t>(present_bits.count());
    oled_state_.faulty_gear_count = static_cast<std::uint8_t>(faulty_bits.count());

    unsigned on_count = 0;
    unsigned sum_arc = 0;
    for (unsigned i = 0; i < 64; ++i) {
        const auto addr = *dali::ShortAddress::from(i);
        const auto st = lighting.state(addr);
        auto& ch = channels_[i];
        ch.level = st.level ? st.level->value() : 0U;
        ch.source = st.source;
        ch.colour = lighting.colour_state(addr);
        if (const auto cap = lighting.capabilities(addr)) {
            ch.capabilities = *cap;
            ch.has_capabilities = true;
        } else {
            ch.has_capabilities = false;
        }
        ch.details = lighting.gear_details(addr);
        if (ch.details.groups == 0) {
            ch.details.groups = lighting.configuration().group_masks[i];
        }
        if (ch.level > 0) {
            oled_state_.gear_on.set(i);
            ++on_count;
            sum_arc += ch.level;
        }
    }
    oled_state_.active_lamp_count = static_cast<std::uint8_t>(on_count);
    oled_state_.average_brightness_pct =
        on_count > 0 ? static_cast<std::uint8_t>((sum_arc * 100U) / (on_count * 254U)) : 0U;

    for (unsigned g = 0; g < 16; ++g) {
        const auto st = lighting.state(*dali::Group::from(g));
        oled_state_.group_levels[g] = st.level ? st.level->value() : 0U;
    }

    unsigned input_devs = 0;
    for (unsigned i = 0; i < 64; ++i) {
        if ((inputs_.present[i / 8] & (1U << (i % 8))) != 0) {
            ++input_devs;
        }
    }
    oled_state_.input_device_count = static_cast<std::uint8_t>(input_devs);
    oled_state_.comm_phase = snap.progress.phase;
    oled_state_.comm_progress = snap.progress.progress;
    oled_state_.comm_found = snap.progress.found;
    oled_state_.dali_tx_frames = trace_.total_frames();
    oled_state_.dali_collisions = trace_.total_collisions();
    oled_state_.logged_errors = static_cast<std::uint32_t>(log_.size());
    if (const auto* last = log_.latest()) {
        oled_state_.last_error_code = last->code;
        oled_state_.last_error_address = last->address;
    }
    oled_state_.uptime_seconds = now_ms / 1000U;
    unlock();
}

void RuntimeState::update_knx_state(std::uint16_t ia, bool configured, bool prog_mode, bool bus_ok,
                                    device::PanelHint hint, std::uint32_t now_ms) noexcept {
    lock();
    oled_state_.knx_individual_address = ia;
    oled_state_.knx_configured = configured;
    oled_state_.knx_prog_mode = prog_mode;
    oled_state_.knx_bus_ok = bus_ok;
    oled_state_.hint = hint;
    oled_state_.uptime_seconds = now_ms / 1000U;

    if (bus_ok && power_loss_latched_) {
        power_loss_latched_ = false;
        log_.record(diagnostics::LogSource::Knx, diagnostics::LogSeverity::Info,
                    diagnostics::ErrorCode::KnxBusPowerRestored, diagnostics::no_address,
                    "NCN5130 SAVEB released", now_ms);
    }

    device::DeviceStatus dev_st{};
    dev_st.knx_bus_ok = bus_ok;
    dev_st.knx_prog_mode = prog_mode;
    dev_st.dali = oled_state_.faulty_gear_count > 0 ? device::DaliState::GearFault : device::DaliState::Ok;
    dev_st.wifi = oled_state_.wifi_state;
    dev_st.hint = hint;
    led_colours_ = device::led_colours(dev_st, now_ms);
    unlock();
}

void RuntimeState::update_network_state(device::WifiState wifi, std::int8_t rssi, const char* ipv4,
                                        bool mqtt_connected, std::uint8_t ws_clients,
                                        std::uint32_t now_ms) noexcept {
    lock();
    oled_state_.wifi_state = wifi;
    oled_state_.wifi_rssi_dbm = rssi;
    if (ipv4) {
        std::strncpy(oled_state_.ipv4.data(), ipv4, oled_state_.ipv4.size() - 1);
    }
    oled_state_.mqtt_connected = mqtt_connected;
    oled_state_.ws_clients = ws_clients;
    oled_state_.uptime_seconds = now_ms / 1000U;
    unlock();
}

void RuntimeState::on_power_fail_saveb(std::uint32_t now_ms) noexcept {
    lock();
    if (!power_loss_latched_) {
        power_loss_latched_ = true;
        log_.record(diagnostics::LogSource::Knx, diagnostics::LogSeverity::Critical,
                    diagnostics::ErrorCode::KnxBusPowerLoss, diagnostics::no_address,
                    "NCN5130 SAVEB low (VFILT < 14V)", now_ms);
        (void)log_.flush_to_storage();
    }
    unlock();
}

void RuntimeState::request_psu_reset() noexcept {
    lock();
    psu_reset_requested_ = true;
    unlock();
}

bool RuntimeState::consume_psu_reset_request() noexcept {
    lock();
    const bool req = psu_reset_requested_;
    psu_reset_requested_ = false;
    unlock();
    return req;
}

void RuntimeState::next_oled_page(std::uint32_t now_ms) noexcept {
    lock();
    oled_.next_page(now_ms);
    oled_.render(oled_state_, now_ms);
    unlock();
}

void RuntimeState::toggle_oled_geometry(std::uint32_t now_ms) noexcept {
    lock();
    net_cfg_.oled_geometry = (oled_.resolved_geometry() == device::OledGeometry::Oled128x32)
                                 ? device::OledGeometry::Oled128x64
                                 : device::OledGeometry::Oled128x32;
    oled_.set_geometry(net_cfg_.oled_geometry);
    oled_.wake(now_ms);
    oled_.render(oled_state_, now_ms);
    unlock();
}

device::OledSystemState RuntimeState::oled_state() const noexcept {
    lock();
    const auto copy = oled_state_;
    unlock();
    return copy;
}

void RuntimeState::render_oled(std::uint32_t now_ms) noexcept {
    lock();
    oled_.render(oled_state_, now_ms);
    unlock();
}

ChannelSnapshot RuntimeState::channel(std::uint8_t index) const noexcept {
    lock();
    const auto copy = channels_[index & 0x3FU];
    unlock();
    return copy;
}

bool RuntimeState::channel_present(std::uint8_t index) const noexcept {
    lock();
    const bool v = oled_state_.gear_present.test(index & 0x3FU);
    unlock();
    return v;
}

bool RuntimeState::channel_faulty(std::uint8_t index) const noexcept {
    lock();
    const bool v = oled_state_.gear_faulty.test(index & 0x3FU);
    unlock();
    return v;
}

std::uint8_t RuntimeState::group_level(std::uint8_t index) const noexcept {
    lock();
    const auto v = oled_state_.group_levels[index & 0x0FU];
    unlock();
    return v;
}

std::size_t RuntimeState::format_live_ws_json(char* out, std::size_t out_size,
                                              std::uint32_t now_ms) const noexcept {
    if (!out || out_size < 512) {
        return 0;
    }
    lock();
    std::size_t off = 0;
    const auto append = [&](const char* fmt, auto... args) {
        if (off >= out_size) {
            return;
        }
        const int n = std::snprintf(out + off, out_size - off, fmt, args...);
        if (n > 0 && off + static_cast<std::size_t>(n) < out_size) {
            off += static_cast<std::size_t>(n);
        }
    };

    const auto* app = esp_app_get_description();
    const auto* running_part = esp_ota_get_running_partition();
    const std::uint32_t free_heap = esp_get_free_heap_size();
    const std::uint32_t min_heap = esp_get_minimum_free_heap_size();

    append(
        "{\"type\":\"live\",\"uptime_s\":%lu,\"epoch_ms\":%llu,\"free_heap\":%lu,\"min_heap\":%lu,"
        "\"fw_version\":\"%s\",\"partition\":\"%s\","
        "\"knx\":{\"ia\":%u,\"configured\":%s,\"prog_mode\":%s,\"bus_ok\":%s},"
        "\"dali\":{\"present\":%u,\"active\":%u,\"faulty\":%u,\"inputs\":%u,\"psu_on\":%s,"
        "\"tx_frames\":%lu,\"collisions\":%lu,\"errors\":%lu,\"comm_phase\":\"%s\",\"comm_progress\":%u},"
        "\"net\":{\"wifi_state\":%u,\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"mqtt\":%s,\"ha\":%s,\"ws\":%"
        "u},"
        "\"leds\":[[%u,%u,%u],[%u,%u,%u],[%u,%u,%u]],"
        "\"oled\":{\"w\":%u,\"h\":%u,\"page\":%u,\"hex\":\"",
        static_cast<unsigned long>(now_ms / 1000U),
        static_cast<unsigned long long>(log_.current_epoch_ms(now_ms)), static_cast<unsigned long>(free_heap),
        static_cast<unsigned long>(min_heap), app ? app->version : "0.1.0",
        running_part ? running_part->label : "ota_0",
        static_cast<unsigned>(oled_state_.knx_individual_address),
        oled_state_.knx_configured ? "true" : "false", oled_state_.knx_prog_mode ? "true" : "false",
        oled_state_.knx_bus_ok ? "true" : "false", static_cast<unsigned>(oled_state_.present_gear_count),
        static_cast<unsigned>(oled_state_.active_lamp_count),
        static_cast<unsigned>(oled_state_.faulty_gear_count),
        static_cast<unsigned>(oled_state_.input_device_count),
        oled_state_.dali_psu_enabled ? "true" : "false", static_cast<unsigned long>(trace_.total_frames()),
        static_cast<unsigned long>(trace_.total_collisions()),
        static_cast<unsigned long>(trace_.total_errors()), phase_str(oled_state_.comm_phase),
        static_cast<unsigned>(oled_state_.comm_progress), static_cast<unsigned>(oled_state_.wifi_state),
        oled_state_.ssid.data(), oled_state_.ipv4.data(), static_cast<int>(oled_state_.wifi_rssi_dbm),
        oled_state_.mqtt_connected ? "true" : "false", oled_state_.ha_discovery_enabled ? "true" : "false",
        static_cast<unsigned>(oled_state_.ws_clients), static_cast<unsigned>(led_colours_[0].r),
        static_cast<unsigned>(led_colours_[0].g), static_cast<unsigned>(led_colours_[0].b),
        static_cast<unsigned>(led_colours_[1].r), static_cast<unsigned>(led_colours_[1].g),
        static_cast<unsigned>(led_colours_[1].b), static_cast<unsigned>(led_colours_[2].r),
        static_cast<unsigned>(led_colours_[2].g), static_cast<unsigned>(led_colours_[2].b),
        static_cast<unsigned>(oled_.width()), static_cast<unsigned>(oled_.height()),
        static_cast<unsigned>(oled_.page()));

    const std::size_t fb_bytes = oled_.framebuffer_bytes();
    const auto& fb = oled_.buffer();
    for (std::size_t i = 0; i < fb_bytes && off + 4 < out_size; ++i) {
        append("%02X", static_cast<unsigned>(fb[i]));
    }

    append("\"},\"groups\":[");
    for (unsigned g = 0; g < 16; ++g) {
        append("%s%u", g == 0 ? "" : ",", static_cast<unsigned>(oled_state_.group_levels[g]));
    }
    append("],\"channels\":[");
    for (unsigned i = 0; i < 64; ++i) {
        const auto& c = channels_[i];
        const unsigned p = oled_state_.gear_present.test(i) ? 1U : 0U;
        const unsigned f = oled_state_.gear_faulty.test(i) ? 1U : 0U;
        const unsigned dt8 = c.has_capabilities ? ((c.capabilities.tc_capable ? 1U : 0U) |
                                                   ((c.capabilities.rgbwaf_channels >= 3 ? 1U : 0U) << 1U) |
                                                   ((c.capabilities.rgbwaf_channels >= 4 ? 1U : 0U) << 2U) |
                                                   ((c.capabilities.xy_capable ? 1U : 0U) << 3U))
                                                : 0U;
        append("%s[%u,%u,%u,\"%s\",%u,%u,%u,%u,%u,%u,%u,%u]", i == 0 ? "" : ",", p, f,
               static_cast<unsigned>(c.level), source_str(c.source), dt8,
               static_cast<unsigned>(c.colour.temperature_kelvin), static_cast<unsigned>(c.colour.r),
               static_cast<unsigned>(c.colour.g), static_cast<unsigned>(c.colour.b),
               static_cast<unsigned>(c.details.status_byte), static_cast<unsigned>(c.details.groups),
               static_cast<unsigned>(c.details.device_type));
    }
    append("],\"inputs\":[");
    for (unsigned i = 0; i < 64; ++i) {
        const bool pres = (inputs_.present[i / 8] & (1U << (i % 8))) != 0;
        append("%s[%u,%u]", i == 0 ? "" : ",", pres ? 1U : 0U, static_cast<unsigned>(inputs_.summary[i]));
    }
    append("]}");
    unlock();
    return off;
}

std::size_t RuntimeState::format_channel_detail_json(std::uint8_t ch, char* out,
                                                     std::size_t out_size) const noexcept {
    if (!out || out_size < 256 || ch >= 64) {
        return 0;
    }
    lock();
    const auto& c = channels_[ch];
    const auto& d = c.details;
    const bool pres = oled_state_.gear_present.test(ch);
    const bool flt = oled_state_.gear_faulty.test(ch);
    std::size_t off = 0;
    const int n = std::snprintf(
        out, out_size,
        "{\"address\":%u,\"present\":%s,\"faulty\":%s,\"level\":%u,\"source\":\"%s\","
        "\"status_byte\":%u,\"gear_failure\":%s,\"lamp_failure\":%s,\"arc_on\":%s,"
        "\"limit_error\":%s,\"fade_running\":%s,\"reset_state\":%s,\"missing_short\":%s,\"power_cycle\":%s,"
        "\"device_type\":%u,\"version\":%u,\"physical_min\":%u,\"min_level\":%u,\"max_level\":%u,"
        "\"power_on_level\":%u,\"system_failure_level\":%u,\"fade_time\":%u,\"fade_rate\":%u,"
        "\"groups\":%u,\"probed\":%s,\"kelvin\":%u,\"r\":%u,\"g\":%u,\"b\":%u,\"w\":%u,\"scenes\":[",
        static_cast<unsigned>(ch), pres ? "true" : "false", flt ? "true" : "false",
        static_cast<unsigned>(c.level), source_str(c.source), static_cast<unsigned>(d.status_byte),
        (d.status_byte & 0x01U) ? "true" : "false", (d.status_byte & 0x02U) ? "true" : "false",
        (d.status_byte & 0x04U) ? "true" : "false", (d.status_byte & 0x08U) ? "true" : "false",
        (d.status_byte & 0x10U) ? "true" : "false", (d.status_byte & 0x20U) ? "true" : "false",
        (d.status_byte & 0x40U) ? "true" : "false", (d.status_byte & 0x80U) ? "true" : "false",
        static_cast<unsigned>(d.device_type), static_cast<unsigned>(d.version_number),
        static_cast<unsigned>(d.physical_min), static_cast<unsigned>(d.min_level),
        static_cast<unsigned>(d.max_level), static_cast<unsigned>(d.power_on_level),
        static_cast<unsigned>(d.system_failure_level), static_cast<unsigned>(d.fade_time),
        static_cast<unsigned>(d.fade_rate), static_cast<unsigned>(d.groups), d.probed ? "true" : "false",
        static_cast<unsigned>(c.colour.temperature_kelvin), static_cast<unsigned>(c.colour.r),
        static_cast<unsigned>(c.colour.g), static_cast<unsigned>(c.colour.b),
        static_cast<unsigned>(c.colour.w));
    if (n > 0 && static_cast<std::size_t>(n) < out_size) {
        off = static_cast<std::size_t>(n);
    }
    for (unsigned s = 0; s < 16 && off + 8 < out_size; ++s) {
        off += static_cast<std::size_t>(std::snprintf(out + off, out_size - off, "%s%u", s == 0 ? "" : ",",
                                                      static_cast<unsigned>(d.scenes[s])));
    }
    if (off + 4 < out_size) {
        off += static_cast<std::size_t>(std::snprintf(out + off, out_size - off, "]}"));
    }
    unlock();
    return off;
}

std::size_t RuntimeState::format_logs_json(char* out, std::size_t out_size) const noexcept {
    if (!out || out_size < 128) {
        return 0;
    }
    lock();
    std::size_t off = 0;
    off += static_cast<std::size_t>(std::snprintf(out + off, out_size - off, "{\"entries\":["));
    const std::size_t n_entries = log_.size();
    for (std::size_t i = 0; i < n_entries; ++i) {
        const auto& e = log_.entry_oldest_first(n_entries - 1 - i);
        const auto& desc = diagnostics::describe_error(e.code);
        const int w = std::snprintf(
            out + off, out_size - off,
            "%s{\"seq\":%lu,\"epoch_ms\":%llu,\"uptime_ms\":%lu,\"count\":%u,\"address\":%u,"
            "\"source\":\"%s\",\"severity\":\"%s\",\"code\":%u,\"symbol\":\"%s\",\"title\":\"%s\","
            "\"detail\":\"%s\",\"description\":\"%s\",\"remedy\":\"%s\"}",
            i == 0 ? "" : ",", static_cast<unsigned long>(e.seq), static_cast<unsigned long long>(e.epoch_ms),
            static_cast<unsigned long>(e.uptime_ms), static_cast<unsigned>(e.count),
            static_cast<unsigned>(e.address), diagnostics::source_name(e.source),
            diagnostics::severity_name(e.severity), static_cast<unsigned>(e.code), desc.symbol, desc.title,
            e.detail.data(), desc.description, desc.remedy);
        if (w <= 0 || off + static_cast<std::size_t>(w) + 16 >= out_size) {
            break;
        }
        off += static_cast<std::size_t>(w);
    }
    off += static_cast<std::size_t>(std::snprintf(out + off, out_size - off, "]}"));
    unlock();
    return off;
}

std::size_t RuntimeState::format_trace_json(char* out, std::size_t out_size,
                                            std::size_t max_items) const noexcept {
    if (!out || out_size < 128) {
        return 0;
    }
    lock();
    std::size_t off = 0;
    off += static_cast<std::size_t>(
        std::snprintf(out + off, out_size - off,
                      "{\"active\":%s,\"auto_trigger\":%s,\"total_frames\":%lu,\"total_collisions\":%lu,"
                      "\"total_errors\":%lu,\"entries\":[",
                      trace_.active() ? "true" : "false", trace_.auto_trigger_from_dali() ? "true" : "false",
                      static_cast<unsigned long>(trace_.total_frames()),
                      static_cast<unsigned long>(trace_.total_collisions()),
                      static_cast<unsigned long>(trace_.total_errors())));

    const std::size_t count = trace_.size();
    const std::size_t start = count > max_items ? (count - max_items) : 0;
    for (std::size_t idx = 0; idx < count - start; ++idx) {
        const auto& e = trace_.entry_oldest_first(count - 1 - idx);
        diagnostics::DecodedTrace dec{};
        diagnostics::decode_trace_entry(e, dec);
        const int w = std::snprintf(
            out + off, out_size - off,
            "%s{\"seq\":%lu,\"epoch_ms\":%llu,\"uptime_ms\":%lu,\"bits\":%u,\"frame\":%lu,"
            "\"has_reply\":%s,\"reply\":%u,\"target\":\"%s\",\"mnemonic\":\"%s\","
            "\"detail\":\"%s\",\"status\":\"%s\"}",
            idx == 0 ? "" : ",", static_cast<unsigned long>(e.seq),
            static_cast<unsigned long long>(e.epoch_ms), static_cast<unsigned long>(e.uptime_ms),
            static_cast<unsigned>(e.bits), static_cast<unsigned long>(e.frame),
            e.has_reply ? "true" : "false", static_cast<unsigned>(e.reply), dec.target.data(),
            dec.mnemonic.data(), dec.detail.data(), dec.status_text);
        if (w <= 0 || off + static_cast<std::size_t>(w) + 16 >= out_size) {
            break;
        }
        off += static_cast<std::size_t>(w);
    }
    off += static_cast<std::size_t>(std::snprintf(out + off, out_size - off, "]}"));
    unlock();
    return off;
}

RuntimeState& runtime_state() noexcept {
    static RuntimeState instance;
    return instance;
}

}
