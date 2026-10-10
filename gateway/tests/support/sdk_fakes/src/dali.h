#pragma once

// Minimal native fake of espressif/dali driver's public headers, used by tests
// in place of the real SDK. Keep names/values in sync with
// managed_components/espressif__dali/include/dali.h + dali_system_components.h
// + dali_command.h.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;
typedef int gpio_num_t;

#define ESP_OK 0
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_FAIL -1

#define DALI_TX_TIMEOUT_MS 110
#define DALI_RESULT_NO_REPLY (-1)
#define DALI_RESULT_IS_VALID(r) ((r) >= 0)

typedef enum {
    DALI_ADDR_SHORT = 0,
    DALI_ADDR_GROUP = 1,
    DALI_ADDR_BROADCAST = 2,
    DALI_ADDR_SPECIAL = 3,
} dali_addr_type_t;

typedef enum {
    DALI_COMMISSION_ALL = 0,
    DALI_COMMISSION_UNADDRESSED = 1,
} dali_commission_mode_t;

typedef struct dali_master_t *dali_master_handle_t;

typedef struct {
    gpio_num_t rx_gpio;
    gpio_num_t tx_gpio;
    bool invert_tx;
    bool invert_rx;
} dali_master_config_t;

typedef struct {
    uint32_t mem_block_symbols;
} dali_master_rmt_config_t;

typedef struct {
    dali_addr_type_t addr_type;
    uint8_t addr;
    bool is_cmd;
    uint8_t command;
    bool send_twice;
    int tx_timeout_ms;
} dali_master_transaction_config_t;

// Gear commands (IEC 62386-102), mirrored from dali_command.h.
#define DALI_CMD_OFF 0x00U
#define DALI_CMD_UP 0x01U
#define DALI_CMD_DOWN 0x02U
#define DALI_CMD_STEP_UP 0x03U
#define DALI_CMD_STEP_DOWN 0x04U
#define DALI_CMD_RECALL_MAX_LEVEL 0x05U
#define DALI_CMD_RECALL_MIN_LEVEL 0x06U
#define DALI_CMD_STEP_DOWN_AND_OFF 0x07U
#define DALI_CMD_ON_AND_STEP_UP 0x08U
#define DALI_CMD_ENABLE_DAPC_SEQ 0x09U
#define DALI_CMD_GO_TO_SCENE_0 0x10U
#define DALI_CMD_GO_TO_SCENE_15 0x1FU
#define DALI_CMD_RESET 0x20U
#define DALI_CMD_STORE_ACTUAL_LEVEL 0x21U
#define DALI_CMD_STORE_DTR_AS_SCENE_0 0x40U
#define DALI_CMD_STORE_DTR_AS_SCENE_15 0x4FU
#define DALI_CMD_ADD_TO_GROUP_0 0x60U
#define DALI_CMD_ADD_TO_GROUP_15 0x6FU
#define DALI_CMD_REMOVE_FROM_GROUP_0 0x70U
#define DALI_CMD_REMOVE_FROM_GROUP_15 0x7FU
#define DALI_CMD_QUERY_STATUS 0x90U
#define DALI_CMD_QUERY_MISSING_SHORT_ADDR 0x96U
#define DALI_CMD_QUERY_ACTUAL_LEVEL 0xA0U
#define DALI_CMD_QUERY_GROUPS_0_7 0xC0U
#define DALI_CMD_QUERY_GROUPS_8_15 0xC1U

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t dali_new_master_rmt(const dali_master_config_t *config, const dali_master_rmt_config_t *rmt_config,
                              dali_master_handle_t *handle);
esp_err_t dali_del_master(dali_master_handle_t handle);
esp_err_t dali_master_do_transaction(dali_master_handle_t handle, const dali_master_transaction_config_t *config,
                                     int *result);
esp_err_t dali_master_do_raw_transaction(dali_master_handle_t handle, const uint8_t *tx_buf, size_t tx_len,
                                         bool send_twice, int tx_timeout_ms, int *result);

#ifdef __cplusplus
}
#endif
