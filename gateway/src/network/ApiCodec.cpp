#include "network/ApiCodec.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>

#include "control/Command.hpp"
#include "dali/Address.hpp"
#include "dali/ArcLevel.hpp"
#include "dali/Group.hpp"
#include "dali/Scene.hpp"
#include "dali/ShortAddress.hpp"
#include "dali/Transport.hpp"
#include "device/OledDisplay.hpp"
#include "diagnostics/ErrorCode.hpp"
#include "lighting/LampFinder.hpp"
#include "lighting/LightingService.hpp"
#include "network/NetworkConfig.hpp"

namespace gateway::network {
namespace {

const char* find_key(const char* json, const char* key) noexcept {
    if (!json || !key) {
        return nullptr;
    }
    char pat[40]{};
    std::snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* p = std::strstr(json, pat);
    if (!p) {
        return nullptr;
    }
    p += std::strlen(pat);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ':') {
        ++p;
    }
    return p;
}

std::optional<int> read_int(const char* json, const char* key) noexcept {
    const char* p = find_key(json, key);
    if (!p) {
        return std::nullopt;
    }
    char* end = nullptr;
    const long v = std::strtol(p, &end, 10);
    if (end == p) {
        return std::nullopt;
    }
    return static_cast<int>(v);
}

std::optional<bool> read_bool(const char* json, const char* key) noexcept {
    const char* p = find_key(json, key);
    if (!p) {
        return std::nullopt;
    }
    if (std::strncmp(p, "true", 4) == 0 || *p == '1') {
        return true;
    }
    if (std::strncmp(p, "false", 5) == 0 || *p == '0') {
        return false;
    }
    return std::nullopt;
}

bool read_str(const char* json, const char* key, char* dest, std::size_t dest_size) noexcept {
    const char* p = find_key(json, key);
    if (!p || *p != '"' || !dest || dest_size == 0) {
        return false;
    }
    ++p;
    std::size_t i = 0;
    while (*p != '\0' && *p != '"' && i + 1 < dest_size) {
        if (*p == '\\' && *(p + 1) != '\0') {
            ++p;
        }
        dest[i++] = *p++;
    }
    dest[i] = '\0';
    return *p == '"';
}

std::optional<dali::Address> read_target_address(const char* json) noexcept {
    char target[16]{};
    if (!read_str(json, "target", target, sizeof(target))) {
        if (const auto idx = read_int(json, "index")) {
            if (const auto s = dali::ShortAddress::from(static_cast<unsigned>(*idx))) {
                return dali::Address::short_address(*s);
            }
        }
        return dali::Address::broadcast();
    }
    const int idx = read_int(json, "index").value_or(0);
    if (std::strcmp(target, "ch") == 0 || std::strcmp(target, "channel") == 0) {
        if (const auto s = dali::ShortAddress::from(static_cast<unsigned>(idx))) {
            return dali::Address::short_address(*s);
        }
        return std::nullopt;
    }
    if (std::strcmp(target, "grp") == 0 || std::strcmp(target, "group") == 0) {
        if (const auto g = dali::Group::from(static_cast<unsigned>(idx))) {
            return dali::Address::group(*g);
        }
        return std::nullopt;
    }
    if (std::strcmp(target, "broadcast") == 0) {
        return dali::Address::broadcast();
    }
    return std::nullopt;
}

}  // namespace

std::optional<control::Command> parse_api_command_json(const char* json, std::size_t len) noexcept {
    if (!json || len == 0 || len > 1023) {
        return std::nullopt;
    }
    char buf[1024]{};
    std::memcpy(buf, json, len);
    buf[len] = '\0';

    char action[32]{};
    if (!read_str(buf, "action", action, sizeof(action))) {
        return std::nullopt;
    }

    if (std::strcmp(action, "scan_bus") == 0) {
        return control::ScanBus{};
    }
    if (std::strcmp(action, "scan_inputs") == 0) {
        return control::ScanInputDevices{};
    }
    if (std::strcmp(action, "reset_addresses") == 0) {
        return control::ResetAddresses{};
    }
    if (std::strcmp(action, "assign_addresses") == 0) {
        char mode_str[16]{};
        (void)read_str(buf, "mode", mode_str, sizeof(mode_str));
        const auto mode = (std::strcmp(mode_str, "all") == 0) ? dali::CommissionMode::All
                                                              : dali::CommissionMode::Unaddressed;
        return control::AssignAddresses{mode};
    }
    if (std::strcmp(action, "assign_inputs") == 0) {
        char mode_str[16]{};
        (void)read_str(buf, "mode", mode_str, sizeof(mode_str));
        const auto mode = (std::strcmp(mode_str, "all") == 0) ? dali::CommissionMode::All
                                                              : dali::CommissionMode::Unaddressed;
        return control::AssignInputDevices{mode};
    }
    if (std::strcmp(action, "recall_scene") == 0) {
        const int sc = read_int(buf, "scene").value_or(-1);
        if (const auto s = dali::Scene::from(static_cast<unsigned>(sc))) {
            return control::RecallScene{*s};
        }
        return std::nullopt;
    }
    if (std::strcmp(action, "store_scene") == 0) {
        const int sc = read_int(buf, "scene").value_or(-1);
        if (const auto s = dali::Scene::from(static_cast<unsigned>(sc))) {
            return control::StoreScene{*s};
        }
        return std::nullopt;
    }

    const int idx = read_int(buf, "index").value_or(-1);
    if (std::strcmp(action, "identify") == 0) {
        if (const auto s = dali::ShortAddress::from(static_cast<unsigned>(idx))) {
            return control::IdentifyDevice{*s};
        }
        return std::nullopt;
    }
    if (std::strcmp(action, "find_lamp") == 0) {
        char mode_str[16]{};
        (void)read_str(buf, "mode", mode_str, sizeof(mode_str));
        auto mode = lighting::FinderMode::BlinkOnOff;
        if (std::strcmp(mode_str, "stop") == 0) {
            mode = lighting::FinderMode::Stop;
        } else if (std::strcmp(mode_str, "pulse") == 0 || std::strcmp(mode_str, "smooth") == 0) {
            mode = lighting::FinderMode::SmoothPulse;
        }
        if (const auto t = read_target_address(buf)) {
            return control::FindLamp{*t, mode};
        }
        return std::nullopt;
    }
    if (std::strcmp(action, "replace_device") == 0) {
        if (const auto s = dali::ShortAddress::from(static_cast<unsigned>(idx))) {
            return control::ReplaceDevice{*s};
        }
        return std::nullopt;
    }
    if (std::strcmp(action, "refresh") == 0) {
        if (const auto s = dali::ShortAddress::from(static_cast<unsigned>(idx))) {
            return control::Refresh{*s};
        }
        return std::nullopt;
    }
    if (std::strcmp(action, "query_gear") == 0) {
        if (const auto s = dali::ShortAddress::from(static_cast<unsigned>(idx))) {
            return control::QueryGearDetails{*s};
        }
        return std::nullopt;
    }
    if (std::strcmp(action, "configure_gear") == 0) {
        const auto s = dali::ShortAddress::from(static_cast<unsigned>(idx));
        if (!s) {
            return std::nullopt;
        }
        lighting::GearConfig gcfg{};
        if (const auto v = read_int(buf, "min_level"); v && *v >= 1 && *v <= 254) {
            gcfg.min_level = static_cast<std::uint8_t>(*v);
        }
        if (const auto v = read_int(buf, "max_level"); v && *v >= 1 && *v <= 254) {
            gcfg.max_level = static_cast<std::uint8_t>(*v);
        }
        if (const auto v = read_int(buf, "power_on_level"); v && *v >= 0 && *v <= 254) {
            gcfg.power_on_level = static_cast<std::uint8_t>(*v);
        }
        if (const auto v = read_int(buf, "system_failure_level"); v && *v >= 0 && *v <= 254) {
            gcfg.system_failure_level = static_cast<std::uint8_t>(*v);
        }
        if (const auto v = read_int(buf, "fade_time"); v && *v >= 0 && *v <= 15) {
            gcfg.fade_time = static_cast<std::uint8_t>(*v);
        }
        if (const auto v = read_int(buf, "fade_rate"); v && *v >= 1 && *v <= 15) {
            gcfg.fade_rate = static_cast<std::uint8_t>(*v);
        }
        return control::ConfigureGear{*s, gcfg};
    }

    const auto addr = read_target_address(buf);
    if (!addr) {
        return std::nullopt;
    }
    if (std::strcmp(action, "switch_on") == 0) {
        return control::SwitchOn{*addr};
    }
    if (std::strcmp(action, "switch_off") == 0) {
        return control::SwitchOff{*addr};
    }
    if (std::strcmp(action, "set_level") == 0) {
        const int lvl = read_int(buf, "level").value_or(-1);
        if (const auto arc = dali::ArcLevel::from(static_cast<unsigned>(lvl))) {
            return control::SetLevel{*addr, *arc};
        }
        return std::nullopt;
    }
    if (std::strcmp(action, "set_cct") == 0) {
        const int kelvin = read_int(buf, "kelvin").value_or(0);
        if (kelvin >= 1500 && kelvin <= 10000) {
            return control::SetColourTemperature{*addr, static_cast<std::uint16_t>(kelvin)};
        }
        return std::nullopt;
    }
    if (std::strcmp(action, "set_rgb") == 0) {
        const int r = read_int(buf, "r").value_or(0);
        const int g = read_int(buf, "g").value_or(0);
        const int b = read_int(buf, "b").value_or(0);
        return control::SetRgb{*addr, static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                               static_cast<std::uint8_t>(b)};
    }
    if (std::strcmp(action, "set_rgbw") == 0) {
        const int r = read_int(buf, "r").value_or(0);
        const int g = read_int(buf, "g").value_or(0);
        const int b = read_int(buf, "b").value_or(0);
        const int w = read_int(buf, "w").value_or(0);
        return control::SetRgbw{*addr,
                                static_cast<std::uint8_t>(r),
                                static_cast<std::uint8_t>(g),
                                static_cast<std::uint8_t>(b),
                                static_cast<std::uint8_t>(w),
                                0x0FU};
    }
    if (std::strcmp(action, "set_xy") == 0) {
        const int x = read_int(buf, "x").value_or(0);
        const int y = read_int(buf, "y").value_or(0);
        const int bri = read_int(buf, "brightness").value_or(254);
        return control::SetXy{*addr, static_cast<std::uint16_t>(x),  static_cast<std::uint16_t>(y),
                              true,  static_cast<std::uint8_t>(bri), true};
    }
    return std::nullopt;
}

bool apply_network_config_json(NetworkConfig& cfg, const char* json, std::size_t len) noexcept {
    if (!json || len == 0 || len > 1023) {
        return false;
    }
    char buf[1024]{};
    std::memcpy(buf, json, len);
    buf[len] = '\0';

    if (const auto v = read_bool(buf, "wifi_enabled")) {
        cfg.wifi_enabled = *v;
    }
    if (const auto v = read_bool(buf, "ap_fallback_enabled")) {
        cfg.ap_fallback_enabled = *v;
    }
    if (const auto v = read_bool(buf, "wifi_power_save")) {
        cfg.wifi_power_save = *v;
    }
    if (const auto v = read_bool(buf, "mqtt_enabled")) {
        cfg.mqtt_enabled = *v;
    }
    if (const auto v = read_bool(buf, "ha_discovery_enabled")) {
        cfg.ha_discovery_enabled = *v;
    }
    if (const auto v = read_bool(buf, "internal_dali_psu_enabled")) {
        cfg.internal_dali_psu_enabled = *v;
    }
    if (const auto v = read_int(buf, "oled_geometry"); v && *v >= 0 && *v <= 3) {
        cfg.oled_geometry = static_cast<device::OledGeometry>(*v);
    }
    if (const auto v = read_int(buf, "oled_sleep_seconds"); v && *v >= 0 && *v <= 3600) {
        cfg.oled_sleep_seconds = static_cast<std::uint16_t>(*v);
    }
    if (const auto v = read_int(buf, "mqtt_port"); v && *v >= 1 && *v <= 65535) {
        cfg.mqtt_port = static_cast<std::uint16_t>(*v);
    }

    char tmp[65]{};
    if (read_str(buf, "wifi_ssid", tmp, cfg.wifi_ssid.size())) {
        std::strncpy(cfg.wifi_ssid.data(), tmp, cfg.wifi_ssid.size() - 1);
    }
    if (read_str(buf, "wifi_password", tmp, cfg.wifi_password.size()) && std::strcmp(tmp, "********") != 0) {
        std::strncpy(cfg.wifi_password.data(), tmp, cfg.wifi_password.size() - 1);
    }
    if (read_str(buf, "hostname", tmp, cfg.hostname.size())) {
        std::strncpy(cfg.hostname.data(), tmp, cfg.hostname.size() - 1);
    }
    if (read_str(buf, "mqtt_host", tmp, cfg.mqtt_host.size())) {
        std::strncpy(cfg.mqtt_host.data(), tmp, cfg.mqtt_host.size() - 1);
    }
    if (read_str(buf, "mqtt_username", tmp, cfg.mqtt_username.size())) {
        std::strncpy(cfg.mqtt_username.data(), tmp, cfg.mqtt_username.size() - 1);
    }
    if (read_str(buf, "mqtt_password", tmp, cfg.mqtt_password.size()) && std::strcmp(tmp, "********") != 0) {
        std::strncpy(cfg.mqtt_password.data(), tmp, cfg.mqtt_password.size() - 1);
    }
    if (read_str(buf, "mqtt_base_topic", tmp, cfg.mqtt_base_topic.size())) {
        std::strncpy(cfg.mqtt_base_topic.data(), tmp, cfg.mqtt_base_topic.size() - 1);
    }
    if (read_str(buf, "ha_discovery_prefix", tmp, cfg.ha_discovery_prefix.size())) {
        std::strncpy(cfg.ha_discovery_prefix.data(), tmp, cfg.ha_discovery_prefix.size() - 1);
    }
    sanitize_network_config(cfg);
    return true;
}

std::size_t format_network_config_json(const NetworkConfig& cfg, char* out, std::size_t out_size) noexcept {
    if (!out || out_size == 0) {
        return 0;
    }
    const int n = std::snprintf(
        out, out_size,
        "{\"wifi_enabled\":%s,\"ap_fallback_enabled\":%s,\"wifi_power_save\":%s,"
        "\"wifi_ssid\":\"%s\",\"wifi_password\":\"%s\",\"hostname\":\"%s\","
        "\"mqtt_enabled\":%s,\"mqtt_host\":\"%s\",\"mqtt_port\":%u,"
        "\"mqtt_username\":\"%s\",\"mqtt_password\":\"%s\",\"mqtt_base_topic\":\"%s\","
        "\"ha_discovery_enabled\":%s,\"ha_discovery_prefix\":\"%s\","
        "\"oled_geometry\":%u,\"oled_sleep_seconds\":%u,\"internal_dali_psu_enabled\":%s}",
        cfg.wifi_enabled ? "true" : "false", cfg.ap_fallback_enabled ? "true" : "false",
        cfg.wifi_power_save ? "true" : "false", cfg.wifi_ssid.data(),
        cfg.wifi_password[0] != '\0' ? "********" : "", cfg.hostname.data(),
        cfg.mqtt_enabled ? "true" : "false", cfg.mqtt_host.data(), static_cast<unsigned>(cfg.mqtt_port),
        cfg.mqtt_username.data(), cfg.mqtt_password[0] != '\0' ? "********" : "", cfg.mqtt_base_topic.data(),
        cfg.ha_discovery_enabled ? "true" : "false", cfg.ha_discovery_prefix.data(),
        static_cast<unsigned>(cfg.oled_geometry), static_cast<unsigned>(cfg.oled_sleep_seconds),
        cfg.internal_dali_psu_enabled ? "true" : "false");
    return (n > 0 && static_cast<std::size_t>(n) < out_size) ? static_cast<std::size_t>(n) : 0;
}

std::size_t format_error_catalog_json(char* out, std::size_t out_size) noexcept {
    if (!out || out_size < 16) {
        return 0;
    }
    std::size_t count = 0;
    const auto* items = diagnostics::error_catalog(count);
    std::size_t offset = 0;
    offset += static_cast<std::size_t>(std::snprintf(out + offset, out_size - offset, "["));
    for (std::size_t i = 0; i < count; ++i) {
        const auto& d = items[i];
        const int written = std::snprintf(
            out + offset, out_size - offset,
            "%s{\"code\":%u,\"symbol\":\"%s\",\"title\":\"%s\",\"description\":\"%s\",\"remedy\":\"%s\"}",
            i == 0 ? "" : ",", static_cast<unsigned>(d.code), d.symbol, d.title, d.description, d.remedy);
        if (written <= 0 || offset + static_cast<std::size_t>(written) + 2 >= out_size) {
            break;
        }
        offset += static_cast<std::size_t>(written);
    }
    offset += static_cast<std::size_t>(std::snprintf(out + offset, out_size - offset, "]"));
    return offset;
}

}
