#pragma once

#ifndef GATEWAY_FW_VERSION
#define GATEWAY_FW_VERSION "00.00.00.0000"
#endif

namespace gateway::firmware {

// Firmware version in yy.mm.dd.hhmm format, injected at build time via
// -DGATEWAY_FW_VERSION (from FW_VERSION env var or current UTC timestamp)
// without modifying tracked files.
inline constexpr const char* kFirmwareVersion = GATEWAY_FW_VERSION;

}  // namespace gateway::firmware
