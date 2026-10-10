#pragma once

#include <array>
#include <bitset>
#include <optional>

#include "config/Configuration.hpp"
#include "dali/Transport.hpp"
#include "storage/LevelStore.hpp"

namespace gateway::lighting {

// Where a cached level came from, so a measured value is never confused
// with an assumed one.
enum class StateSource {
    Unknown,
    Readback,
    Commanded,
    RestoredCommand
};

enum class ControlError {
    None,
    TransportFailure,
    NoSavedTarget,
    StorageFailure
};

struct State {
    std::optional<dali::ArcLevel> level;
    StateSource source = StateSource::Unknown;
};

struct ControlResult {
    ControlError error;
    dali::TransportStatus transport_status;
    State state;
    // State of these channels/groups changed during the operation.
    std::bitset<dali::ShortAddress::capacity> changed_channels;
    std::bitset<dali::Group::capacity> changed_groups;
};

// Lighting use cases: set level on channel/group/broadcast, read back, recover
// after restart, reconcile group membership. Independent of ESP-IDF, KNX and
// the console.
class LightingService final {
public:
    LightingService(dali::Transport& transport, storage::LevelStore& store) noexcept;
    LightingService(const LightingService&) = delete;
    LightingService& operator=(const LightingService&) = delete;

    void configure(const config::Configuration& configuration) noexcept;
    [[nodiscard]] ControlResult set_level(dali::Address address, dali::ArcLevel target);
    [[nodiscard]] ControlResult switch_on(dali::Address address);
    [[nodiscard]] ControlResult switch_off(dali::Address address);
    [[nodiscard]] ControlResult refresh(dali::ShortAddress address);
    [[nodiscard]] ControlResult recover(dali::ShortAddress address);
    [[nodiscard]] ControlResult reconcile_groups(dali::ShortAddress address);
    State state(dali::ShortAddress address) const noexcept;
    State state(dali::Group group) const noexcept;

private:
    struct LastActive {
        bool loaded = false;
        std::optional<dali::ArcLevel> level;
    };

    ControlResult apply_level(dali::Address address, dali::ArcLevel target);
    void persist(dali::Address address, dali::ArcLevel level, ControlResult& result) noexcept;
    std::optional<dali::ArcLevel> last_active(dali::Address address) noexcept;
    const config::SwitchOn& switch_on_policy(dali::Address address) const noexcept;
    void remember_if_active(dali::Address address, dali::ArcLevel level) noexcept;

    dali::Transport& transport_;
    storage::LevelStore& store_;
    config::Configuration config_{};
    std::array<State, dali::ShortAddress::capacity> channel_states_{};
    std::array<State, dali::Group::capacity> group_states_{};
    std::array<LastActive, dali::ShortAddress::capacity> channel_last_active_{};
    std::array<LastActive, dali::Group::capacity> group_last_active_{};
    LastActive broadcast_last_active_{};
};

}
