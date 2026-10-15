#include "diagnostics/DaliDebugTrace.hpp"

#include <cstdint>
#include <cstdio>

#include "dali/Commands.hpp"
#include "dali/SpecialCommands.hpp"
#include "dali/Transport.hpp"

namespace gateway::diagnostics {
namespace {

const char* transport_status_label(dali::TransportStatus status) noexcept {
    switch (status) {
    case dali::TransportStatus::Ok:
        return "OK";
    case dali::TransportStatus::Unconfigured:
        return "UNCONFIGURED";
    case dali::TransportStatus::Timeout:
        return "TIMEOUT";
    case dali::TransportStatus::Collision:
        return "COLLISION";
    case dali::TransportStatus::InvalidResponse:
        return "INVALID_REPLY";
    case dali::TransportStatus::Unsupported:
        return "UNSUPPORTED";
    case dali::TransportStatus::Failure:
        return "BUS_FAILURE";
    }
    return "UNKNOWN";
}

const char* gear_opcode_name(std::uint8_t opcode) noexcept {
    using namespace dali::cmd;
    if (opcode >= go_to_scene_0 && opcode < go_to_scene_0 + 16) {
        return "GO_TO_SCENE";
    }
    if (opcode >= store_dtr_as_scene_0 && opcode < store_dtr_as_scene_0 + 16) {
        return "STORE_DTR_AS_SCENE";
    }
    if (opcode >= remove_from_scene_0 && opcode < remove_from_scene_0 + 16) {
        return "REMOVE_FROM_SCENE";
    }
    if (opcode >= add_to_group_0 && opcode < add_to_group_0 + 16) {
        return "ADD_TO_GROUP";
    }
    if (opcode >= remove_from_group_0 && opcode < remove_from_group_0 + 16) {
        return "REMOVE_FROM_GROUP";
    }
    if (opcode >= query_scene_level_0 && opcode < query_scene_level_0 + 16) {
        return "QUERY_SCENE_LEVEL";
    }
    switch (opcode) {
    case off:
        return "OFF";
    case up:
        return "UP";
    case down:
        return "DOWN";
    case step_up:
        return "STEP_UP";
    case step_down:
        return "STEP_DOWN";
    case recall_max_level:
        return "RECALL_MAX_LEVEL";
    case recall_min_level:
        return "RECALL_MIN_LEVEL";
    case on_and_step_up:
        return "ON_AND_STEP_UP";
    case reset:
        return "RESET";
    case store_actual_level_in_dtr0:
        return "STORE_ACTUAL_IN_DTR0";
    case identify_device:
        return "IDENTIFY_DEVICE";
    case set_max_level:
        return "SET_MAX_LEVEL";
    case set_min_level:
        return "SET_MIN_LEVEL";
    case set_system_failure_level:
        return "SET_SYS_FAIL_LEVEL";
    case set_power_on_level:
        return "SET_POWER_ON_LEVEL";
    case set_fade_time:
        return "SET_FADE_TIME";
    case set_fade_rate:
        return "SET_FADE_RATE";
    case store_dtr_as_short_address:
        return "STORE_DTR_SHORT_ADDR";
    case query_status:
        return "QUERY_STATUS";
    case query_control_gear:
        return "QUERY_CONTROL_GEAR";
    case query_lamp_failure:
        return "QUERY_LAMP_FAILURE";
    case query_lamp_power_on:
        return "QUERY_LAMP_POWER_ON";
    case query_limit_error:
        return "QUERY_LIMIT_ERROR";
    case query_reset_state:
        return "QUERY_RESET_STATE";
    case query_missing_short_address:
        return "QUERY_MISSING_SHORT";
    case query_version_number:
        return "QUERY_VERSION";
    case query_content_dtr0:
        return "QUERY_DTR0";
    case query_device_type:
        return "QUERY_DEVICE_TYPE";
    case query_physical_minimum:
        return "QUERY_PHYSICAL_MIN";
    case query_power_failure:
        return "QUERY_POWER_FAILURE";
    case query_actual_level:
        return "QUERY_ACTUAL_LEVEL";
    case query_max_level:
        return "QUERY_MAX_LEVEL";
    case query_min_level:
        return "QUERY_MIN_LEVEL";
    case query_power_on_level:
        return "QUERY_POWER_ON_LEVEL";
    case query_system_failure_level:
        return "QUERY_SYS_FAIL_LEVEL";
    case query_fade_time_rate:
        return "QUERY_FADE_TIME_RATE";
    case query_groups_0_7:
        return "QUERY_GROUPS_0_7";
    case query_groups_8_15:
        return "QUERY_GROUPS_8_15";
    case 0xE0:
        return "DT8_SET_TEMP_X";
    case 0xE1:
        return "DT8_SET_TEMP_Y";
    case 0xE2:
        return "DT8_ACTIVATE";
    case 0xE3:
        return "DT8_X_STEP_UP";
    case 0xE4:
        return "DT8_X_STEP_DOWN";
    case 0xE5:
        return "DT8_Y_STEP_UP";
    case 0xE6:
        return "DT8_Y_STEP_DOWN";
    case 0xE7:
        return "DT8_SET_TEMP_TC";
    case 0xE8:
        return "DT8_TC_COOLER";
    case 0xE9:
        return "DT8_TC_WARMER";
    case 0xEA:
        return "DT8_SET_PRIMARY_N";
    case 0xEB:
        return "DT8_SET_RGB_DIM";
    case 0xEC:
        return "DT8_SET_WAF_DIM";
    case 0xED:
        return "DT8_SET_RGBWAF_CTRL";
    case 0xEE:
        return "DT8_COPY_REPORT_TEMP";
    case 0xF0:
        return "DT8_STORE_TC_LIMIT";
    case 0xF7:
        return "DT8_QUERY_GEAR_FEATURES";
    case 0xF8:
        return "DT8_QUERY_COLOUR_STATUS";
    case 0xF9:
        return "DT8_QUERY_COLOUR_TYPES";
    case 0xFA:
        return "DT8_QUERY_COLOUR_VALUE";
    default:
        return "CMD";
    }
}

const char* special_opcode_name(std::uint8_t opcode) noexcept {
    using namespace dali::special;
    switch (opcode) {
    case terminate:
        return "TERMINATE";
    case dtr0:
        return "SET_DTR0";
    case initialise:
        return "INITIALISE";
    case randomise:
        return "RANDOMISE";
    case compare:
        return "COMPARE";
    case withdraw:
        return "WITHDRAW";
    case ping:
        return "PING";
    case searchaddr_h:
        return "SEARCHADDRH";
    case searchaddr_m:
        return "SEARCHADDRM";
    case searchaddr_l:
        return "SEARCHADDRL";
    case program_short_address:
        return "PROGRAM_SHORT_ADDR";
    case verify_short_address:
        return "VERIFY_SHORT_ADDR";
    case query_short_address:
        return "QUERY_SHORT_ADDR";
    case enable_device_type:
        return "ENABLE_DEVICE_TYPE";
    case dtr1:
        return "SET_DTR1";
    case dtr2:
        return "SET_DTR2";
    default:
        return "SPECIAL";
    }
}

void decode_16bit(const TraceEntry& entry, DecodedTrace& out) noexcept {
    const auto addr_byte = static_cast<std::uint8_t>((entry.frame >> 8) & 0xFFu);
    const auto data_byte = static_cast<std::uint8_t>(entry.frame & 0xFFu);

    if (entry.kind == TraceKind::Special16 || ((addr_byte & 0xE1u) == 0xA1u) ||
        ((addr_byte & 0xE1u) == 0xC1u)) {
        std::snprintf(out.target.data(), out.target.size(), "SPECIAL");
        std::snprintf(out.mnemonic.data(), out.mnemonic.size(), "%s", special_opcode_name(addr_byte));
        if (entry.has_reply) {
            std::snprintf(out.detail.data(), out.detail.size(), "op=0x%02X data=0x%02X -> reply=0x%02X",
                          addr_byte, data_byte, entry.reply);
        } else {
            std::snprintf(out.detail.data(), out.detail.size(), "op=0x%02X data=0x%02X (%u)%s", addr_byte,
                          data_byte, data_byte, entry.send_twice ? " [2x]" : "");
        }
        return;
    }

    if (addr_byte == 0xFEu || addr_byte == 0xFFu) {
        std::snprintf(out.target.data(), out.target.size(), "BROADCAST");
    } else if ((addr_byte & 0x80u) != 0) {
        std::snprintf(out.target.data(), out.target.size(), "G%02u",
                      static_cast<unsigned>((addr_byte >> 1) & 0x0Fu));
    } else {
        std::snprintf(out.target.data(), out.target.size(), "A%02u",
                      static_cast<unsigned>((addr_byte >> 1) & 0x3Fu));
    }

    const bool is_command = (addr_byte & 0x01u) != 0;
    if (!is_command) {
        const unsigned pct = (static_cast<unsigned>(data_byte) * 100u + 127u) / 254u;
        std::snprintf(out.mnemonic.data(), out.mnemonic.size(), "DAPC");
        std::snprintf(out.detail.data(), out.detail.size(), "arc=%u (%u%%)", data_byte, pct);
        return;
    }

    std::snprintf(out.mnemonic.data(), out.mnemonic.size(), "%s", gear_opcode_name(data_byte));
    if (entry.has_reply) {
        if (data_byte == dali::cmd::query_status) {
            std::snprintf(out.detail.data(), out.detail.size(),
                          "reply=0x%02X [gearErr=%u lampFail=%u arcOn=%u lim=%u pwrCyc=%u]", entry.reply,
                          static_cast<unsigned>((entry.reply >> 0) & 1u),
                          static_cast<unsigned>((entry.reply >> 1) & 1u),
                          static_cast<unsigned>((entry.reply >> 2) & 1u),
                          static_cast<unsigned>((entry.reply >> 3) & 1u),
                          static_cast<unsigned>((entry.reply >> 7) & 1u));
        } else {
            std::snprintf(out.detail.data(), out.detail.size(), "cmd=0x%02X -> reply=0x%02X (%u)", data_byte,
                          entry.reply, entry.reply);
        }
    } else {
        std::snprintf(out.detail.data(), out.detail.size(), "cmd=0x%02X%s", data_byte,
                      entry.send_twice ? " [send-twice]" : "");
    }
}

void decode_24bit(const TraceEntry& entry, DecodedTrace& out) noexcept {
    const auto b0 = static_cast<std::uint8_t>((entry.frame >> 16) & 0xFFu);
    const auto b1 = static_cast<std::uint8_t>((entry.frame >> 8) & 0xFFu);
    const auto b2 = static_cast<std::uint8_t>(entry.frame & 0xFFu);

    if (entry.kind == TraceKind::UnsolicitedEvent24 || (b0 & 0x01u) == 0) {
        const unsigned dev = (b0 >> 1) & 0x3Fu;
        const unsigned inst = b1 & 0x1Fu;
        const unsigned event_data = ((static_cast<unsigned>(b1) & 0x03u) << 8) | b2;
        std::snprintf(out.target.data(), out.target.size(), "DEV%02u/I%02u", dev, inst);
        std::snprintf(out.mnemonic.data(), out.mnemonic.size(), "PART103_EVENT");
        std::snprintf(out.detail.data(), out.detail.size(), "raw=0x%06lX event=0x%03X (%u)",
                      static_cast<unsigned long>(entry.frame & 0xFFFFFFu), event_data, event_data);
        return;
    }

    if (b0 == 0xFFu) {
        std::snprintf(out.target.data(), out.target.size(), "DEV_BCAST");
    } else if ((b0 & 0x80u) == 0) {
        std::snprintf(out.target.data(), out.target.size(), "DEV%02u/I%02u",
                      static_cast<unsigned>((b0 >> 1) & 0x3Fu), static_cast<unsigned>(b1 & 0x1Fu));
    } else {
        std::snprintf(out.target.data(), out.target.size(), "DEV_SPECIAL");
    }
    std::snprintf(out.mnemonic.data(), out.mnemonic.size(),
                  entry.kind == TraceKind::DeviceQuery24 ? "DEV_QUERY_24" : "DEV_CMD_24");
    if (entry.has_reply) {
        std::snprintf(out.detail.data(), out.detail.size(), "frame=0x%06lX -> reply=0x%02X",
                      static_cast<unsigned long>(entry.frame & 0xFFFFFFu), entry.reply);
    } else {
        std::snprintf(out.detail.data(), out.detail.size(), "frame=0x%06lX%s",
                      static_cast<unsigned long>(entry.frame & 0xFFFFFFu), entry.send_twice ? " [2x]" : "");
    }
}

}  // namespace

void DaliDebugTrace::start_session(std::uint32_t now_ms, bool clear_existing) noexcept {
    if (clear_existing) {
        clear();
    }
    started_ms_ = now_ms;
    active_ = true;
}

void DaliDebugTrace::stop_session() noexcept {
    active_ = false;
}

void DaliDebugTrace::record(TraceKind kind, std::uint32_t frame, std::uint8_t bits, bool send_twice,
                            dali::TransportStatus status, bool has_reply, std::uint8_t reply,
                            std::uint64_t epoch_ms, std::uint32_t uptime_ms) noexcept {
    ++total_frames_;
    if (status == dali::TransportStatus::Collision) {
        ++total_collisions_;
    } else if (status != dali::TransportStatus::Ok && status != dali::TransportStatus::Timeout) {
        ++total_errors_;
    }

    if (!active_ && auto_trigger_ &&
        (kind == TraceKind::UnsolicitedEvent24 || status == dali::TransportStatus::Collision ||
         status == dali::TransportStatus::InvalidResponse || status == dali::TransportStatus::Failure)) {
        active_ = true;
        started_ms_ = uptime_ms;
    }

    if (!active_) {
        return;
    }

    TraceEntry item{};
    item.seq = next_seq_++;
    item.epoch_ms = epoch_ms;
    item.uptime_ms = uptime_ms;
    item.frame = frame;
    item.bits = bits;
    item.reply = reply;
    item.has_reply = has_reply;
    item.send_twice = send_twice;
    item.kind = kind;
    item.status = status;

    entries_[head_] = item;
    head_ = (head_ + 1) % capacity;
    if (count_ < capacity) {
        ++count_;
    }
}

void DaliDebugTrace::clear() noexcept {
    head_ = 0;
    count_ = 0;
    entries_ = {};
}

const TraceEntry& DaliDebugTrace::entry_oldest_first(std::size_t index) const noexcept {
    if (count_ == 0 || index >= count_) {
        return entries_[0];
    }
    const auto start = (head_ + capacity - count_) % capacity;
    return entries_[(start + index) % capacity];
}

void decode_trace_entry(const TraceEntry& entry, DecodedTrace& out) noexcept {
    out = {};
    out.status_text = transport_status_label(entry.status);
    if (entry.bits == 24 || entry.kind == TraceKind::DeviceCommand24 ||
        entry.kind == TraceKind::DeviceQuery24 || entry.kind == TraceKind::UnsolicitedEvent24) {
        decode_24bit(entry, out);
        return;
    }
    decode_16bit(entry, out);
}

}
