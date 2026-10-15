#include "network/NetworkConfig.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "device/OledDisplay.hpp"

namespace gateway::network {
namespace {

template <std::size_t N>
void ensure_null_terminated(std::array<char, N>& str, const char* fallback) noexcept {
    str[N - 1] = '\0';
    if (fallback && str[0] == '\0') {
        std::strncpy(str.data(), fallback, N - 1);
    }
}

}  // namespace

NetworkConfig default_network_config() noexcept {
    NetworkConfig cfg{};
    sanitize_network_config(cfg);
    return cfg;
}

void sanitize_network_config(NetworkConfig& cfg) noexcept {
    cfg.magic = 0x4B444E43u;
    ensure_null_terminated(cfg.wifi_ssid, nullptr);
    ensure_null_terminated(cfg.wifi_password, nullptr);
    ensure_null_terminated(cfg.hostname, "knx-dali-gw");
    ensure_null_terminated(cfg.mqtt_host, nullptr);
    ensure_null_terminated(cfg.mqtt_username, nullptr);
    ensure_null_terminated(cfg.mqtt_password, nullptr);
    ensure_null_terminated(cfg.mqtt_base_topic, "knx_dali");
    ensure_null_terminated(cfg.ha_discovery_prefix, "homeassistant");
    if (cfg.mqtt_port == 0) {
        cfg.mqtt_port = 1883;
    }
    if (static_cast<unsigned>(cfg.oled_geometry) > static_cast<unsigned>(device::OledGeometry::Disabled)) {
        cfg.oled_geometry = device::OledGeometry::Auto;
    }
}

void format_unique_ap_ssid(const char* chip_id, char* out, std::size_t out_size) noexcept {
    if (!out || out_size == 0) {
        return;
    }
    const char* id = (chip_id && chip_id[0] != '\0') ? chip_id : "0000000";
    std::snprintf(out, out_size, "KNX-DALI-SETUP-%.7s", id);
}

void format_unique_node_id(const char* base_name, const char* chip_id, char* out,
                           std::size_t out_size) noexcept {
    if (!out || out_size == 0) {
        return;
    }
    const char* base = (base_name && base_name[0] != '\0') ? base_name : "knx-dali-gw";
    const char* id = (chip_id && chip_id[0] != '\0') ? chip_id : "0000000";
    if (std::strstr(base, id) != nullptr) {
        std::snprintf(out, out_size, "%s", base);
        return;
    }
    std::snprintf(out, out_size, "%.20s-%.7s", base, id);
}

void ReconnectBackoff::on_connected() noexcept {
    connected_ = true;
    attempts_ = 0;
    delay_ms_ = initial_delay_ms;
}

void ReconnectBackoff::on_disconnected(std::uint32_t now_ms) noexcept {
    connected_ = false;
    next_retry_ms_ = now_ms + delay_ms_;
}

bool ReconnectBackoff::should_attempt(std::uint32_t now_ms) const noexcept {
    if (connected_) {
        return false;
    }
    return static_cast<std::int32_t>(now_ms - next_retry_ms_) >= 0;
}

void ReconnectBackoff::mark_attempt(std::uint32_t now_ms) noexcept {
    ++attempts_;
    next_retry_ms_ = now_ms + delay_ms_;
    if (delay_ms_ < max_delay_ms) {
        const auto doubled = delay_ms_ * 2U;
        delay_ms_ = doubled > max_delay_ms ? max_delay_ms : doubled;
    }
}

}
