#pragma once

#include <cstdint>
#include <optional>

#include "dali/Address.hpp"
#include "dali/ArcLevel.hpp"
#include "dali/Frame.hpp"
#include "dali/Scene.hpp"
#include "dali/devices/DeviceFrames.hpp"
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

// Answers QUERY CONTROL GEAR: any valid backward frame means the gear is
// there; Timeout means no answer this time.
struct GearPresence {
    TransportStatus status;
    bool present;
};

// Answers QUERY STATUS: the raw IEC 62386-102 status byte; bit 1 is a lamp
// failure.
struct GearStatusRead {
    TransportStatus status;
    std::optional<std::uint8_t> gear_status;
};

enum class CommissionMode {
    All,
    Unaddressed
};

struct CommissionResult {
    TransportStatus status;
    unsigned count;
};

struct ByteRead {
    TransportStatus status;
    std::optional<std::uint8_t> value;
};

// Part 102 data transfer registers, loaded through the special commands
// DTR0 (0xA3), DTR1 (0xC3) and DTR2 (0xC5).
enum class Dtr : std::uint8_t {
    Dtr0,
    Dtr1,
    Dtr2
};

// Port to the physical DALI bus. Firmware implements it with the Espressif
// DALI driver, tests with an in-memory fake.
class Transport {
public:
    virtual ~Transport() = default;
    virtual TransportStatus set_level(Address address, ArcLevel level) = 0;
    virtual TransportStatus send(Address address, GearCommand command) = 0;
    // Generic addressed command frame (second byte = opcode); twice marks
    // IEC 62386-102 send-twice commands like configuration stores.
    virtual TransportStatus send_command(Address address, std::uint8_t opcode, bool twice) = 0;
    // Part 102 special command DTR0/1/2: loads `value` into the register.
    virtual TransportStatus set_dtr(Dtr which, std::uint8_t value) = 0;
    // Part 102 special command ENABLE DEVICE TYPE (0xC1): arms the next
    // application-extended command on the bus for that device type.
    virtual TransportStatus enable_device_type(std::uint8_t type) = 0;
    // Generic addressed query command; the gear's backward frame is the value.
    virtual ByteRead query(ShortAddress address, std::uint8_t opcode) = 0;
    // Part 102 special command frame {opcode, data} (e.g. SEARCHADDRH 0xB1).
    virtual TransportStatus send_special(std::uint8_t, std::uint8_t, bool) {
        return TransportStatus::Unsupported;
    }
    // Special command expecting a backward reply (COMPARE, VERIFY/QUERY
    // SHORT ADDRESS). Collision means several units answered at once, which
    // still counts as "yes" for COMPARE-style queries.
    virtual ByteRead query_special(std::uint8_t, std::uint8_t) {
        return {TransportStatus::Unsupported, std::nullopt};
    }
    // Part 103 24-bit forward frame {address, instance, opcode}, encoded
    // through dali::device::command; twice marks send-twice commands.
    virtual TransportStatus send_device_frame(std::uint32_t, bool) {
        return TransportStatus::Unsupported;
    }
    // Part 103 query expecting an 8-bit backward frame. Collision means
    // several units answered at once, which still counts as "yes" for
    // COMPARE-style queries.
    virtual ByteRead query_device_frame(std::uint32_t) {
        return {TransportStatus::Unsupported, std::nullopt};
    }
    // Table 21 device-level command: instance byte 0xFE "device"; the
    // broadcast destination byte is 0xFF.
    TransportStatus send_device_command(std::uint8_t destination, std::uint8_t command,
                                        bool twice) {
        return send_device_frame(device::command(destination, device::instance_device, command),
                                 twice);
    }
    // Next unsolicited frame observed outside a transaction (bounded queue;
    // implementations without a receive path return nullopt).
    virtual std::optional<BusEvent> poll_event() { return std::nullopt; }
    virtual TransportStatus go_to_scene(Address address, Scene scene) = 0;
    virtual LevelRead read_level(ShortAddress address) = 0;
    virtual GroupsRead read_groups(ShortAddress address) = 0;
    virtual TransportStatus set_group_membership(ShortAddress address, Group group, bool member) = 0;
    virtual GearPresence query_present(ShortAddress address) = 0;
    virtual GearStatusRead query_status(ShortAddress address) = 0;
    // Part 102 binary-search address assignment; a long blocking operation.
    virtual CommissionResult commission(CommissionMode mode, ShortAddress start, unsigned max_devices) = 0;
    // Part 103 control-device address assignment; a long blocking
    // operation. The default implementation runs dali::DeviceAddressing
    // over send_device_frame/query_device_frame.
    virtual CommissionResult commission_devices(CommissionMode mode, ShortAddress start,
                                                unsigned max_devices);
    // Makes the gear draw attention to itself (the pinned driver has no
    // IDENTIFY DEVICE, so adapters blink or go to maximum).
    virtual TransportStatus identify(ShortAddress address) = 0;
    // Broadcasts DTR0 = 0xFF followed by SET SHORT ADDRESS (send-twice),
    // wiping every short address on the bus; gear must be recommissioned.
    virtual TransportStatus clear_short_addresses() = 0;
};

}
