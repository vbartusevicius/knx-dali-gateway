#include "NvsSdkFake.hpp"

#include <cstdint>

#include <nvs.h>

namespace gateway::testing {

NvsSdkState& nvs_state() {
    static NvsSdkState state;
    return state;
}

void reset_nvs() {
    nvs_state() = {};
}

}

extern "C" esp_err_t nvs_get_u8(nvs_handle_t handle, const char* key, std::uint8_t* value) {
    auto& state = gateway::testing::nvs_state();
    if (!handle || !state.reads_succeed) {
        return ESP_FAIL;
    }
    const auto staged = state.staged.find(key);
    if (staged != state.staged.end()) {
        *value = staged->second;
        return ESP_OK;
    }
    const auto committed = state.committed.find(key);
    if (committed == state.committed.end()) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    *value = committed->second;
    return ESP_OK;
}

extern "C" esp_err_t nvs_set_u8(nvs_handle_t handle, const char* key, std::uint8_t value) {
    auto& state = gateway::testing::nvs_state();
    ++state.writes;
    if (!handle || !state.writes_succeed) {
        return ESP_FAIL;
    }
    state.staged[key] = value;
    return ESP_OK;
}

extern "C" esp_err_t nvs_commit(nvs_handle_t handle) {
    auto& state = gateway::testing::nvs_state();
    ++state.commits;
    if (!handle || !state.commits_succeed) {
        return ESP_FAIL;
    }
    for (const auto& entry : state.staged) {
        state.committed[entry.first] = entry.second;
    }
    state.staged.clear();
    return ESP_OK;
}

extern "C" void nvs_close(nvs_handle_t handle) {
    if (handle) {
        ++gateway::testing::nvs_state().closes;
    }
}
