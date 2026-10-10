#pragma once

// Host fake of the Espressif DALI C functions, letting the real DaliTransport
// adapter run in native tests. DaliSdkState records calls and scripts replies.

#include <deque>
#include <vector>
#include <dali.h>

namespace gateway::testing {

struct RecordedTransaction {
    dali_master_transaction_config_t config;
    bool expects_reply;
};

struct DaliSdkState {
    esp_err_t transaction_error = ESP_OK;
    int reply = 0;
    std::deque<int> replies;
    std::vector<RecordedTransaction> transactions;
    unsigned deletions = 0;
};

DaliSdkState& dali_state();
dali_master_handle_t dali_handle();
void reset_dali();

}
