#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "device/OledDisplay.hpp"

namespace gateway::network {

struct NetworkConfig {
    std::uint32_t magic = 0x4B444E43u;  // "KDNC"
    bool wifi_enabled = true;
    bool ap_fallback_enabled = true;
    bool wifi_power_save = true;
    bool mqtt_enabled = false;
    bool ha_discovery_enabled = true;
    bool internal_dali_psu_enabled = true;
    device::OledGeometry oled_geometry = device::OledGeometry::Auto;
    std::uint16_t oled_sleep_seconds = 60;
    std::uint16_t mqtt_port = 1883;
    std::array<char, 33> wifi_ssid{};
    std::array<char, 65> wifi_password{};
    std::array<char, 33> hostname{"knx-dali-gw"};
    std::array<char, 65> mqtt_host{};
    std::array<char, 33> mqtt_username{};
    std::array<char, 65> mqtt_password{};
    std::array<char, 33> mqtt_base_topic{"knx_dali"};
    std::array<char, 33> ha_discovery_prefix{"homeassistant"};
};

static_assert(std::is_trivially_copyable_v<NetworkConfig>, "NetworkConfig persists directly to NVS");

[[nodiscard]] NetworkConfig default_network_config() noexcept;
void sanitize_network_config(NetworkConfig& cfg) noexcept;
void format_unique_ap_ssid(const char* chip_id, char* out, std::size_t out_size) noexcept;
void format_unique_node_id(const char* base_name, const char* chip_id, char* out,
                           std::size_t out_size) noexcept;

class ReconnectBackoff final {
public:
    static constexpr std::uint32_t initial_delay_ms = 1'000;
    static constexpr std::uint32_t max_delay_ms = 30'000;

    void on_connected() noexcept;
    void on_disconnected(std::uint32_t now_ms) noexcept;
    [[nodiscard]] bool should_attempt(std::uint32_t now_ms) const noexcept;
    void mark_attempt(std::uint32_t now_ms) noexcept;
    [[nodiscard]] std::uint32_t attempts() const noexcept { return attempts_; }
    [[nodiscard]] std::uint32_t current_delay_ms() const noexcept { return delay_ms_; }
    [[nodiscard]] bool connected() const noexcept { return connected_; }

private:
    std::uint32_t delay_ms_ = initial_delay_ms;
    std::uint32_t next_retry_ms_ = 0;
    std::uint32_t attempts_ = 0;
    bool connected_ = false;
};

}
