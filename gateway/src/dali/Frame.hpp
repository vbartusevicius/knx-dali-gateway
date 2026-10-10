#pragma once

#include <cstdint>

#include "dali/Address.hpp"
#include "dali/ArcLevel.hpp"

namespace gateway::dali {

// Frame observed on the bus that was not a reply to our own transaction:
// 24-bit input-device events, other masters' forward frames.
struct BusEvent {
    std::uint32_t value;
    std::uint8_t bits;
};

// IEC 62386-102 16-bit forward frame encoding for control gear.
// Byte 0 carries the destination (Y AAA AAA S: 0 = short, 1 = group,
// both-bits-set = broadcast, selector S = 0 for DAPC level, 1 for command);
// byte 1 is the arc level or command opcode. Special commands (Part 102
// sequence frames like DTR0/INITIALISE/RANDOMISE) put their opcode in byte 0
// and their data in byte 1 instead of an address.
namespace frame {

std::uint16_t dapc(Address address, ArcLevel level) noexcept;
std::uint16_t command(Address address, std::uint8_t opcode) noexcept;
std::uint16_t special(std::uint8_t first, std::uint8_t second) noexcept;

}

}
