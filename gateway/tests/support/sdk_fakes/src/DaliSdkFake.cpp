#include "DaliSdkFake.hpp"

struct dali_master_t {};

namespace gateway::testing {

DaliSdkState& dali_state() {
    static DaliSdkState state;
    return state;
}

dali_master_handle_t dali_handle() {
    static dali_master_t handle;
    return &handle;
}

void reset_dali() {
    dali_state() = {};
}

}

extern "C" esp_err_t dali_master_do_transaction(dali_master_handle_t handle,
                                               const dali_master_transaction_config_t* config, int* result) {
    auto& state = gateway::testing::dali_state();
    if (!handle) {
        return ESP_FAIL;
    }
    state.transactions.push_back({*config, result != nullptr});
    if (result) {
        *result = state.replies.empty() ? state.reply : state.replies.front();
        if (!state.replies.empty()) {
            state.replies.pop_front();
        }
    }
    return state.transaction_error;
}

extern "C" esp_err_t dali_del_master(dali_master_handle_t handle) {
    if (handle) {
        ++gateway::testing::dali_state().deletions;
    }
    return ESP_OK;
}
