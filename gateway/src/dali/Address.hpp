#pragma once

#include <cstdint>
#include <optional>
#include <type_traits>

#include "dali/Group.hpp"
#include "dali/ShortAddress.hpp"

namespace gateway::dali {

enum class AddressKind : std::uint8_t {
    Short,
    Group,
    Broadcast
};

// One DALI destination: a single gear, a group, or the whole bus. Stored as a
// tag plus raw byte so it stays trivially copyable for FreeRTOS queues.
class Address final {
public:
    static Address short_address(ShortAddress address) noexcept;
    static Address group(Group group) noexcept;
    static Address broadcast() noexcept;

    AddressKind kind() const noexcept;
    [[nodiscard]] std::optional<ShortAddress> as_short() const noexcept;
    [[nodiscard]] std::optional<Group> as_group() const noexcept;

    bool operator==(const Address& other) const noexcept;
    bool operator!=(const Address& other) const noexcept;

private:
    Address(AddressKind kind, std::uint8_t value) noexcept;
    AddressKind kind_;
    std::uint8_t value_;
};

static_assert(std::is_trivially_copyable_v<Address>, "Address travels through FreeRTOS queues");

}
