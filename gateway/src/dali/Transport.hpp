#pragma once

#include <cstdint>
#include <optional>

#include "dali/Address.hpp"
#include "dali/ArcLevel.hpp"
#include "dali/Scene.hpp"
#include "dali/ShortAddress.hpp"

namespace gateway::dali {

// Outcome of one bus transaction. Unsupported (gear cannot answer) is
// deliberately distinct from Timeout (no answer this time).
enum class TransportStatus {
    Ok,
    Unconfigured,
    Timeout,
    Collision,
    InvalidResponse,
    Unsupported,
    Failure
};

// Indirect control commands (IEC 62386-102) without an explicit level.
enum class GearCommand {
    Off,
    Up,
    Down,
    OnAndStepUp
};

struct LevelRead {
    TransportStatus status;
    std::optional<ArcLevel> level;
};

struct GroupsRead {
    TransportStatus status;
    std::optional<std::uint16_t> groups;
};

// Port to the physical DALI bus. Firmware implements it with the Espressif
// DALI driver, tests with an in-memory fake.
class Transport {
public:
    virtual ~Transport() = default;
    virtual TransportStatus set_level(Address address, ArcLevel level) = 0;
    virtual TransportStatus send(Address address, GearCommand command) = 0;
    virtual TransportStatus go_to_scene(Address address, Scene scene) = 0;
    virtual TransportStatus store_scene(Address address, Scene scene) = 0;
    virtual LevelRead read_level(ShortAddress address) = 0;
    virtual GroupsRead read_groups(ShortAddress address) = 0;
    virtual TransportStatus set_group_membership(ShortAddress address, Group group, bool member) = 0;
};

}
