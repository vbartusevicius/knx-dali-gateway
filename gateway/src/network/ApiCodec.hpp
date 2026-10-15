#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "control/Command.hpp"
#include "network/NetworkConfig.hpp"

namespace gateway::network {

std::optional<control::Command> parse_api_command_json(const char* json, std::size_t len) noexcept;

bool apply_network_config_json(NetworkConfig& cfg, const char* json, std::size_t len) noexcept;

std::size_t format_network_config_json(const NetworkConfig& cfg, char* out, std::size_t out_size) noexcept;

std::size_t format_error_catalog_json(char* out, std::size_t out_size) noexcept;

}
