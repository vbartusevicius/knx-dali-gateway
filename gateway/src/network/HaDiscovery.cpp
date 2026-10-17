#include "network/HaDiscovery.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>

#include "control/Command.hpp"
#include "dali/Address.hpp"
#include "dali/ColourControl.hpp"
#include "dali/Group.hpp"
#include "dali/Scene.hpp"
#include "dali/ShortAddress.hpp"
#include "firmware/Version.hpp"
#include "lighting/LightingService.hpp"

namespace gateway::network {
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

const char* find_json_key(const char* json, const char* key) noexcept {
    if (!json || !key) {
        return nullptr;
    }
    char pattern[36]{};
    std::snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* hit = std::strstr(json, pattern);
    if (!hit) {
        return nullptr;
    }
    hit += std::strlen(pattern);
    while (*hit == ' ' || *hit == '\t' || *hit == ':') {
        ++hit;
    }
    return hit;
}

std::optional<int> parse_json_int(const char* json, const char* key) noexcept {
    const char* val = find_json_key(json, key);
    if (!val) {
        return std::nullopt;
    }
    char* end = nullptr;
    const long v = std::strtol(val, &end, 10);
    if (end == val) {
        return std::nullopt;
    }
    return static_cast<int>(v);
}

bool json_string_equals(const char* json, const char* key, const char* expected) noexcept {
    const char* val = find_json_key(json, key);
    if (!val || *val != '"') {
        return false;
    }
    ++val;
    const std::size_t len = std::strlen(expected);
    return std::strncmp(val, expected, len) == 0 && val[len] == '"';
}

}  // namespace

std::size_t format_ha_gateway_discovery_topic(const char* ha_prefix, const char* node_id,
                                              const char* component, const char* object_suffix, char* out,
                                              std::size_t out_size) noexcept {
    if (!out || out_size == 0) {
        return 0;
    }
    const int n = std::snprintf(out, out_size, "%s/%s/%s_%s/config", ha_prefix ? ha_prefix : "homeassistant",
                                component ? component : "sensor", node_id ? node_id : "knx_dali",
                                object_suffix ? object_suffix : "power");
    return (n > 0 && static_cast<std::size_t>(n) < out_size) ? static_cast<std::size_t>(n) : 0;
}

std::size_t format_ha_gateway_power_discovery_payload(const HaDeviceMeta& meta, char* out,
                                                      std::size_t out_size) noexcept {
    if (!out || out_size == 0) {
        return 0;
    }
    const char* base = meta.base_topic ? meta.base_topic : "knx_dali";
    const char* node = meta.node_id ? meta.node_id : "knx_dali";
    const char* ip = (meta.ipv4 && meta.ipv4[0] != '\0') ? meta.ipv4 : "192.168.4.1";
    const char* sw = meta.sw_version ? meta.sw_version : firmware::kFirmwareVersion;
    const int n =
        std::snprintf(out, out_size,
                      "{\"name\":\"Total Power\",\"uniq_id\":\"%s_total_power\","
                      "\"stat_t\":\"%s/gateway/state\",\"avty_t\":\"%s/status\","
                      "\"dev_cla\":\"power\",\"unit_of_meas\":\"W\",\"stat_cla\":\"measurement\","
                      "\"val_tpl\":\"{{ value_json.total_power_w }}\","
                      "\"dev\":{\"ids\":[\"%s\"],\"name\":\"KNX-DALI Gateway (%s)\","
                      "\"mdl\":\"KNX-DALI-GW\",\"mf\":\"OpenKNX-DALI\",\"sw\":\"%s\",\"cu\":\"http://%s/\"}}",
                      node, base, base, node, node, sw, ip);
    return (n > 0 && static_cast<std::size_t>(n) < out_size) ? static_cast<std::size_t>(n) : 0;
}

std::size_t format_ha_gateway_energy_discovery_payload(const HaDeviceMeta& meta, char* out,
                                                       std::size_t out_size) noexcept {
    if (!out || out_size == 0) {
        return 0;
    }
    const char* base = meta.base_topic ? meta.base_topic : "knx_dali";
    const char* node = meta.node_id ? meta.node_id : "knx_dali";
    const char* ip = (meta.ipv4 && meta.ipv4[0] != '\0') ? meta.ipv4 : "192.168.4.1";
    const char* sw = meta.sw_version ? meta.sw_version : firmware::kFirmwareVersion;
    const int n =
        std::snprintf(out, out_size,
                      "{\"name\":\"Total Energy\",\"uniq_id\":\"%s_total_energy\","
                      "\"stat_t\":\"%s/gateway/state\",\"avty_t\":\"%s/status\","
                      "\"dev_cla\":\"energy\",\"unit_of_meas\":\"Wh\",\"stat_cla\":\"total_increasing\","
                      "\"val_tpl\":\"{{ value_json.total_energy_wh }}\","
                      "\"dev\":{\"ids\":[\"%s\"],\"name\":\"KNX-DALI Gateway (%s)\","
                      "\"mdl\":\"KNX-DALI-GW\",\"mf\":\"OpenKNX-DALI\",\"sw\":\"%s\",\"cu\":\"http://%s/\"}}",
                      node, base, base, node, node, sw, ip);
    return (n > 0 && static_cast<std::size_t>(n) < out_size) ? static_cast<std::size_t>(n) : 0;
}

std::size_t format_ha_gateway_fault_discovery_payload(const HaDeviceMeta& meta, char* out,
                                                      std::size_t out_size) noexcept {
    if (!out || out_size == 0) {
        return 0;
    }
    const char* base = meta.base_topic ? meta.base_topic : "knx_dali";
    const char* node = meta.node_id ? meta.node_id : "knx_dali";
    const char* ip = (meta.ipv4 && meta.ipv4[0] != '\0') ? meta.ipv4 : "192.168.4.1";
    const char* sw = meta.sw_version ? meta.sw_version : firmware::kFirmwareVersion;
    const int n =
        std::snprintf(out, out_size,
                      "{\"name\":\"Gateway Problem\",\"uniq_id\":\"%s_fault\","
                      "\"stat_t\":\"%s/gateway/state\",\"avty_t\":\"%s/status\","
                      "\"dev_cla\":\"problem\",\"ent_cat\":\"diagnostic\","
                      "\"val_tpl\":\"{{ 'ON' if value_json.fault else 'OFF' }}\","
                      "\"dev\":{\"ids\":[\"%s\"],\"name\":\"KNX-DALI Gateway (%s)\","
                      "\"mdl\":\"KNX-DALI-GW\",\"mf\":\"OpenKNX-DALI\",\"sw\":\"%s\",\"cu\":\"http://%s/\"}}",
                      node, base, base, node, node, sw, ip);
    return (n > 0 && static_cast<std::size_t>(n) < out_size) ? static_cast<std::size_t>(n) : 0;
}

std::size_t format_mqtt_gateway_state(std::uint16_t total_power_deciwatts, std::uint32_t total_energy_wh,
                                      std::uint8_t active_lamps, std::uint8_t faulty_lamps, bool bus_fault,
                                      char* out, std::size_t out_size) noexcept {
    if (!out || out_size == 0) {
        return 0;
    }
    const bool any_fault = bus_fault || (faulty_lamps > 0);
    const int n = std::snprintf(
        out, out_size,
        "{\"total_power_w\":%u.%u,\"total_energy_wh\":%lu,\"active_lamps\":%u,\"faulty_lamps\":%u,"
        "\"fault\":%s}",
        static_cast<unsigned>(total_power_deciwatts / 10U),
        static_cast<unsigned>(total_power_deciwatts % 10U), static_cast<unsigned long>(total_energy_wh),
        static_cast<unsigned>(active_lamps), static_cast<unsigned>(faulty_lamps),
        any_fault ? "true" : "false");
    return (n > 0 && static_cast<std::size_t>(n) < out_size) ? static_cast<std::size_t>(n) : 0;
}

std::size_t format_ha_channel_discovery_topic(const char* ha_prefix, const char* node_id,
                                              dali::ShortAddress channel, char* out, std::size_t out_size,
                                              const char* component, const char* suffix) noexcept {
    if (!out || out_size == 0) {
        return 0;
    }
    const int n =
        std::snprintf(out, out_size, "%s/%s/%s_ch%02u%s/config", ha_prefix ? ha_prefix : "homeassistant",
                      component ? component : "light", node_id ? node_id : "knx_dali",
                      static_cast<unsigned>(channel.value()), suffix ? suffix : "");
    return (n > 0 && static_cast<std::size_t>(n) < out_size) ? static_cast<std::size_t>(n) : 0;
}

std::size_t format_ha_channel_discovery_payload(const char* base_topic, const char* node_id,
                                                dali::ShortAddress channel,
                                                std::optional<dali::ColourCapabilities> capabilities,
                                                char* out, std::size_t out_size, const char* ipv4,
                                                const char* sw_version, const char* custom_name) noexcept {
    if (!out || out_size == 0) {
        return 0;
    }
    const char* base = base_topic ? base_topic : "knx_dali";
    const char* node = node_id ? node_id : "knx_dali";
    const char* ip = (ipv4 && ipv4[0] != '\0') ? ipv4 : "192.168.4.1";
    const char* sw = sw_version ? sw_version : firmware::kFirmwareVersion;
    char modes[64]{"\"brightness\""};
    if (capabilities) {
        if (capabilities->rgbwaf_channels >= 4) {
            std::snprintf(modes, sizeof(modes), "\"rgbw\",\"color_temp\",\"brightness\"");
        } else if (capabilities->rgbwaf_channels >= 3) {
            std::snprintf(modes, sizeof(modes), "\"rgb\",\"color_temp\",\"brightness\"");
        } else if (capabilities->tc_capable) {
            std::snprintf(modes, sizeof(modes), "\"color_temp\",\"brightness\"");
        } else if (capabilities->xy_capable) {
            std::snprintf(modes, sizeof(modes), "\"xy\",\"brightness\"");
        }
    }
    const unsigned ch = channel.value();
    char dev_name[48]{};
    if (custom_name && custom_name[0] != '\0') {
        std::snprintf(dev_name, sizeof(dev_name), "%s (A%02u)", custom_name, ch);
    } else {
        std::snprintf(dev_name, sizeof(dev_name), "DALI Lamp A%02u", ch);
    }
    const int n =
        std::snprintf(out, out_size,
                      "{\"name\":\"Light\",\"uniq_id\":\"%s_dali_a%02u\","
                      "\"schema\":\"json\",\"cmd_t\":\"%s/light/ch%02u/set\","
                      "\"stat_t\":\"%s/light/ch%02u/state\",\"avty_t\":\"%s/status\","
                      "\"brightness\":true,\"brightness_scale\":254,"
                      "\"supported_color_modes\":[%s],\"min_mireds\":153,\"max_mireds\":500,"
                      "\"dev\":{\"ids\":[\"%s_ch%02u\"],\"via_device\":\"%s\","
                      "\"name\":\"%s\",\"mdl\":\"DALI Ballast A%02u\","
                      "\"mf\":\"OpenKNX-DALI\",\"sw\":\"%s\",\"cu\":\"http://%s/?ch=%u\"}}",
                      node, ch, base, ch, base, ch, base, modes, node, ch, node, dev_name, ch, sw, ip, ch);
    return (n > 0 && static_cast<std::size_t>(n) < out_size) ? static_cast<std::size_t>(n) : 0;
}

std::size_t format_ha_channel_power_discovery_payload(const HaDeviceMeta& meta, dali::ShortAddress channel,
                                                      char* out, std::size_t out_size,
                                                      const char* custom_name) noexcept {
    if (!out || out_size == 0) {
        return 0;
    }
    const char* base = meta.base_topic ? meta.base_topic : "knx_dali";
    const char* node = meta.node_id ? meta.node_id : "knx_dali";
    const char* ip = (meta.ipv4 && meta.ipv4[0] != '\0') ? meta.ipv4 : "192.168.4.1";
    const char* sw = meta.sw_version ? meta.sw_version : firmware::kFirmwareVersion;
    const unsigned ch = channel.value();
    char dev_name[48]{};
    if (custom_name && custom_name[0] != '\0') {
        std::snprintf(dev_name, sizeof(dev_name), "%s (A%02u)", custom_name, ch);
    } else {
        std::snprintf(dev_name, sizeof(dev_name), "DALI Lamp A%02u", ch);
    }
    const int n = std::snprintf(out, out_size,
                                "{\"name\":\"Power\",\"uniq_id\":\"%s_dali_a%02u_power\","
                                "\"stat_t\":\"%s/light/ch%02u/state\",\"avty_t\":\"%s/status\","
                                "\"dev_cla\":\"power\",\"unit_of_meas\":\"W\",\"stat_cla\":\"measurement\","
                                "\"val_tpl\":\"{{ value_json.power_w }}\","
                                "\"dev\":{\"ids\":[\"%s_ch%02u\"],\"via_device\":\"%s\","
                                "\"name\":\"%s\",\"mdl\":\"DALI Ballast A%02u\","
                                "\"mf\":\"OpenKNX-DALI\",\"sw\":\"%s\",\"cu\":\"http://%s/?ch=%u\"}}",
                                node, ch, base, ch, base, node, ch, node, dev_name, ch, sw, ip, ch);
    return (n > 0 && static_cast<std::size_t>(n) < out_size) ? static_cast<std::size_t>(n) : 0;
}

std::size_t format_ha_channel_fault_discovery_payload(const HaDeviceMeta& meta, dali::ShortAddress channel,
                                                      char* out, std::size_t out_size,
                                                      const char* custom_name) noexcept {
    if (!out || out_size == 0) {
        return 0;
    }
    const char* base = meta.base_topic ? meta.base_topic : "knx_dali";
    const char* node = meta.node_id ? meta.node_id : "knx_dali";
    const char* ip = (meta.ipv4 && meta.ipv4[0] != '\0') ? meta.ipv4 : "192.168.4.1";
    const char* sw = meta.sw_version ? meta.sw_version : firmware::kFirmwareVersion;
    const unsigned ch = channel.value();
    char dev_name[48]{};
    if (custom_name && custom_name[0] != '\0') {
        std::snprintf(dev_name, sizeof(dev_name), "%s (A%02u)", custom_name, ch);
    } else {
        std::snprintf(dev_name, sizeof(dev_name), "DALI Lamp A%02u", ch);
    }
    const int n = std::snprintf(out, out_size,
                                "{\"name\":\"Error\",\"uniq_id\":\"%s_dali_a%02u_fault\","
                                "\"stat_t\":\"%s/light/ch%02u/state\",\"avty_t\":\"%s/status\","
                                "\"dev_cla\":\"problem\",\"ent_cat\":\"diagnostic\","
                                "\"val_tpl\":\"{{ 'ON' if value_json.fault else 'OFF' }}\","
                                "\"dev\":{\"ids\":[\"%s_ch%02u\"],\"via_device\":\"%s\","
                                "\"name\":\"%s\",\"mdl\":\"DALI Ballast A%02u\","
                                "\"mf\":\"OpenKNX-DALI\",\"sw\":\"%s\",\"cu\":\"http://%s/?ch=%u\"}}",
                                node, ch, base, ch, base, node, ch, node, dev_name, ch, sw, ip, ch);
    return (n > 0 && static_cast<std::size_t>(n) < out_size) ? static_cast<std::size_t>(n) : 0;
}

std::size_t format_ha_group_discovery_topic(const char* ha_prefix, const char* node_id, dali::Group group,
                                            char* out, std::size_t out_size) noexcept {
    if (!out || out_size == 0) {
        return 0;
    }
    const int n =
        std::snprintf(out, out_size, "%s/light/%s_grp%02u/config", ha_prefix ? ha_prefix : "homeassistant",
                      node_id ? node_id : "knx_dali", static_cast<unsigned>(group.value()));
    return (n > 0 && static_cast<std::size_t>(n) < out_size) ? static_cast<std::size_t>(n) : 0;
}

std::size_t format_ha_group_discovery_payload(const char* base_topic, const char* node_id, dali::Group group,
                                              char* out, std::size_t out_size, const char* ipv4,
                                              const char* sw_version, const char* custom_name) noexcept {
    if (!out || out_size == 0) {
        return 0;
    }
    const char* base = base_topic ? base_topic : "knx_dali";
    const char* node = node_id ? node_id : "knx_dali";
    const char* ip = (ipv4 && ipv4[0] != '\0') ? ipv4 : "192.168.4.1";
    const char* sw = sw_version ? sw_version : firmware::kFirmwareVersion;
    const unsigned g = group.value();
    char grp_name[48]{};
    if (custom_name && custom_name[0] != '\0') {
        std::snprintf(grp_name, sizeof(grp_name), "%s (G%02u)", custom_name, g);
    } else {
        std::snprintf(grp_name, sizeof(grp_name), "DALI Group G%02u", g);
    }
    const int n =
        std::snprintf(out, out_size,
                      "{\"name\":\"%s\",\"uniq_id\":\"%s_dali_g%02u\","
                      "\"schema\":\"json\",\"cmd_t\":\"%s/light/grp%02u/set\","
                      "\"stat_t\":\"%s/light/grp%02u/state\",\"avty_t\":\"%s/status\","
                      "\"brightness\":true,\"brightness_scale\":254,"
                      "\"supported_color_modes\":[\"brightness\"],"
                      "\"dev\":{\"ids\":[\"%s\"],\"name\":\"KNX-DALI Gateway (%s)\","
                      "\"mdl\":\"KNX-DALI-GW\",\"mf\":\"OpenKNX-DALI\",\"sw\":\"%s\",\"cu\":\"http://%s/\"}}",
                      grp_name, node, g, base, g, base, g, base, node, node, sw, ip);
    return (n > 0 && static_cast<std::size_t>(n) < out_size) ? static_cast<std::size_t>(n) : 0;
}

std::size_t format_mqtt_channel_state(const lighting::State& state, const lighting::ColourState& colour,
                                      bool faulty, char* out, std::size_t out_size,
                                      std::uint16_t power_deciwatts, std::uint32_t energy_wh) noexcept {
    if (!out || out_size == 0) {
        return 0;
    }
    const std::uint8_t arc = state.level ? state.level->value() : 0U;
    const bool is_on = arc > 0;
    const unsigned pw_int = power_deciwatts / 10U;
    const unsigned pw_frac = power_deciwatts % 10U;
    if (colour.kind == lighting::ColourKind::Temperature && colour.temperature_kelvin > 0) {
        const unsigned mireds = 1'000'000U / colour.temperature_kelvin;
        const int n =
            std::snprintf(out, out_size,
                          "{\"state\":\"%s\",\"brightness\":%u,\"color_mode\":\"color_temp\","
                          "\"color_temp\":%u,\"kelvin\":%u,\"fault\":%s,\"power_w\":%u.%u,\"energy_wh\":%lu,"
                          "\"source\":\"%s\"}",
                          is_on ? "ON" : "OFF", static_cast<unsigned>(arc), mireds,
                          static_cast<unsigned>(colour.temperature_kelvin), faulty ? "true" : "false", pw_int,
                          pw_frac, static_cast<unsigned long>(energy_wh), source_str(state.source));
        return (n > 0 && static_cast<std::size_t>(n) < out_size) ? static_cast<std::size_t>(n) : 0;
    }
    if (colour.kind == lighting::ColourKind::Rgb || colour.kind == lighting::ColourKind::Rgbw) {
        const int n = std::snprintf(out, out_size,
                                    "{\"state\":\"%s\",\"brightness\":%u,\"color_mode\":\"%s\","
                                    "\"color\":{\"r\":%u,\"g\":%u,\"b\":%u,\"w\":%u},\"fault\":%s,"
                                    "\"power_w\":%u.%u,\"energy_wh\":%lu,\"source\":\"%s\"}",
                                    is_on ? "ON" : "OFF", static_cast<unsigned>(arc),
                                    colour.kind == lighting::ColourKind::Rgbw ? "rgbw" : "rgb",
                                    static_cast<unsigned>(colour.r), static_cast<unsigned>(colour.g),
                                    static_cast<unsigned>(colour.b), static_cast<unsigned>(colour.w),
                                    faulty ? "true" : "false", pw_int, pw_frac,
                                    static_cast<unsigned long>(energy_wh), source_str(state.source));
        return (n > 0 && static_cast<std::size_t>(n) < out_size) ? static_cast<std::size_t>(n) : 0;
    }
    const int n =
        std::snprintf(out, out_size,
                      "{\"state\":\"%s\",\"brightness\":%u,\"color_mode\":\"brightness\",\"fault\":%s,"
                      "\"power_w\":%u.%u,\"energy_wh\":%lu,\"source\":\"%s\"}",
                      is_on ? "ON" : "OFF", static_cast<unsigned>(arc), faulty ? "true" : "false", pw_int,
                      pw_frac, static_cast<unsigned long>(energy_wh), source_str(state.source));
    return (n > 0 && static_cast<std::size_t>(n) < out_size) ? static_cast<std::size_t>(n) : 0;
}

std::optional<control::Command> parse_mqtt_command(const char* base_topic, const char* topic,
                                                   const char* payload, std::size_t payload_len) noexcept {
    if (!base_topic || !topic || !payload || payload_len == 0 || payload_len > 511) {
        return std::nullopt;
    }
    const std::size_t base_len = std::strlen(base_topic);
    if (std::strncmp(topic, base_topic, base_len) != 0 || topic[base_len] != '/') {
        return std::nullopt;
    }
    const char* sub = topic + base_len + 1;

    char body[512]{};
    std::memcpy(body, payload, payload_len);
    body[payload_len] = '\0';

    if (std::strcmp(sub, "scene/set") == 0) {
        const auto sc = parse_json_int(body, "scene").value_or(std::atoi(body));
        if (const auto scene = dali::Scene::from(static_cast<unsigned>(sc))) {
            return control::RecallScene{*scene};
        }
        return std::nullopt;
    }

    dali::Address target = dali::Address::broadcast();
    if (std::strncmp(sub, "light/ch", 8) == 0) {
        char* end = nullptr;
        const unsigned ch = static_cast<unsigned>(std::strtoul(sub + 8, &end, 10));
        if (!end || std::strcmp(end, "/set") != 0) {
            return std::nullopt;
        }
        const auto s = dali::ShortAddress::from(ch);
        if (!s) {
            return std::nullopt;
        }
        target = dali::Address::short_address(*s);
    } else if (std::strncmp(sub, "light/grp", 9) == 0) {
        char* end = nullptr;
        const unsigned grp = static_cast<unsigned>(std::strtoul(sub + 9, &end, 10));
        if (!end || std::strcmp(end, "/set") != 0) {
            return std::nullopt;
        }
        const auto g = dali::Group::from(grp);
        if (!g) {
            return std::nullopt;
        }
        target = dali::Address::group(*g);
    } else if (std::strcmp(sub, "light/broadcast/set") != 0) {
        return std::nullopt;
    }

    if (json_string_equals(body, "state", "OFF")) {
        return control::SwitchOff{target};
    }

    if (const char* color_obj = find_json_key(body, "color")) {
        const auto r = parse_json_int(color_obj, "r");
        const auto g = parse_json_int(color_obj, "g");
        const auto b = parse_json_int(color_obj, "b");
        const auto w = parse_json_int(color_obj, "w");
        if (r && g && b && w) {
            return control::SetRgbw{target,
                                    static_cast<std::uint8_t>(*r),
                                    static_cast<std::uint8_t>(*g),
                                    static_cast<std::uint8_t>(*b),
                                    static_cast<std::uint8_t>(*w),
                                    0x0FU};
        }
        if (r && g && b) {
            return control::SetRgb{target, static_cast<std::uint8_t>(*r), static_cast<std::uint8_t>(*g),
                                   static_cast<std::uint8_t>(*b)};
        }
    }

    if (const auto ct = parse_json_int(body, "color_temp")) {
        if (*ct >= 100 && *ct <= 1000) {
            const auto kelvin = static_cast<std::uint16_t>(1'000'000 / *ct);
            return control::SetColourTemperature{target, kelvin};
        }
    }
    if (const auto kelvin = parse_json_int(body, "kelvin")) {
        if (*kelvin >= 1500 && *kelvin <= 10000) {
            return control::SetColourTemperature{target, static_cast<std::uint16_t>(*kelvin)};
        }
    }

    if (const auto bri = parse_json_int(body, "brightness")) {
        const unsigned clamped = *bri < 0 ? 0U : (*bri > 254 ? 254U : static_cast<unsigned>(*bri));
        if (const auto lvl = dali::ArcLevel::from(clamped)) {
            return control::SetLevel{target, *lvl};
        }
    }

    if (json_string_equals(body, "state", "ON")) {
        return control::SwitchOn{target};
    }
    return std::nullopt;
}

}  // namespace gateway::network
