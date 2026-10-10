#include "dali/Frame.hpp"

namespace gateway::dali::frame {
namespace {

std::uint8_t address_byte(Address address, bool selector) {
    const auto s = static_cast<std::uint8_t>(selector ? 1 : 0);
    switch (address.kind()) {
    case AddressKind::Short:
        return static_cast<std::uint8_t>((address.as_short()->value() << 1) | s);
    case AddressKind::Group:
        return static_cast<std::uint8_t>(0x80 | (address.as_group()->value() << 1) | s);
    case AddressKind::Broadcast:
        return static_cast<std::uint8_t>(0xFE | s);
    }
    return 0;
}

}

std::uint16_t dapc(Address address, ArcLevel level) noexcept {
    return static_cast<std::uint16_t>((address_byte(address, false) << 8) | level.value());
}

std::uint16_t command(Address address, std::uint8_t opcode) noexcept {
    return static_cast<std::uint16_t>((address_byte(address, true) << 8) | opcode);
}

std::uint16_t special(std::uint8_t first, std::uint8_t second) noexcept {
    return static_cast<std::uint16_t>((first << 8) | second);
}

}
