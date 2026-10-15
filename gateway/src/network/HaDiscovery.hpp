#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "control/Command.hpp"
#include "dali/ColourControl.hpp"
#include "dali/Group.hpp"
#include "dali/ShortAddress.hpp"
#include "lighting/LightingService.hpp"

namespace gateway::network {

std::size_t format_ha_channel_discovery_topic(const char* ha_prefix, const char* node_id,
                                              dali::ShortAddress channel, char* out,
                                              std::size_t out_size) noexcept;

std::size_t format_ha_channel_discovery_payload(const char* base_topic, const char* node_id,
                                                dali::ShortAddress channel,
                                                std::optional<dali::ColourCapabilities> capabilities,
                                                char* out, std::size_t out_size) noexcept;

std::size_t format_ha_group_discovery_topic(const char* ha_prefix, const char* node_id, dali::Group group,
                                            char* out, std::size_t out_size) noexcept;

std::size_t format_ha_group_discovery_payload(const char* base_topic, const char* node_id, dali::Group group,
                                              char* out, std::size_t out_size) noexcept;

std::size_t format_mqtt_channel_state(const lighting::State& state, const lighting::ColourState& colour,
                                      bool faulty, char* out, std::size_t out_size) noexcept;

std::optional<control::Command> parse_mqtt_command(const char* base_topic, const char* topic,
                                                   const char* payload, std::size_t payload_len) noexcept;

}
