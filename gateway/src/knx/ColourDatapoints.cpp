#include "knx/ColourDatapoints.hpp"

namespace gateway::knx {

std::array<std::uint8_t, 2> encode_colour_temperature(ColourTemperatureValue value) noexcept {
    return {static_cast<std::uint8_t>(value.kelvin >> 8U),
            static_cast<std::uint8_t>(value.kelvin & 0xFFU)};
}

std::optional<ColourTemperatureValue> decode_colour_temperature(const std::uint8_t* data,
                                                                std::size_t length) noexcept {
    if (length != 2) {
        return std::nullopt;
    }
    return ColourTemperatureValue{
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[0]) << 8U | data[1])};
}

std::array<std::uint8_t, 3> encode_rgb(RgbValue value) noexcept {
    return {value.red, value.green, value.blue};
}

std::optional<RgbValue> decode_rgb(const std::uint8_t* data, std::size_t length) noexcept {
    if (length != 3) {
        return std::nullopt;
    }
    return RgbValue{data[0], data[1], data[2]};
}

std::array<std::uint8_t, 6> encode_rgbw(RgbwValue value) noexcept {
    return {value.red,  value.green, value.blue,
            value.white, std::uint8_t{0}, static_cast<std::uint8_t>(value.validity & 0x0FU)};
}

std::optional<RgbwValue> decode_rgbw(const std::uint8_t* data, std::size_t length) noexcept {
    if (length != 6) {
        return std::nullopt;
    }
    return RgbwValue{data[0], data[1], data[2], data[3],
                     static_cast<std::uint8_t>(data[5] & 0x0FU)};
}

std::array<std::uint8_t, 6> encode_xyy(XyYValue value) noexcept {
    return {static_cast<std::uint8_t>(value.x >> 8U),
            static_cast<std::uint8_t>(value.x & 0xFFU),
            static_cast<std::uint8_t>(value.y >> 8U),
            static_cast<std::uint8_t>(value.y & 0xFFU),
            value.brightness,
            static_cast<std::uint8_t>(value.validity & 0x03U)};
}

std::optional<XyYValue> decode_xyy(const std::uint8_t* data, std::size_t length) noexcept {
    if (length != 6) {
        return std::nullopt;
    }
    return XyYValue{static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[0]) << 8U | data[1]),
                    static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[2]) << 8U | data[3]),
                    data[4], static_cast<std::uint8_t>(data[5] & 0x03U)};
}

}
