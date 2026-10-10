#pragma once

// Host fake of the ESP-IDF NVS C functions backed by maps, letting the real
// NvsTargetStore run in native tests, including commit failures.

#include <cstdint>
#include <map>
#include <string>

namespace gateway::testing {

struct NvsSdkState {
    std::map<std::string, std::uint8_t> staged;
    std::map<std::string, std::uint8_t> committed;
    bool reads_succeed = true;
    bool writes_succeed = true;
    bool commits_succeed = true;
    unsigned writes = 0;
    unsigned commits = 0;
    unsigned closes = 0;
};

NvsSdkState& nvs_state();
void reset_nvs();

}
