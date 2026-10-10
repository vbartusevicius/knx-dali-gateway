#pragma once

#include <cstdint>
#include <optional>

#include "dali/Frame.hpp"

namespace gateway::dali::device {

// One decoded device/instance-scheme event frame (IEC 62386-103 Table 5):
// bit 23 = 0 (short address), bit 16 = 0 (event), bit 15 = 1 (device/
// instance scheme); bits 22:17 the device short address, bits 14:10 the
// instance number, bits 9:0 the event information.
struct InputEvent {
    std::uint8_t address;
    std::uint8_t instance;
    std::uint16_t info;
};

// Decodes only the device/instance addressing scheme; other schemes,
// command frames and non-24-bit frames are rejected.
std::optional<InputEvent> decode_event(const BusEvent& frame) noexcept;

// Part 301 push-button event information values (Table 4 of Part 301;
// verified against python-dali dali/device/pushbutton.py).
enum class ButtonEvent : std::uint8_t {
    Released = 0,
    Pressed = 1,
    ShortPress = 2,
    DoublePress = 5,
    LongStart = 9,
    LongRepeat = 11,
    LongStop = 12,
    ButtonFree = 14,
    ButtonStuck = 15,
};

std::optional<ButtonEvent> decode_button(std::uint16_t info) noexcept;

// Part 303 occupancy event information (lowest four bits; high six must be
// zero): bit 0 movement, bit 1 occupied, bit 2 repeat event, bit 3 sensor
// kind (1 = movement sensor, 0 = presence sensor).
struct OccupancyEvent {
    bool movement;
    bool occupied;
    bool repeat;
    bool movement_sensor;
};

std::optional<OccupancyEvent> decode_occupancy(std::uint16_t info) noexcept;

// Part 304 light event information: the 10-bit illuminance value. It is a
// relative value (scaled by the instance resolution), not absolute lux.
struct LightEvent {
    std::uint16_t illuminance;
};

LightEvent decode_light(std::uint16_t info) noexcept;

}
