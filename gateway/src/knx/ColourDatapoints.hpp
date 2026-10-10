#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace gateway::knx {

// Encode/decode helpers for the colour datapoint types used by the colour
// objects. Byte layouts verified against xknx 3.20.0 (DPTColourTemperature,
// DPTColorRGB, DPTColorRGBW, DPTColorXYY) and the KNX DPT specification.

// DPT 7.600: colour temperature in Kelvin, 2-byte unsigned (0..65535).
struct ColourTemperatureValue {
    std::uint16_t kelvin;
};

// DPT 232.600: RGB colour value, 3 bytes R, G, B (0..255 each).
struct RgbValue {
    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;
};

// DPT 251.600: RGBW colour value, 6 bytes: R, G, B, W, then two reserved
// bytes; the last byte's low nibble is the validity mask
// (bit 3 = R, bit 2 = G, bit 1 = B, bit 0 = W; invalid = "no change").
struct RgbwValue {
    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;
    std::uint8_t white;
    std::uint8_t validity;
};

// DPT 242.600: xyY colour, 6 bytes: x (uint16 big-endian), y (uint16),
// brightness (uint8), then one reserved byte and the validity mask
// (bit 1 = xy valid, bit 0 = brightness valid).
struct XyYValue {
    std::uint16_t x;
    std::uint16_t y;
    std::uint8_t brightness;
    std::uint8_t validity;
};

std::array<std::uint8_t, 2> encode_colour_temperature(ColourTemperatureValue value) noexcept;
std::optional<ColourTemperatureValue> decode_colour_temperature(const std::uint8_t* data,
                                                                std::size_t length) noexcept;

std::array<std::uint8_t, 3> encode_rgb(RgbValue value) noexcept;
std::optional<RgbValue> decode_rgb(const std::uint8_t* data, std::size_t length) noexcept;

std::array<std::uint8_t, 6> encode_rgbw(RgbwValue value) noexcept;
std::optional<RgbwValue> decode_rgbw(const std::uint8_t* data, std::size_t length) noexcept;

std::array<std::uint8_t, 6> encode_xyy(XyYValue value) noexcept;
std::optional<XyYValue> decode_xyy(const std::uint8_t* data, std::size_t length) noexcept;

}
