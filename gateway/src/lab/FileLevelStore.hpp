#pragma once

#include <map>
#include <string>

#include "storage/LevelStore.hpp"

namespace gateway::lab {

// LevelStore backed by a small text file ("key level" lines), so the lab
// gateway keeps last-active levels across restarts like the NVS firmware.
// save() stages in memory; commit() rewrites the file atomically.
class FileLevelStore final : public storage::LevelStore {
public:
    explicit FileLevelStore(std::string path);

    storage::TargetRead load(storage::Slot slot) override;
    bool save(storage::Slot slot, dali::ArcLevel level) override;
    bool commit() override;

private:
    static std::string key(storage::Slot slot);

    std::string path_;
    std::map<std::string, dali::ArcLevel> values_;
    bool dirty_ = false;
};

}
