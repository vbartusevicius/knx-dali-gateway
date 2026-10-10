#include "dali/ColourControl.hpp"

namespace gateway::dali {
namespace {

// IEC 62386-102 opcodes used by the DT8 sequences.
constexpr std::uint8_t cmd_store_actual_level = 0x21;
constexpr std::uint8_t cmd_store_dtr_as_scene_0 = 0x40;
constexpr std::uint8_t cmd_query_content_dtr0 = 0x98;

// IEC 62386-209 application-extended commands (opcode >= 0xE0). Values
// verified against IEC 62386-209 Table 10/13 and python-dali's
// dali/gear/colour.py (_cmdval, uses_dtr0/1/2, sendtwice).
constexpr std::uint8_t ext_set_temporary_x = 0xE0;          // 224, uses DTR0+DTR1
constexpr std::uint8_t ext_set_temporary_y = 0xE1;          // 225, uses DTR0+DTR1
constexpr std::uint8_t ext_activate = 0xE2;                 // 226
constexpr std::uint8_t ext_set_temporary_tc = 0xE7;         // 231, uses DTR0+DTR1
constexpr std::uint8_t ext_tc_step_cooler = 0xE8;           // 232
constexpr std::uint8_t ext_tc_step_warmer = 0xE9;           // 233
constexpr std::uint8_t ext_set_primary_n_dimlevel = 0xEA;   // 234, uses DTR0+DTR1+DTR2
constexpr std::uint8_t ext_set_temporary_rgb = 0xEB;        // 235, uses DTR0+DTR1+DTR2
constexpr std::uint8_t ext_set_temporary_waf = 0xEC;        // 236, uses DTR0+DTR1+DTR2
constexpr std::uint8_t ext_copy_report_to_temporary = 0xEE; // 238
constexpr std::uint8_t ext_store_tc_limit = 0xF2;           // 242, send-twice
constexpr std::uint8_t ext_query_colour_status = 0xF8;      // 248
constexpr std::uint8_t ext_query_colour_type_features = 0xF9; // 249
constexpr std::uint8_t ext_query_colour_value = 0xFA;       // 250, uses DTR0

constexpr std::uint8_t device_type_colour = 8;

TransportStatus first_of(std::initializer_list<TransportStatus> results) noexcept {
    for (const auto status : results) {
        if (status != TransportStatus::Ok) {
            return status;
        }
    }
    return TransportStatus::Ok;
}

}

std::optional<Mirek> Mirek::from(unsigned value) noexcept {
    if (value < 1 || value > 65534) {
        return std::nullopt;
    }
    return Mirek{static_cast<std::uint16_t>(value)};
}

std::uint16_t Mirek::value() const noexcept {
    return value_;
}

Mirek::Mirek(std::uint16_t value) noexcept : value_(value) {}

std::optional<XyCoordinate> XyCoordinate::from(unsigned value) noexcept {
    if (value > 0xFFFF) {
        return std::nullopt;
    }
    return XyCoordinate{static_cast<std::uint16_t>(value)};
}

std::uint16_t XyCoordinate::value() const noexcept {
    return value_;
}

XyCoordinate::XyCoordinate(std::uint16_t value) noexcept : value_(value) {}

ColourControl::ColourControl(Transport& transport) noexcept : transport_(transport) {}

TransportStatus ColourControl::extended(Address address, std::uint8_t opcode) {
    const auto enabled = transport_.enable_device_type(device_type_colour);
    if (enabled != TransportStatus::Ok) {
        return enabled;
    }
    return transport_.send_command(address, opcode, false);
}

TransportStatus ColourControl::extended_twice(Address address, std::uint8_t opcode) {
    const auto enabled = transport_.enable_device_type(device_type_colour);
    if (enabled != TransportStatus::Ok) {
        return enabled;
    }
    return transport_.send_command(address, opcode, true);
}

TransportStatus ColourControl::set_colour_temperature(Address address, Mirek value) {
    auto status = first_of({transport_.set_dtr(Dtr::Dtr0, static_cast<std::uint8_t>(value.value() & 0xFF)),
                            transport_.set_dtr(Dtr::Dtr1, static_cast<std::uint8_t>(value.value() >> 8))});
    if (status != TransportStatus::Ok) {
        return status;
    }
    status = extended(address, ext_set_temporary_tc);
    if (status != TransportStatus::Ok) {
        return status;
    }
    return extended(address, ext_activate);
}

TransportStatus ColourControl::set_xy(Address address, XyCoordinate x, XyCoordinate y) {
    auto status = first_of({transport_.set_dtr(Dtr::Dtr0, static_cast<std::uint8_t>(x.value() & 0xFF)),
                            transport_.set_dtr(Dtr::Dtr1, static_cast<std::uint8_t>(x.value() >> 8))});
    if (status != TransportStatus::Ok) {
        return status;
    }
    status = extended(address, ext_set_temporary_x);
    if (status != TransportStatus::Ok) {
        return status;
    }
    status = first_of({transport_.set_dtr(Dtr::Dtr0, static_cast<std::uint8_t>(y.value() & 0xFF)),
                       transport_.set_dtr(Dtr::Dtr1, static_cast<std::uint8_t>(y.value() >> 8))});
    if (status != TransportStatus::Ok) {
        return status;
    }
    status = extended(address, ext_set_temporary_y);
    if (status != TransportStatus::Ok) {
        return status;
    }
    return extended(address, ext_activate);
}

TransportStatus ColourControl::set_primary_n(Address address, std::uint8_t primary,
                                             std::uint16_t dimlevel) {
    auto status = first_of({transport_.set_dtr(Dtr::Dtr0, static_cast<std::uint8_t>(dimlevel & 0xFF)),
                            transport_.set_dtr(Dtr::Dtr1, static_cast<std::uint8_t>(dimlevel >> 8)),
                            transport_.set_dtr(Dtr::Dtr2, primary)});
    if (status != TransportStatus::Ok) {
        return status;
    }
    status = extended(address, ext_set_primary_n_dimlevel);
    if (status != TransportStatus::Ok) {
        return status;
    }
    return extended(address, ext_activate);
}

TransportStatus ColourControl::set_rgbwaf(Address address, std::uint8_t red, std::uint8_t green,
                                          std::uint8_t blue, std::uint8_t white, std::uint8_t amber,
                                          std::uint8_t freecolour) {
    auto status = first_of({transport_.set_dtr(Dtr::Dtr0, red), transport_.set_dtr(Dtr::Dtr1, green),
                            transport_.set_dtr(Dtr::Dtr2, blue)});
    if (status != TransportStatus::Ok) {
        return status;
    }
    status = extended(address, ext_set_temporary_rgb);
    if (status != TransportStatus::Ok) {
        return status;
    }
    status = first_of({transport_.set_dtr(Dtr::Dtr0, white), transport_.set_dtr(Dtr::Dtr1, amber),
                       transport_.set_dtr(Dtr::Dtr2, freecolour)});
    if (status != TransportStatus::Ok) {
        return status;
    }
    status = extended(address, ext_set_temporary_waf);
    if (status != TransportStatus::Ok) {
        return status;
    }
    return extended(address, ext_activate);
}

TransportStatus ColourControl::step_colour_temperature(Address address, ColourStep step) {
    return extended(address, step == ColourStep::Cooler ? ext_tc_step_cooler : ext_tc_step_warmer);
}

TransportStatus ColourControl::store_colour_temperature_limit(Address address, TcLimit which,
                                                              Mirek value) {
    const auto status = first_of(
        {transport_.set_dtr(Dtr::Dtr0, static_cast<std::uint8_t>(value.value() & 0xFF)),
         transport_.set_dtr(Dtr::Dtr1, static_cast<std::uint8_t>(value.value() >> 8)),
         transport_.set_dtr(Dtr::Dtr2, static_cast<std::uint8_t>(which))});
    if (status != TransportStatus::Ok) {
        return status;
    }
    return extended_twice(address, ext_store_tc_limit);
}

ColourStatusRead ColourControl::query_colour_status(ShortAddress address) {
    const auto enabled = transport_.enable_device_type(device_type_colour);
    if (enabled != TransportStatus::Ok) {
        return {enabled, std::nullopt};
    }
    const auto answer = transport_.query(address, ext_query_colour_status);
    if (answer.status != TransportStatus::Ok || !answer.value) {
        return {answer.status, std::nullopt};
    }
    const auto raw = *answer.value;
    return {TransportStatus::Ok,
            ColourStatus{
                .xy_out_of_range = (raw & 0x01) != 0,
                .tc_out_of_range = (raw & 0x02) != 0,
                .auto_calibration_running = (raw & 0x04) != 0,
                .auto_calibration_successful = (raw & 0x08) != 0,
                .xy_active = (raw & 0x10) != 0,
                .tc_active = (raw & 0x20) != 0,
                .primary_n_active = (raw & 0x40) != 0,
                .rgbwaf_active = (raw & 0x80) != 0,
            }};
}

CapabilitiesRead ColourControl::query_colour_type_features(ShortAddress address) {
    const auto enabled = transport_.enable_device_type(device_type_colour);
    if (enabled != TransportStatus::Ok) {
        return {enabled, std::nullopt};
    }
    const auto answer = transport_.query(address, ext_query_colour_type_features);
    if (answer.status != TransportStatus::Ok || !answer.value) {
        return {answer.status, std::nullopt};
    }
    const auto raw = *answer.value;
    return {TransportStatus::Ok,
            ColourCapabilities{
                .xy_capable = (raw & 0x01) != 0,
                .tc_capable = (raw & 0x02) != 0,
                .primary_count = static_cast<std::uint8_t>((raw >> 2) & 0x07),
                .rgbwaf_channels = static_cast<std::uint8_t>((raw >> 5) & 0x07),
            }};
}

ColourValueRead ColourControl::query_colour_value(ShortAddress address, std::uint8_t selector) {
    auto status = transport_.set_dtr(Dtr::Dtr0, selector);
    if (status != TransportStatus::Ok) {
        return {status, std::nullopt, std::nullopt};
    }
    status = transport_.enable_device_type(device_type_colour);
    if (status != TransportStatus::Ok) {
        return {status, std::nullopt, std::nullopt};
    }
    const auto answer = transport_.query(address, ext_query_colour_value);
    if (answer.status != TransportStatus::Ok || !answer.value) {
        return {answer.status, std::nullopt, std::nullopt};
    }
    // The LSB of 16-bit values is left in DTR0; QUERY CONTENT DTR0 reads it.
    const auto low = transport_.query(address, cmd_query_content_dtr0);
    if (low.status != TransportStatus::Ok || !low.value) {
        return {low.status, answer.value, std::nullopt};
    }
    return {TransportStatus::Ok, answer.value, low.value};
}

TransportStatus ColourControl::copy_report_to_temporary(Address address) {
    return extended(address, ext_copy_report_to_temporary);
}

TransportStatus ColourControl::store_scene(Address address, Scene scene) {
    const auto stored = transport_.send_command(address, cmd_store_actual_level, true);
    if (stored != TransportStatus::Ok) {
        return stored;
    }
    const auto copied = copy_report_to_temporary(address);
    if (copied != TransportStatus::Ok) {
        return copied;
    }
    return transport_.send_command(address,
                                   static_cast<std::uint8_t>(cmd_store_dtr_as_scene_0 + scene.value()),
                                   true);
}

std::optional<Mirek> mirek_from_kelvin(std::uint32_t kelvin) noexcept {
    if (kelvin == 0) {
        return std::nullopt;
    }
    return Mirek::from((1'000'000u + kelvin / 2) / kelvin);
}

std::uint32_t kelvin_from_mirek(Mirek mirek) noexcept {
    return (1'000'000u + mirek.value() / 2) / mirek.value();
}

}
