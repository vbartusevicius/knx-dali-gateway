#pragma once

#include <array>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "dali/Transport.hpp"
#include "storage/LevelStore.hpp"

namespace gateway::testing {

struct LevelWrite {
    dali::Address address;
    dali::ArcLevel level;
};

struct MembershipWrite {
    dali::ShortAddress address;
    dali::Group group;
    bool member;
};

struct GearCommandWrite {
    dali::Address address;
    dali::GearCommand command;
};

// Scriptable dali::Transport that records writes/reads, so LightingService is
// tested without hardware.
class FakeDaliTransport final : public dali::Transport {
public:
    dali::TransportStatus write_status = dali::TransportStatus::Ok;
    dali::TransportStatus membership_status = dali::TransportStatus::Ok;
    dali::LevelRead next_read{dali::TransportStatus::Ok, dali::ArcLevel::from(100)};
    dali::GroupsRead next_groups{dali::TransportStatus::Ok, std::uint16_t{0}};
    std::vector<LevelWrite> writes;
    std::vector<dali::ShortAddress> reads;
    std::vector<dali::ShortAddress> group_reads;
    std::vector<MembershipWrite> membership_writes;
    std::vector<GearCommandWrite> gear_commands;
    std::vector<dali::Scene> scenes_recalled;
    std::vector<dali::Scene> scenes_stored;

    dali::TransportStatus set_level(dali::Address address, dali::ArcLevel level) override {
        writes.push_back({address, level});
        return write_status;
    }

    dali::TransportStatus send(dali::Address address, dali::GearCommand command) override {
        gear_commands.push_back({address, command});
        return write_status;
    }

    dali::TransportStatus go_to_scene(dali::Address, dali::Scene scene) override {
        scenes_recalled.push_back(scene);
        return write_status;
    }

    dali::TransportStatus store_scene(dali::Address, dali::Scene scene) override {
        scenes_stored.push_back(scene);
        return write_status;
    }

    dali::LevelRead read_level(dali::ShortAddress address) override {
        reads.push_back(address);
        return next_read;
    }

    dali::GroupsRead read_groups(dali::ShortAddress address) override {
        group_reads.push_back(address);
        return next_groups;
    }

    dali::TransportStatus set_group_membership(dali::ShortAddress address, dali::Group group,
                                               bool member) override {
        membership_writes.push_back({address, group, member});
        return membership_status;
    }
};

// In-memory LevelStore with switchable failures for persistence and recovery tests.
class MemoryLevelStore final : public storage::LevelStore {
public:
    std::map<std::string, dali::ArcLevel> values;
    bool read_failure = false;
    bool malformed_read = false;
    bool save_succeeds = true;
    bool commit_succeeds = true;
    unsigned saves = 0;
    unsigned loads = 0;
    unsigned commits = 0;

    static std::string key(storage::Slot slot) {
        char key[8] = {};
        const auto& address = slot.address;
        const char* pattern = "b";
        int index = -1;
        if (slot.kind == storage::SlotKind::Target) {
            pattern = "arc_%02u";
            index = address.as_short() ? address.as_short()->value() : -1;
        } else if (address.as_short()) {
            pattern = "a%02u";
            index = address.as_short()->value();
        } else if (address.as_group()) {
            pattern = "g%02u";
            index = address.as_group()->value();
        }
        if (index < 0) {
            return pattern;
        }
        std::snprintf(key, sizeof(key), pattern, static_cast<unsigned>(index));
        return key;
    }

    storage::TargetRead load(storage::Slot slot) override {
        ++loads;
        if (read_failure) {
            return {storage::ReadStatus::Failure, std::nullopt};
        }
        if (malformed_read) {
            return {storage::ReadStatus::Ok, std::nullopt};
        }
        const auto found = values.find(key(slot));
        return found != values.end() ? storage::TargetRead{storage::ReadStatus::Ok, found->second}
                                     : storage::TargetRead{storage::ReadStatus::NotFound, std::nullopt};
    }

    bool save(storage::Slot slot, dali::ArcLevel level) override {
        ++saves;
        if (!save_succeeds) {
            return false;
        }
        values.insert_or_assign(key(slot), level);
        return true;
    }

    bool commit() override {
        ++commits;
        return commit_succeeds;
    }
};

}
