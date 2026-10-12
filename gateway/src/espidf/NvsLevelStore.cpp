#include "espidf/NvsLevelStore.hpp"

#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <string>

#include "dali/ArcLevel.hpp"
#include "nvs.h"
#include "storage/LevelStore.hpp"

namespace gateway::espidf {
namespace {

// Slot keys: targets keep the "arc_NN" format from TargetStore; last-active
// levels use "aNN" (short), "gNN" (group) and "b" (broadcast).
std::string slot_key(storage::Slot slot) {
    char key[8] = {};
    const char* pattern = "b";
    int index = -1;
    switch (slot.kind) {
    case storage::SlotKind::Target:
        pattern = "arc_%02u";
        index = slot.address.as_short() ? slot.address.as_short()->value() : -1;
        break;
    case storage::SlotKind::LastActive:
        if (slot.address.as_short()) {
            pattern = "a%02u";
            index = slot.address.as_short()->value();
        } else if (slot.address.as_group()) {
            pattern = "g%02u";
            index = slot.address.as_group()->value();
        }
        break;
    }
    if (index < 0) {
        if (std::string(pattern).find('%') != std::string::npos) {
            return {};
        }
        return pattern;
    }
    std::snprintf(key, sizeof(key), pattern, static_cast<unsigned>(index));
    return key;
}

}

NvsLevelStore::NvsLevelStore(nvs_handle_t handle) noexcept : handle_(handle) {}

NvsLevelStore::~NvsLevelStore() {
    if (handle_) {
        nvs_close(handle_);
    }
}

bool NvsLevelStore::available() const noexcept {
    return handle_ != 0;
}

storage::TargetRead NvsLevelStore::load(storage::Slot slot) {
    if (!available() || slot_key(slot).empty()) {
        return {storage::ReadStatus::Failure, std::nullopt};
    }
    const auto key = slot_key(slot);
    const auto staged = staged_.find(key);
    const std::uint8_t* stored = nullptr;
    std::uint8_t raw = 0;
    if (staged != staged_.end()) {
        stored = &staged->second;
    } else {
        const auto error = nvs_get_u8(handle_, key.c_str(), &raw);
        if (error == ESP_ERR_NVS_NOT_FOUND) {
            return {storage::ReadStatus::NotFound, std::nullopt};
        }
        if (error != ESP_OK) {
            return {storage::ReadStatus::Failure, std::nullopt};
        }
        stored = &raw;
    }
    const auto level = dali::ArcLevel::from(*stored);
    return {level ? storage::ReadStatus::Ok : storage::ReadStatus::Failure, level};
}

bool NvsLevelStore::save(storage::Slot slot, dali::ArcLevel level) {
    if (!available()) {
        return false;
    }
    const auto key = slot_key(slot);
    if (key.empty()) {
        return false;
    }
    const auto staged = staged_.find(key);
    if (staged != staged_.end()) {
        if (staged->second == level.value()) {
            return true;
        }
    } else {
        std::uint8_t raw = 0;
        const auto error = nvs_get_u8(handle_, key.c_str(), &raw);
        if (error == ESP_OK && raw == level.value()) {
            return true;
        }
        if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND) {
            return false;
        }
    }
    if (nvs_set_u8(handle_, key.c_str(), level.value()) != ESP_OK) {
        return false;
    }
    staged_[key] = level.value();
    dirty_ = true;
    return true;
}

bool NvsLevelStore::commit() {
    if (!available()) {
        return false;
    }
    if (!dirty_) {
        return true;
    }
    if (nvs_commit(handle_) != ESP_OK) {
        return false;
    }
    staged_.clear();
    dirty_ = false;
    return true;
}

}
