#include "dali/devices/EventDecoder.hpp"

#include <cstdint>
#include <optional>

#include "dali/Frame.hpp"

namespace gateway::dali::device {

std::optional<InputEvent> decode_event(const BusEvent& frame) noexcept {
    if (frame.bits != 24) {
        return std::nullopt;
    }
    const std::uint32_t value = frame.value;
    // Event frames have bit 16 clear; the device/instance scheme has
    // bit 23 = 0 (short address) and bit 15 = 1.
    if ((value & (1u << 16)) != 0 || (value & (1u << 23)) != 0 || (value & (1u << 15)) == 0) {
        return std::nullopt;
    }
    return InputEvent{
        static_cast<std::uint8_t>((value >> 17) & 0x3F),
        static_cast<std::uint8_t>((value >> 10) & 0x1F),
        static_cast<std::uint16_t>(value & 0x3FF),
    };
}

std::optional<ButtonEvent> decode_button(std::uint16_t info) noexcept {
    switch (info) {
    case 0:
        return ButtonEvent::Released;
    case 1:
        return ButtonEvent::Pressed;
    case 2:
        return ButtonEvent::ShortPress;
    case 5:
        return ButtonEvent::DoublePress;
    case 9:
        return ButtonEvent::LongStart;
    case 11:
        return ButtonEvent::LongRepeat;
    case 12:
        return ButtonEvent::LongStop;
    case 14:
        return ButtonEvent::ButtonFree;
    case 15:
        return ButtonEvent::ButtonStuck;
    default:
        return std::nullopt;
    }
}

std::optional<OccupancyEvent> decode_occupancy(std::uint16_t info) noexcept {
    if ((info & ~0x0Fu) != 0) {
        return std::nullopt;
    }
    return OccupancyEvent{
        (info & 0x01) != 0,
        (info & 0x02) != 0,
        (info & 0x04) != 0,
        (info & 0x08) != 0,
    };
}

LightEvent decode_light(std::uint16_t info) noexcept {
    return LightEvent{static_cast<std::uint16_t>(info & 0x3FF)};
}

}
