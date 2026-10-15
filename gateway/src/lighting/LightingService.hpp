#pragma once

#include <array>
#include <bitset>
#include <cstdint>
#include <optional>

#include "config/Configuration.hpp"
#include "dali/ColourControl.hpp"
#include "dali/Scene.hpp"
#include "dali/Transport.hpp"
#include "lighting/RelativeDimmer.hpp"
#include "storage/LevelStore.hpp"

namespace gateway::lighting {

// Where a cached level came from, so a measured value is never confused
// with an assumed one.
enum class StateSource : std::uint8_t {
    Unknown,
    Readback,
    Commanded,
    RestoredCommand
};

enum class ControlError : std::uint8_t {
    None,
    TransportFailure,
    NoSavedTarget,
    StorageFailure,
    // The probed DT8 capabilities of a short-addressed gear do not include
    // the requested colour type; no frames were sent.
    Unsupported,
    InvalidValue
};

struct State {
    std::optional<dali::ArcLevel> level;
    StateSource source = StateSource::Unknown;
};

// Which colour type a channel/group colour state currently describes; also
// picks the KNX status objects used to report it.
enum class ColourKind : std::uint8_t {
    None,
    Temperature,
    Rgb,
    Rgbw,
    XyY
};

// Last known colour of a channel or group, in KNX-space units. Tc is stored
// as Kelvin, xy as raw coordinates. RGB/RGBW channels are stored with the
// brightness scaling they were commanded or read at (colour_scaling), so a
// status can report colour x brightness as the actual level drifts. Colour
// is deliberately not persisted: the gear keeps its colour and recovery
// reads it back.
struct ColourState {
    ColourKind kind = ColourKind::None;
    StateSource source = StateSource::Unknown;
    std::uint16_t temperature_kelvin = 0;
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t w = 0;
    std::uint8_t rgbw_validity = 0;  // bit 3 = r, bit 2 = g, bit 1 = b, bit 0 = w
    std::uint8_t colour_scaling = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
};

// KNX-space colour ready to be encoded into status objects: RGB/RGBW
// channels already multiplied by the current brightness, xyY carries the
// current brightness byte.
struct ColourReport {
    ColourKind kind = ColourKind::None;
    std::uint16_t kelvin = 0;
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t w = 0;
    std::uint8_t rgbw_validity = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint8_t brightness = 0;
};

struct GearDetails {
    std::uint8_t status_byte = 0;
    std::uint8_t device_type = 6;
    std::uint8_t version_number = 0;
    std::uint8_t physical_min = 1;
    std::uint8_t min_level = 1;
    std::uint8_t max_level = 254;
    std::uint8_t power_on_level = 254;
    std::uint8_t system_failure_level = 254;
    std::uint8_t fade_time = 0;
    std::uint8_t fade_rate = 7;
    std::uint8_t dimming_curve = 0;  // IEC 62386-207 (DT6): 0 = Logarithmic, 1 = Linear
    std::uint16_t groups = 0;
    std::array<std::uint8_t, 16> scenes{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    bool probed = false;
    bool switch_only = false;  // True when DT7 (switching relay) or overridden as On/Off only
    bool power_configured = false;
    // IEC 62386-252 (D4i Memory Bank 202) native energy/power reporting + manual override:
    bool d4i_power_supported = false;
    bool use_manual_power_override = false;
    std::uint16_t d4i_active_power_deciwatts = 0;  // 0.1 W units read from Memory Bank 202
    std::uint32_t d4i_active_energy_wh = 0;        // Wh read from Memory Bank 202
    std::uint16_t full_power_deciwatts = 0;  // User-configured 100% lamp power in 0.1 W (e.g. 150 = 15.0 W)
    std::uint16_t idle_power_centiwatts =
        0;  // User-configured ballast idle power in 0.01 W (e.g. 40 = 0.40 W)
};

struct GearConfig {
    std::uint8_t min_level = 0xFF;
    std::uint8_t max_level = 0xFF;
    std::uint8_t power_on_level = 0xFF;
    std::uint8_t system_failure_level = 0xFF;
    std::uint8_t fade_time = 0xFF;
    std::uint8_t fade_rate = 0xFF;
    std::uint8_t dimming_curve = 0xFF;  // 0 = Logarithmic, 1 = Linear, 0xFF = unchanged
    std::uint16_t full_power_deciwatts = 0xFFFF;
    std::uint16_t idle_power_centiwatts = 0xFFFF;
    std::uint8_t use_manual_power_override = 0xFF;  // 0 = false, 1 = true, 0xFF = unchanged
    std::uint8_t switch_only = 0xFF;                // 0 = false, 1 = true, 0xFF = unchanged
    std::uint8_t device_type_override = 0xFF;       // 0..8 = override device_type, 0xFF = unchanged
    std::uint8_t dt8_mask_override = 0xFF;          // bit0=Tc, bit1=RGB, bit2=RGBW, bit3=xy, 0xFF = unchanged
    std::uint32_t groups_mask = 0xFFFFFFFFU;  // 0..0xFFFF = set 16-bit group mask, 0xFFFFFFFF = unchanged
};

struct ControlResult {
    ControlError error = ControlError::None;
    dali::TransportStatus transport_status = dali::TransportStatus::Ok;
    State state{};
    // State of these channels/groups changed during the operation.
    std::bitset<dali::ShortAddress::capacity>
        changed_channels{};                               // NOLINT(readability-redundant-member-init)
    std::bitset<dali::Group::capacity> changed_groups{};  // NOLINT(readability-redundant-member-init)
    // Colour state of these channels/groups changed.
    std::bitset<dali::ShortAddress::capacity>
        changed_colour_channels{};                               // NOLINT(readability-redundant-member-init)
    std::bitset<dali::Group::capacity> changed_colour_groups{};  // NOLINT(readability-redundant-member-init)
};

// Lighting use cases: set level on channel/group/broadcast, read back, recover
// after restart, reconcile group membership. Independent of ESP-IDF, KNX and
// the console.
class LightingService final {
public:
    LightingService(dali::Transport& transport, storage::LevelStore& store) noexcept;
    LightingService(const LightingService&) = delete;
    LightingService& operator=(const LightingService&) = delete;

    void configure(const config::Configuration& configuration) noexcept;
    [[nodiscard]] ControlResult set_level(dali::Address address, dali::ArcLevel target);
    [[nodiscard]] ControlResult switch_on(dali::Address address);
    [[nodiscard]] ControlResult switch_off(dali::Address address);
    [[nodiscard]] ControlResult refresh(dali::ShortAddress address);
    [[nodiscard]] ControlResult recover(dali::ShortAddress address);
    [[nodiscard]] ControlResult reconcile_groups(dali::ShortAddress address);
    // One relative-dim step produced by the worker's RelativeDimmer. Sends
    // ON AND STEP UP for the first up-step, UP/DOWN afterwards; never off.
    [[nodiscard]] ControlResult dim_step(dali::Address address, DimDirection direction, bool first);
    // Read back actual levels after a dim finished (stop or expiry); group and
    // broadcast states are derived from the lowest-numbered answering member.
    [[nodiscard]] ControlResult finish_dimming(dali::Address address);
    [[nodiscard]] ControlResult recall_scene(dali::Scene scene);
    [[nodiscard]] ControlResult store_scene(dali::Scene scene);
    // Colour operations on a channel, group or broadcast address. A short
    // address whose probed capabilities lack the colour type answers
    // ControlError::Unsupported without touching the bus; groups and
    // broadcast always send (mixed gear ignores what it cannot do).
    [[nodiscard]] ControlResult set_colour_temperature(dali::Address address, std::uint16_t kelvin);
    [[nodiscard]] ControlResult set_rgb(dali::Address address, std::uint8_t red, std::uint8_t green,
                                        std::uint8_t blue);
    // validity: bit 3 = r, bit 2 = g, bit 1 = b, bit 0 = w (DPT 251.600 mask);
    // invalid channels are sent as 0xFF "no change".
    [[nodiscard]] ControlResult set_rgbw(dali::Address address, std::uint8_t red, std::uint8_t green,
                                         std::uint8_t blue, std::uint8_t white, std::uint8_t validity);
    [[nodiscard]] ControlResult set_xy(dali::Address address, std::uint16_t x, std::uint16_t y, bool xy_valid,
                                       std::uint8_t brightness, bool brightness_valid);
    // One relative step produced by the worker's colour dimmer; DPT 3.007
    // "increase" means a higher colour temperature (cooler), documented
    // explicitly because DALI steps in mirek.
    [[nodiscard]] ControlResult colour_temperature_step(dali::Address address, DimDirection direction);
    // Read back colour temperatures after a relative dim stopped.
    [[nodiscard]] ControlResult finish_colour_temperature(dali::Address address);
    // Reads the colour value matching the gear's active (or first capable)
    // colour type and records it as Readback state.
    [[nodiscard]] ControlResult refresh_colour(dali::ShortAddress address);
    // Probes QUERY COLOUR TYPE FEATURES and records the answer; a gear that
    // does not answer is recorded as having no colour capability.
    [[nodiscard]] ControlResult refresh_capabilities(dali::ShortAddress address);
    void set_capabilities(dali::ShortAddress address, dali::ColourCapabilities value) noexcept;
    std::optional<dali::ColourCapabilities> capabilities(dali::ShortAddress address) const noexcept;
    ColourState colour_state(dali::ShortAddress address) const noexcept;
    ColourState colour_state(dali::Group group) const noexcept;
    // Colour value to report on KNX: rgb/rgbw channels scaled by the
    // channel's current brightness, xyY with the current brightness byte.
    std::optional<ColourReport> colour_report(dali::Address address) const noexcept;
    State state(dali::ShortAddress address) const noexcept;
    State state(dali::Group group) const noexcept;
    [[nodiscard]] ControlResult query_gear_details(dali::ShortAddress address);
    [[nodiscard]] ControlResult configure_gear(dali::ShortAddress address, const GearConfig& cfg);
    void record_status_byte(dali::ShortAddress address, std::uint8_t status_byte) noexcept;
    const GearDetails& gear_details(dali::ShortAddress address) const noexcept;
    [[nodiscard]] std::uint16_t effective_power_deciwatts(dali::ShortAddress address) const noexcept;
    const config::Configuration& configuration() const noexcept { return config_; }

private:
    struct LastActive {
        bool loaded = false;
        std::optional<dali::ArcLevel> level;
    };

    ControlResult apply_level(dali::Address address, dali::ArcLevel target);
    void record_level(dali::Address address, dali::ArcLevel target, ControlResult& result) noexcept;
    std::bitset<dali::ShortAddress::capacity> affected_channels(dali::Address address) const noexcept;
    // False only for a short address whose probed capabilities are known and
    // lack the requested colour type; unknown counts as sendable.
    bool colour_supported(dali::Address address, ColourKind kind) const noexcept;
    // Records `state` on every channel/group the address reaches and marks
    // the corresponding changed_colour bits.
    void apply_colour_state(dali::Address address, const ColourState& colour, ControlResult& result) noexcept;
    // Shared RGB/RGBW path: sends the RGBWAF dim levels then drives the arc
    // level to the brightest valid component.
    ControlResult apply_rgbwaf(dali::Address address, std::uint8_t red, std::uint8_t green, std::uint8_t blue,
                               std::uint8_t white, std::uint8_t amber, std::uint8_t freecolour,
                               std::uint8_t validity, ColourKind kind);
    void persist(dali::Address address, dali::ArcLevel level, ControlResult& result) noexcept;
    std::optional<dali::ArcLevel> last_active(dali::Address address) noexcept;
    const config::SwitchOn& switch_on_policy(dali::Address address) const noexcept;
    void remember_if_active(dali::Address address, dali::ArcLevel level) noexcept;
    State state_of(dali::Address address) const noexcept;

    dali::Transport& transport_;
    storage::LevelStore& store_;
    dali::ColourControl colour_;
    config::Configuration config_{};
    std::array<State, dali::ShortAddress::capacity> channel_states_{};
    std::array<State, dali::Group::capacity> group_states_{};
    std::array<ColourState, dali::ShortAddress::capacity> channel_colours_{};
    std::array<ColourState, dali::Group::capacity> group_colours_{};
    std::array<std::optional<dali::ColourCapabilities>, dali::ShortAddress::capacity> capabilities_{};
    std::array<LastActive, dali::ShortAddress::capacity> channel_last_active_{};
    std::array<LastActive, dali::Group::capacity> group_last_active_{};
    LastActive broadcast_last_active_{};
    std::array<GearDetails, dali::ShortAddress::capacity> gear_details_{};
};

}
