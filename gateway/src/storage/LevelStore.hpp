#pragma once

#include "dali/Address.hpp"
#include "dali/ArcLevel.hpp"

namespace gateway::storage {

enum class ReadStatus {
    Ok,
    NotFound,
    Failure
};

struct TargetRead {
    ReadStatus status;
    std::optional<dali::ArcLevel> level;
};

enum class SlotKind {
    Target,      // last commanded level per gear, restores gear without readback
    LastActive   // last non-zero level per destination, recalled by switch-on
};

struct Slot {
    SlotKind kind;
    dali::Address address;
};

// Port to persistent memory holding commanded and last-active levels. NVS in
// firmware, memory in tests. save() only stages; commit() writes the batch so
// one bus operation costs a single flash commit.
class LevelStore {
public:
    virtual ~LevelStore() = default;
    virtual TargetRead load(Slot slot) = 0;
    virtual bool save(Slot slot, dali::ArcLevel level) = 0;
    virtual bool commit() = 0;
};

}
