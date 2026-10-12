#include "lab/FileLevelStore.hpp"

#include <cstdio>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <utility>

#include "dali/ArcLevel.hpp"
#include "storage/LevelStore.hpp"

namespace gateway::lab {
namespace {

constexpr const char* target_pattern = "arc_%u";  // matches the NVS key space
constexpr const char* channel_pattern = "a%u";
constexpr const char* group_pattern = "g%u";
constexpr const char* broadcast_key = "b";

}

FileLevelStore::FileLevelStore(std::string path) : path_(std::move(path)) {
    std::ifstream input(path_);
    std::string key;
    unsigned level = 0;
    while (input >> key >> level) {
        if (const auto arc = dali::ArcLevel::from(level)) {
            values_.emplace(key, *arc);
        }
    }
}

std::string FileLevelStore::key(storage::Slot slot) {
    const auto& address = slot.address;
    const char* pattern = broadcast_key;
    unsigned index = 0;
    bool indexed = false;
    if (slot.kind == storage::SlotKind::Target) {
        pattern = target_pattern;
        indexed = address.as_short().has_value();
        index = indexed ? address.as_short()->value() : 0;
    } else if (address.as_short()) {
        pattern = channel_pattern;
        indexed = true;
        index = address.as_short()->value();
    } else if (address.as_group()) {
        pattern = group_pattern;
        indexed = true;
        index = address.as_group()->value();
    }
    if (!indexed) {
        return pattern;
    }
    char key[12] = {};
    std::snprintf(key, sizeof(key), pattern, index);
    return key;
}

storage::TargetRead FileLevelStore::load(storage::Slot slot) {
    const auto found = values_.find(key(slot));
    return found != values_.end() ? storage::TargetRead{storage::ReadStatus::Ok, found->second}
                                  : storage::TargetRead{storage::ReadStatus::NotFound, std::nullopt};
}

bool FileLevelStore::save(storage::Slot slot, dali::ArcLevel level) {
    auto stored = key(slot);
    const auto current = values_.find(stored);
    if (current != values_.end() && current->second.value() == level.value()) {
        return true;
    }
    values_.insert_or_assign(stored, level);
    dirty_ = true;
    return true;
}

bool FileLevelStore::commit() {
    if (!dirty_) {
        return true;
    }
    const auto staging = path_ + ".tmp";
    {
        std::ofstream output(staging, std::ios::trunc);
        if (!output) {
            return false;
        }
        for (const auto& [key, level] : values_) {
            output << key << ' ' << static_cast<unsigned>(level.value()) << '\n';
        }
        output.flush();
        if (!output) {
            return false;
        }
    }
    if (std::rename(staging.c_str(), path_.c_str()) != 0) {
        return false;
    }
    dirty_ = false;
    return true;
}

}
