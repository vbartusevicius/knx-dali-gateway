#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "commissioning/Commissioning.hpp"
#include "control/Command.hpp"

namespace gateway::knx {

// KNX function-property interface for DALI commissioning (OpenKNX-style:
// function properties on interface-object index 160, operated by ETS
// parameter-page buttons — no DCA).
constexpr std::uint8_t function_property_object_index = 160;

enum class CommandProperty : std::uint8_t {
    Scan = 1,
    Assign = 2,
    Replace = 3,
    Identify = 4,
    ResetAddresses = 5,
    ScanInputs = 6,
    AssignInputs = 7
};

constexpr std::uint8_t state_property = 1;
constexpr std::uint8_t input_state_property = 2;

// Single-byte response of a command request.
enum class RequestResponse : std::uint8_t {
    Accepted = 0,
    Busy = 1,  // a commissioning job is queued or running
    InvalidArgument = 2
};

struct DecodedRequest {
    std::optional<control::Command> command;
    RequestResponse response = RequestResponse::Accepted;
};

// Decodes a FunctionPropertyCommand request. A wrong object index or
// property id, a missing/invalid data byte, or an out-of-range address is
// rejected with the matching response code instead of a command.
DecodedRequest decode_command(std::uint8_t object_index, std::uint8_t property_id, const std::uint8_t* data,
                              std::uint8_t length) noexcept;

// State response layout: [phase, progress 0..100, found, last error,
// presence bitmap (8 bytes, LSB = address 0), fault bitmap (8 bytes)].
constexpr std::size_t state_response_size = 20;

// Encodes a snapshot into the state response; returns the byte count.
std::size_t encode_state(const commissioning::Snapshot& snapshot, std::uint8_t* result) noexcept;

// Input-device state layout: [presence bitmap (8 bytes, LSB = device 0),
// per-device summary (64 bytes): bits 0-4 instance count, bit 5 push
// button, bit 6 occupancy, bit 7 light sensor; valid when present].
constexpr std::size_t input_state_response_size = 72;

// Encodes an input snapshot into the state-2 response; returns 72.
std::size_t encode_input_state(const commissioning::InputSnapshot& snapshot, std::uint8_t* result) noexcept;

}
