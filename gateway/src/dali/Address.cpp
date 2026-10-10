#include "dali/Address.hpp"

namespace gateway::dali {

Address::Address(AddressKind kind, std::uint8_t value) noexcept : kind_(kind), value_(value) {}

Address Address::short_address(ShortAddress address) noexcept {
    return Address{AddressKind::Short, address.value()};
}

Address Address::group(Group group) noexcept {
    return Address{AddressKind::Group, group.value()};
}

Address Address::broadcast() noexcept {
    return Address{AddressKind::Broadcast, 0};
}

AddressKind Address::kind() const noexcept {
    return kind_;
}

std::optional<ShortAddress> Address::as_short() const noexcept {
    if (kind_ != AddressKind::Short) {
        return std::nullopt;
    }
    return ShortAddress::from(value_);
}

std::optional<Group> Address::as_group() const noexcept {
    if (kind_ != AddressKind::Group) {
        return std::nullopt;
    }
    return Group::from(value_);
}

bool Address::operator==(const Address& other) const noexcept {
    return kind_ == other.kind_ && value_ == other.value_;
}

bool Address::operator!=(const Address& other) const noexcept {
    return !(*this == other);
}

}
