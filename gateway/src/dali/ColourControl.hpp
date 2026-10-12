#pragma once

#include <cstdint>
#include <optional>
#include <type_traits>

#include "dali/Address.hpp"
#include "dali/Scene.hpp"
#include "dali/ShortAddress.hpp"
#include "dali/Transport.hpp"

namespace gateway::dali {

// IEC 62386-209 colour temperature unit: mirek = 1'000'000 / Kelvin.
// 0 and 0xFFFF are reserved, so 1..65534.
class Mirek final {
public:
    [[nodiscard]] static std::optional<Mirek> from(unsigned value) noexcept;
    std::uint16_t value() const noexcept;

private:
    explicit Mirek(std::uint16_t value) noexcept;
    std::uint16_t value_;
};

// 16-bit chromaticity coordinate (0..65535 maps to 0.0..1.0).
class XyCoordinate final {
public:
    [[nodiscard]] static std::optional<XyCoordinate> from(unsigned value) noexcept;
    std::uint16_t value() const noexcept;

private:
    explicit XyCoordinate(std::uint16_t value) noexcept;
    std::uint16_t value_;
};

// QUERY COLOUR TYPE FEATURES answer (IEC 62386-209, command 249): bit 0 = xy
// capable, bit 1 = Tc capable, bits 2-4 = number of primaries, bits 5-7 =
// number of RGBWAF channels.
struct ColourCapabilities {
    bool xy_capable = false;
    bool tc_capable = false;
    std::uint8_t primary_count = 0;
    std::uint8_t rgbwaf_channels = 0;
};

// QUERY COLOUR STATUS answer (command 248).
struct ColourStatus {
    bool xy_out_of_range = false;
    bool tc_out_of_range = false;
    bool auto_calibration_running = false;
    bool auto_calibration_successful = false;
    bool xy_active = false;
    bool tc_active = false;
    bool primary_n_active = false;
    bool rgbwaf_active = false;
};

struct ColourStatusRead {
    TransportStatus status;
    std::optional<ColourStatus> value;
};

struct CapabilitiesRead {
    TransportStatus status;
    std::optional<ColourCapabilities> value;
};

// QUERY COLOUR VALUE (command 250) answers a selector-dependent value; for
// 16-bit values the backward frame is the MSB and the LSB is loaded into
// DTR0 (IEC 62386-209 Table 11, matching python-dali's QueryColourValue and
// fake gear — the "lo/hi index" scheme in the Espressif header comment is
// incorrect).
struct ColourValueRead {
    TransportStatus status;
    std::optional<std::uint8_t> msb;
    std::optional<std::uint8_t> lsb;
};

// DTR2 selector for STORE COLOUR TEMPERATURE TC LIMIT (command 242).
enum class TcLimit : std::uint8_t {
    Coolest = 0,
    Warmest = 1,
    PhysicalCoolest = 2,
    PhysicalWarmest = 3
};

enum class ColourStep {
    Cooler,
    Warmer
};

// Portable IEC 62386-209 (DT8) sequencing over Transport. Every application-
// extended command is preceded by ENABLE DEVICE TYPE 8; DTR pre-loads are
// sent before the EDT because the enable window only covers the next frame.
// Non-DT8 gear ignores extended commands, so sequences are safe to address
// at mixed groups or broadcast.
class ColourControl final {
public:
    explicit ColourControl(Transport& transport) noexcept;

    // DTR0/DTR1 = mirek (LSB first), SET TEMPORARY COLOUR TEMPERATURE,
    // ACTIVATE.
    TransportStatus set_colour_temperature(Address address, Mirek value);
    // DTR0/DTR1 = x then y (LSB first), SET TEMPORARY x/y COORDINATE,
    // ACTIVATE.
    TransportStatus set_xy(Address address, XyCoordinate x, XyCoordinate y);
    // SET TEMPORARY PRIMARY N DIMLEVEL: DTR0/DTR1 = dimlevel, DTR2 = N.
    TransportStatus set_primary_n(Address address, std::uint8_t primary, std::uint16_t dimlevel);
    // DTR0..2 = R,G,B -> SET TEMPORARY RGB DIMLEVEL; DTR0..2 = W,A,F ->
    // SET TEMPORARY WAF DIMLEVEL; ACTIVATE. 0xFF per channel = "no change".
    TransportStatus set_rgbwaf(Address address, std::uint8_t red, std::uint8_t green, std::uint8_t blue,
                               std::uint8_t white, std::uint8_t amber, std::uint8_t freecolour);
    TransportStatus step_colour_temperature(Address address, ColourStep step);
    // STORE COLOUR TEMPERATURE TC LIMIT (send-twice): DTR0/DTR1 = mirek,
    // DTR2 selects which limit is stored.
    TransportStatus store_colour_temperature_limit(Address address, TcLimit which, Mirek value);
    ColourStatusRead query_colour_status(ShortAddress address);
    CapabilitiesRead query_colour_type_features(ShortAddress address);
    ColourValueRead query_colour_value(ShortAddress address, std::uint8_t selector);
    // COPY REPORT TO TEMPORARY: loads the reported (actual) colour into the
    // temporary colour registers; used before storing colour scenes.
    TransportStatus copy_report_to_temporary(Address address);
    // Scene learn captures colour too: STORE ACTUAL LEVEL IN DTR0, then
    // COPY REPORT TO TEMPORARY (ignored by non-DT8 gear), then
    // STORE DTR AS SCENE.
    TransportStatus store_scene(Address address, Scene scene);

private:
    // ENABLE DEVICE TYPE 8 immediately before one extended command.
    TransportStatus extended(Address address, std::uint8_t opcode);
    TransportStatus extended_twice(Address address, std::uint8_t opcode);

    Transport& transport_;
};

// mirek = 1'000'000 / K, rounded. Kelvin 0 or above ~1e6 has no mirek value.
std::optional<Mirek> mirek_from_kelvin(std::uint32_t kelvin) noexcept;
std::uint32_t kelvin_from_mirek(Mirek mirek) noexcept;

}
