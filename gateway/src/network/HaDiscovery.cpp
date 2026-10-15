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

std::size_t format_ha_channel_discovery_topic(const char* ha_prefix, const char* node_id,
                                              dali::ShortAddress channel, char* out,
                                              std::size_t out_size) noexcept {
    if (!out || out_size == 0) {
        return 0;
    }
    const int n =
        std::snprintf(out, out_size, "%s/light/%s_ch%02u/config", ha_prefix ? ha_prefix : "homeassistant",
                      node_id ? node_id : "knx_dali", static_cast<unsigned>(channel.value()));
    return (n > 0 && static_cast<std::size_t>(n) < out_size) ? static_cast<std::size_t>(n) : 0;
}

std::size_t format_ha_channel_discovery_payload(const char* base_topic, const char* node_id,
                                                dali::ShortAddress channel,
                                                std::optional<dali::ColourCapabilities> capabilities,
                                                char* out, std::size_t out_size) noexcept {
    if (!out || out_size == 0) {
        return 0;
    }
    const char* base = base_topic ? base_topic : "knx_dali";
    const char* node = node_id ? node_id : "knx_dali";
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
    const int n = std::snprintf(out, out_size,
                                "{\"name\":\"DALI Lamp A%02u\",\"uniq_id\":\"%s_dali_a%02u\","
                                "\"schema\":\"json\",\"cmd_t\":\"%s/light/ch%02u/set\","
                                "\"stat_t\":\"%s/light/ch%02u/state\",\"avty_t\":\"%s/status\","
                                "\"brightness\":true,\"brightness_scale\":254,"
                                "\"supported_color_modes\":[%s],\"min_mireds\":153,\"max_mireds\":500,"
                                "\"dev\":{\"ids\":[\"%s\"],\"name\":\"KNX-DALI "
                                "Gateway\",\"mdl\":\"KNX-DALI-GW\",\"mf\":\"OpenKNX-DALI\"}}",
                                ch, node, ch, base, ch, base, ch, base, modes, node);
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
                                              char* out, std::size_t out_size) noexcept {
    if (!out || out_size == 0) {
        return 0;
    }
    const char* base = base_topic ? base_topic : "knx_dali";
    const char* node = node_id ? node_id : "knx_dali";
    const unsigned g = group.value();
    const int n = std::snprintf(out, out_size,
                                "{\"name\":\"DALI Group G%02u\",\"uniq_id\":\"%s_dali_g%02u\","
                                "\"schema\":\"json\",\"cmd_t\":\"%s/light/grp%02u/set\","
                                "\"stat_t\":\"%s/light/grp%02u/state\",\"avty_t\":\"%s/status\","
                                "\"brightness\":true,\"brightness_scale\":254,"
                                "\"supported_color_modes\":[\"brightness\"],"
                                "\"dev\":{\"ids\":[\"%s\"],\"name\":\"KNX-DALI "
                                "Gateway\",\"mdl\":\"KNX-DALI-GW\",\"mf\":\"OpenKNX-DALI\"}}",
                                g, node, g, base, g, base, g, base, node);
    return (n > 0 && static_cast<std::size_t>(n) < out_size) ? static_cast<std::size_t>(n) : 0;
}

std::size_t format_mqtt_channel_state(const lighting::State& state, const lighting::ColourState& colour,
                                      bool faulty, char* out, std::size_t out_size) noexcept {
    if (!out || out_size == 0) {
        return 0;
    }
    const std::uint8_t arc = state.level ? state.level->value() : 0U;
    const bool is_on = arc > 0;
    if (colour.kind == lighting::ColourKind::Temperature && colour.temperature_kelvin > 0) {
        const unsigned mireds = 1'000'000U / colour.temperature_kelvin;
        const int n = std::snprintf(out, out_size,
                                    "{\"state\":\"%s\",\"brightness\":%u,\"color_mode\":\"color_temp\","
                                    "\"color_temp\":%u,\"kelvin\":%u,\"fault\":%s,\"source\":\"%s\"}",
                                    is_on ? "ON" : "OFF", static_cast<unsigned>(arc), mireds,
                                    static_cast<unsigned>(colour.temperature_kelvin),
                                    faulty ? "true" : "false", source_str(state.source));
        return (n > 0 && static_cast<std::size_t>(n) < out_size) ? static_cast<std::size_t>(n) : 0;
    }
    if (colour.kind == lighting::ColourKind::Rgb || colour.kind == lighting::ColourKind::Rgbw) {
        const int n = std::snprintf(
            out, out_size,
            "{\"state\":\"%s\",\"brightness\":%u,\"color_mode\":\"%s\","
            "\"color\":{\"r\":%u,\"g\":%u,\"b\":%u,\"w\":%u},\"fault\":%s,\"source\":\"%s\"}",
            is_on ? "ON" : "OFF", static_cast<unsigned>(arc),
            colour.kind == lighting::ColourKind::Rgbw ? "rgbw" : "rgb", static_cast<unsigned>(colour.r),
            static_cast<unsigned>(colour.g), static_cast<unsigned>(colour.b), static_cast<unsigned>(colour.w),
            faulty ? "true" : "false", source_str(state.source));
        return (n > 0 && static_cast<std::size_t>(n) < out_size) ? static_cast<std::size_t>(n) : 0;
    }
    const int n = std::snprintf(
        out, out_size,
        "{\"state\":\"%s\",\"brightness\":%u,\"color_mode\":\"brightness\",\"fault\":%s,\"source\":\"%s\"}",
        is_on ? "ON" : "OFF", static_cast<unsigned>(arc), faulty ? "true" : "false",
        source_str(state.source));
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

    if (json_string_equals(body, "state", "OFF") || std::strcmp(body, "OFF") == 0) {
        return control::SwitchOff{target};
    }
    if (const auto r = parse_json_int(body, "r")) {
        const auto g = parse_json_int(body, "g").value_or(0);
        const auto b = parse_json_int(body, "b").value_or(0);
        if (const auto w = parse_json_int(body, "w")) {
            return control::SetRgbw{target,
                                    static_cast<std::uint8_t>(*r),
                                    static_cast<std::uint8_t>(g),
                                    static_cast<std::uint8_t>(b),
                                    static_cast<std::uint8_t>(*w),
                                    0x0FU};
        }
        return control::SetRgb{target, static_cast<std::uint8_t>(*r), static_cast<std::uint8_t>(g),
                               static_cast<std::uint8_t>(b)};
    }
    if (const auto kelvin = parse_json_int(body, "kelvin")) {
        if (*kelvin >= 1500 && *kelvin <= 10000) {
            return control::SetColourTemperature{target, static_cast<std::uint16_t>(*kelvin)};
        }
    }
    if (const auto mireds = parse_json_int(body, "color_temp")) {
        if (*mireds >= 100 && *mireds <= 1000) {
            return control::SetColourTemperature{target, static_cast<std::uint16_t>(1'000'000 / *mireds)};
        }
    }
    if (const auto bri = parse_json_int(body, "brightness")) {
        const unsigned clamped = *bri < 0 ? 0U : (*bri > 254 ? 254U : static_cast<unsigned>(*bri));
        if (const auto arc = dali::ArcLevel::from(clamped)) {
            return control::SetLevel{target, *arc};
        }
    }
    if (json_string_equals(body, "state", "ON") || std::strcmp(body, "ON") == 0) {
        return control::SwitchOn{target};
    }
    return std::nullopt;
}

}
