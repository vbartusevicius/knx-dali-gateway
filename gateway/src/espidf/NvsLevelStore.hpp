#pragma once

#include <cstdint>
#include <map>
#include <string>

#include <nvs.h>

#include "storage/LevelStore.hpp"

namespace gateway::espidf {

// storage::LevelStore backed by an already-open NVS handle (namespace
// "targets_v1"). save() only stages via nvs_set_u8; commit() calls nvs_commit
// once per batch and retries after failures.
class NvsLevelStore final : public storage::LevelStore {
public:
    explicit NvsLevelStore(nvs_handle_t handle) noexcept;
    ~NvsLevelStore() override;
    NvsLevelStore(const NvsLevelStore&) = delete;
    NvsLevelStore& operator=(const NvsLevelStore&) = delete;

    bool available() const noexcept;
    storage::TargetRead load(storage::Slot slot) override;
    bool save(storage::Slot slot, dali::ArcLevel level) override;
    bool commit() override;

private:
    nvs_handle_t handle_;
    std::map<std::string, std::uint8_t> staged_;
    bool dirty_ = false;
};

}
