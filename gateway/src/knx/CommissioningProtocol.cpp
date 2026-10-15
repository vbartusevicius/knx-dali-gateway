#include "knx/CommissioningProtocol.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "commissioning/Commissioning.hpp"
#include "control/Command.hpp"
#include "dali/Address.hpp"
#include "dali/ShortAddress.hpp"
#include "dali/Transport.hpp"
#include "lighting/LampFinder.hpp"

namespace gateway::knx {
namespace {

DecodedRequest reject(RequestResponse response) {
    return {std::nullopt, response};
}

DecodedRequest address_command(std::uint8_t raw, bool replace) {
    const auto address = dali::ShortAddress::from(raw);
    if (!address) {
        return reject(RequestResponse::InvalidArgument);
    }
    if (replace) {
        return {control::ReplaceDevice{*address}, RequestResponse::Accepted};
    }
    return {control::IdentifyDevice{*address}, RequestResponse::Accepted};
}

}

DecodedRequest decode_command(std::uint8_t object_index, std::uint8_t property_id, const std::uint8_t* data,
                              std::uint8_t length) noexcept {
    if (object_index != function_property_object_index) {
        return reject(RequestResponse::InvalidArgument);
    }
    switch (static_cast<CommandProperty>(property_id)) {
    case CommandProperty::Scan:
        return {control::ScanBus{}, RequestResponse::Accepted};
    case CommandProperty::Assign:
        if (length < 1 || data[0] > 1) {
            return reject(RequestResponse::InvalidArgument);
        }
        return {control::AssignAddresses{data[0] == 1 ? dali::CommissionMode::All
                                                      : dali::CommissionMode::Unaddressed},
                RequestResponse::Accepted};
    case CommandProperty::Replace:
        if (length < 1) {
            return reject(RequestResponse::InvalidArgument);
        }
        return address_command(data[0], true);
    case CommandProperty::Identify:
        if (length < 1) {
            return reject(RequestResponse::InvalidArgument);
        }
        return address_command(data[0], false);
    case CommandProperty::ResetAddresses:
        return {control::ResetAddresses{}, RequestResponse::Accepted};
    case CommandProperty::ScanInputs:
        return {control::ScanInputDevices{}, RequestResponse::Accepted};
    case CommandProperty::AssignInputs:
        if (length < 1 || data[0] > 1) {
            return reject(RequestResponse::InvalidArgument);
        }
        return {control::AssignInputDevices{data[0] == 1 ? dali::CommissionMode::All
                                                         : dali::CommissionMode::Unaddressed},
                RequestResponse::Accepted};
    case CommandProperty::LampFinder: {
        if (length < 1) {
            return reject(RequestResponse::InvalidArgument);
        }
        const std::uint8_t raw_mode = length >= 2 ? data[1] : (data[0] == 0xFFU ? 0U : 1U);
        if (raw_mode > 2) {
            return reject(RequestResponse::InvalidArgument);
        }
        const auto mode = static_cast<lighting::FinderMode>(raw_mode);
        if (data[0] == 0xFFU) {
            return {control::FindLamp{dali::Address::broadcast(), mode}, RequestResponse::Accepted};
        }
        const auto ch = dali::ShortAddress::from(data[0]);
        if (!ch) {
            return reject(RequestResponse::InvalidArgument);
        }
        return {control::FindLamp{dali::Address::short_address(*ch), mode}, RequestResponse::Accepted};
    }
    }
    return reject(RequestResponse::InvalidArgument);
}

std::size_t encode_state(const commissioning::Snapshot& snapshot, std::uint8_t* result) noexcept {
    result[0] = static_cast<std::uint8_t>(snapshot.progress.phase);
    result[1] = snapshot.progress.progress;
    result[2] = snapshot.progress.found;
    result[3] = static_cast<std::uint8_t>(snapshot.progress.last_error);
    std::copy_n(snapshot.present.data(), snapshot.present.size(), result + 4);
    std::copy_n(snapshot.faulty.data(), snapshot.faulty.size(), result + 12);
    return state_response_size;
}

std::size_t encode_input_state(const commissioning::InputSnapshot& snapshot, std::uint8_t* result) noexcept {
    std::copy_n(snapshot.present.data(), snapshot.present.size(), result);
    std::copy_n(snapshot.summary.data(), snapshot.summary.size(), result + 8);
    return input_state_response_size;
}

}
