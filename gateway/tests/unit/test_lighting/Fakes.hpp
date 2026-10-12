#pragma once

#include <cstdio>
#include <deque>
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

struct CommandWrite {
    dali::Address address;
    std::uint8_t opcode;
    bool twice;
};

struct DtrWrite {
    dali::Dtr which;
    std::uint8_t value;
};

struct QueryWrite {
    dali::ShortAddress address;
    std::uint8_t opcode;
};

struct CommissionCall {
    dali::CommissionMode mode;
    dali::ShortAddress start;
    unsigned max_devices;
};

// One transport call in call order, so tests can verify exact frame
// sequences (DTR loads before each ENABLE DEVICE TYPE, and so on).
struct TransportOp {
    enum class Kind : std::uint8_t {
        Level,
        GearCommand,
        Command,
        Dtr,
        EnableDeviceType,
        Query,
        Scene,
        ReadLevel,
        ReadGroups,
        Membership,
        Presence,
        StatusQuery,
        Commission,
        Identify,
        Clear
    };
    Kind kind;
    dali::Address address = dali::Address::broadcast();
    std::uint8_t opcode = 0;  // command/query opcode, gear command, device type
    std::uint8_t value = 0;   // DTR data, level, DTR index, scene, member
    bool flag = false;        // send-twice / member bool
};

// Scriptable dali::Transport that records writes/reads, so LightingService is
// tested without hardware. Not final: the commissioning tests derive a
// control-device model from it.
class FakeDaliTransport : public dali::Transport {
public:
    dali::TransportStatus write_status = dali::TransportStatus::Ok;
    dali::TransportStatus membership_status = dali::TransportStatus::Ok;
    dali::LevelRead next_read{dali::TransportStatus::Ok, dali::ArcLevel::from(100)};
    dali::GroupsRead next_groups{dali::TransportStatus::Ok, std::uint16_t{0}};
    // Per-address presence answers; addresses not listed answer default_presence.
    std::map<unsigned, dali::GearPresence> presence_replies;
    dali::GearPresence default_presence{dali::TransportStatus::Ok, false};
    std::deque<dali::GearStatusRead> status_replies;
    dali::GearStatusRead next_status{dali::TransportStatus::Ok, std::uint8_t{0}};
    std::deque<dali::CommissionResult> commission_replies;
    dali::CommissionResult next_commission{dali::TransportStatus::Ok, 0};
    dali::TransportStatus identify_status = dali::TransportStatus::Ok;
    dali::TransportStatus clear_status = dali::TransportStatus::Ok;
    std::deque<dali::ByteRead> query_replies;
    dali::ByteRead next_query{dali::TransportStatus::Ok, std::uint8_t{0}};
    std::vector<LevelWrite> writes;
    std::vector<dali::ShortAddress> reads;
    std::vector<dali::ShortAddress> group_reads;
    std::vector<MembershipWrite> membership_writes;
    std::vector<GearCommandWrite> gear_commands;
    std::vector<CommandWrite> command_writes;
    std::vector<DtrWrite> dtr_writes;
    std::vector<std::uint8_t> device_type_enables;
    std::vector<QueryWrite> queries;
    std::vector<dali::Scene> scenes_recalled;
    std::vector<dali::Scene> scenes_stored;
    std::vector<dali::ShortAddress> presence_queries;
    std::vector<dali::ShortAddress> status_queries;
    std::vector<CommissionCall> commission_calls;
    std::vector<dali::ShortAddress> identifies;
    unsigned clear_calls = 0;
    std::vector<TransportOp> ops;

    dali::TransportStatus set_level(dali::Address address, dali::ArcLevel level) override {
        writes.push_back({address, level});
        ops.push_back({TransportOp::Kind::Level, address, 0, level.value()});
        return write_status;
    }

    dali::TransportStatus send(dali::Address address, dali::GearCommand command) override {
        gear_commands.push_back({address, command});
        ops.push_back({TransportOp::Kind::GearCommand, address, static_cast<std::uint8_t>(command)});
        return write_status;
    }

    dali::TransportStatus send_command(dali::Address address, std::uint8_t opcode, bool twice) override {
        command_writes.push_back({address, opcode, twice});
        ops.push_back({TransportOp::Kind::Command, address, opcode, 0, twice});
        return write_status;
    }

    dali::TransportStatus set_dtr(dali::Dtr which, std::uint8_t value) override {
        dtr_writes.push_back({which, value});
        ops.push_back(
            {TransportOp::Kind::Dtr, dali::Address::broadcast(), value, static_cast<std::uint8_t>(which)});
        return write_status;
    }

    dali::TransportStatus enable_device_type(std::uint8_t type) override {
        device_type_enables.push_back(type);
        ops.push_back({TransportOp::Kind::EnableDeviceType, dali::Address::broadcast(), type});
        return write_status;
    }

    dali::ByteRead query(dali::ShortAddress address, std::uint8_t opcode) override {
        queries.push_back({address, opcode});
        ops.push_back({TransportOp::Kind::Query, dali::Address::short_address(address), opcode});
        if (!query_replies.empty()) {
            const auto reply = query_replies.front();
            query_replies.pop_front();
            return reply;
        }
        return next_query;
    }

    dali::TransportStatus go_to_scene(dali::Address, dali::Scene scene) override {
        scenes_recalled.push_back(scene);
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

    dali::GearPresence query_present(dali::ShortAddress address) override {
        presence_queries.push_back(address);
        const auto scripted = presence_replies.find(address.value());
        return scripted != presence_replies.end() ? scripted->second : default_presence;
    }

    dali::GearStatusRead query_status(dali::ShortAddress address) override {
        status_queries.push_back(address);
        if (!status_replies.empty()) {
            const auto reply = status_replies.front();
            status_replies.pop_front();
            return reply;
        }
        return next_status;
    }

    dali::CommissionResult commission(dali::CommissionMode mode, dali::ShortAddress start,
                                      unsigned max_devices) override {
        commission_calls.push_back({mode, start, max_devices});
        if (!commission_replies.empty()) {
            const auto reply = commission_replies.front();
            commission_replies.pop_front();
            return reply;
        }
        return next_commission;
    }

    dali::TransportStatus identify(dali::ShortAddress address) override {
        identifies.push_back(address);
        return identify_status;
    }

    dali::TransportStatus clear_short_addresses() override {
        ++clear_calls;
        return clear_status;
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
