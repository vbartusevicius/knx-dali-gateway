#include "dali/D4iMemoryBank.hpp"

#include <cstdint>
#include <optional>

#include "dali/Commands.hpp"
#include "dali/ShortAddress.hpp"
#include "dali/Transport.hpp"

namespace gateway::dali {
namespace {

constexpr std::uint8_t memory_bank_202 = 202U;
constexpr std::uint8_t offset_bank_id = 0x02U;
constexpr std::uint8_t offset_energy_scale = 0x03U;
constexpr std::uint8_t offset_energy_msb = 0x06U;
constexpr std::uint8_t offset_power_scale = 0x0AU;
constexpr std::uint8_t offset_power_msb = 0x0BU;

int decode_scale_factor(std::uint8_t raw) noexcept {
    return raw >= 128U ? (static_cast<int>(raw) - 256) : static_cast<int>(raw);
}

std::optional<std::uint8_t> read_mb_byte(Transport& transport, ShortAddress address, std::uint8_t offset) {
    if (transport.set_dtr(Dtr::Dtr0, offset) != TransportStatus::Ok) {
        return std::nullopt;
    }
    const auto r = transport.query(address, cmd::read_memory_location);
    if (r.status == TransportStatus::Ok) {
        return r.value;
    }
    return std::nullopt;
}

}  // namespace

std::optional<D4iPowerReading> read_d4i_power_energy(Transport& transport, ShortAddress address) {
    if (transport.set_dtr(Dtr::Dtr1, memory_bank_202) != TransportStatus::Ok ||
        transport.set_dtr(Dtr::Dtr0, offset_bank_id) != TransportStatus::Ok) {
        return std::nullopt;
    }
    const auto bank_id = transport.query(address, cmd::read_memory_location);
    if (bank_id.status != TransportStatus::Ok || !bank_id.value || *bank_id.value != memory_bank_202) {
        return std::nullopt;
    }

    D4iPowerReading reading{};

    const auto p_scale = read_mb_byte(transport, address, offset_power_scale);
    const auto p0 = read_mb_byte(transport, address, offset_power_msb);
    const auto p1 = read_mb_byte(transport, address, static_cast<std::uint8_t>(offset_power_msb + 1U));
    const auto p2 = read_mb_byte(transport, address, static_cast<std::uint8_t>(offset_power_msb + 2U));
    const auto p3 = read_mb_byte(transport, address, static_cast<std::uint8_t>(offset_power_msb + 3U));
    if (p_scale && p0 && p1 && p2 && p3) {
        const std::uint32_t raw_p = (static_cast<std::uint32_t>(*p0) << 24U) |
                                    (static_cast<std::uint32_t>(*p1) << 16U) |
                                    (static_cast<std::uint32_t>(*p2) << 8U) | static_cast<std::uint32_t>(*p3);
        std::uint32_t deciwatts = raw_p;
        int exp10 = decode_scale_factor(*p_scale) + 1;
        while (exp10 > 0 && deciwatts < 65535U) {
            deciwatts *= 10U;
            --exp10;
        }
        while (exp10 < 0) {
            deciwatts = (deciwatts + 5U) / 10U;
            ++exp10;
        }
        reading.active_power_deciwatts =
            deciwatts > 65535U ? std::uint16_t{65535U} : static_cast<std::uint16_t>(deciwatts);
    }

    const auto e_scale = read_mb_byte(transport, address, offset_energy_scale);
    const auto e0 = read_mb_byte(transport, address, offset_energy_msb);
    const auto e1 = read_mb_byte(transport, address, static_cast<std::uint8_t>(offset_energy_msb + 1U));
    const auto e2 = read_mb_byte(transport, address, static_cast<std::uint8_t>(offset_energy_msb + 2U));
    const auto e3 = read_mb_byte(transport, address, static_cast<std::uint8_t>(offset_energy_msb + 3U));
    if (e_scale && e0 && e1 && e2 && e3) {
        std::uint32_t wh = (static_cast<std::uint32_t>(*e0) << 24U) |
                           (static_cast<std::uint32_t>(*e1) << 16U) |
                           (static_cast<std::uint32_t>(*e2) << 8U) | static_cast<std::uint32_t>(*e3);
        int exp10 = decode_scale_factor(*e_scale);
        while (exp10 > 0) {
            wh *= 10U;
            --exp10;
        }
        while (exp10 < 0) {
            wh = (wh + 5U) / 10U;
            ++exp10;
        }
        reading.active_energy_wh = wh;
    }

    return reading;
}

}  // namespace gateway::dali
