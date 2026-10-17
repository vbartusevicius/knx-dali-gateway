#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "control/Command.hpp"
#include "dali/ColourControl.hpp"
#include "dali/Group.hpp"
#include "dali/ShortAddress.hpp"
#include "firmware/Version.hpp"
#include "lighting/LightingService.hpp"

namespace gateway::network {

struct HaDeviceMeta {
    const char* base_topic = "knx_dali";
    const char* node_id = "knx_dali";
    const char* ipv4 = "192.168.1.42";
    const char* sw_version = firmware::kFirmwareVersion;
};

// Central Gateway Device discovery (total power W, total energy Wh, gateway fault binary_sensor)
std::size_t format_ha_gateway_discovery_topic(const char* ha_prefix, const char* node_id,
                                              const char* component, const char* object_suffix, char* out,
                                              std::size_t out_size) noexcept;

std::size_t format_ha_gateway_power_discovery_payload(const HaDeviceMeta& meta, char* out,
                                                      std::size_t out_size) noexcept;

std::size_t format_ha_gateway_energy_discovery_payload(const HaDeviceMeta& meta, char* out,
                                                       std::size_t out_size) noexcept;

std::size_t format_ha_gateway_fault_discovery_payload(const HaDeviceMeta& meta, char* out,
                                                      std::size_t out_size) noexcept;

std::size_t format_mqtt_gateway_state(std::uint16_t total_power_deciwatts, std::uint32_t total_energy_wh,
                                      std::uint8_t active_lamps, std::uint8_t faulty_lamps, bool bus_fault,
                                      char* out, std::size_t out_size) noexcept;

// Per-lamp child device discovery (linked via `via_device` to the central gateway)
std::size_t format_ha_channel_discovery_topic(const char* ha_prefix, const char* node_id,
                                              dali::ShortAddress channel, char* out, std::size_t out_size,
                                              const char* component = "light",
                                              const char* suffix = "") noexcept;

std::size_t format_ha_channel_discovery_payload(const char* base_topic, const char* node_id,
                                                dali::ShortAddress channel,
                                                std::optional<dali::ColourCapabilities> capabilities,
                                                char* out, std::size_t out_size,
                                                const char* ipv4 = "192.168.1.42",
                                                const char* sw_version = firmware::kFirmwareVersion,
                                                const char* custom_name = nullptr) noexcept;

std::size_t format_ha_channel_power_discovery_payload(const HaDeviceMeta& meta, dali::ShortAddress channel,
                                                      char* out, std::size_t out_size,
                                                      const char* custom_name = nullptr) noexcept;

std::size_t format_ha_channel_fault_discovery_payload(const HaDeviceMeta& meta, dali::ShortAddress channel,
                                                      char* out, std::size_t out_size,
                                                      const char* custom_name = nullptr) noexcept;

std::size_t format_ha_group_discovery_topic(const char* ha_prefix, const char* node_id, dali::Group group,
                                            char* out, std::size_t out_size) noexcept;

std::size_t format_ha_group_discovery_payload(const char* base_topic, const char* node_id, dali::Group group,
                                              char* out, std::size_t out_size,
                                              const char* ipv4 = "192.168.1.42",
                                              const char* sw_version = firmware::kFirmwareVersion,
                                              const char* custom_name = nullptr) noexcept;

std::size_t format_mqtt_channel_state(const lighting::State& state, const lighting::ColourState& colour,
                                      bool faulty, char* out, std::size_t out_size,
                                      std::uint16_t power_deciwatts = 0,
                                      std::uint32_t energy_wh = 0) noexcept;

std::optional<control::Command> parse_mqtt_command(const char* base_topic, const char* topic,
                                                   const char* payload, std::size_t payload_len) noexcept;

}  // namespace gateway::network
