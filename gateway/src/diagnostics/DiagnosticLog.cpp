#include "diagnostics/DiagnosticLog.hpp"

#include <array>
#include <cstdint>
#include <cstring>

#include "diagnostics/ErrorCode.hpp"

namespace gateway::diagnostics {
namespace {

constexpr std::uint32_t storage_magic = 0x4B444C47u;
constexpr std::uint32_t coalesce_window_ms = 30'000;

void copy_detail(std::array<char, 48>& dest, const char* src) noexcept {
    dest.fill('\0');
    if (!src) {
        return;
    }
    std::strncpy(dest.data(), src, dest.size() - 1);
}

}  // namespace

DiagnosticLog::DiagnosticLog(LogBackend* backend) noexcept : backend_(backend) {}

bool DiagnosticLog::load_from_storage() noexcept {
    if (!backend_) {
        return false;
    }
    PersistedState loaded{};
    if (!backend_->load(&loaded, sizeof(loaded))) {
        return false;
    }
    if (loaded.magic != storage_magic || loaded.count > capacity || loaded.head >= capacity ||
        loaded.next_seq == 0) {
        return false;
    }
    state_ = loaded;
    if (state_.saved_epoch_ms > 0) {
        base_epoch_ms_ = state_.saved_epoch_ms;
        base_uptime_ms_ = 0;
    }
    dirty_ = false;
    return true;
}

bool DiagnosticLog::flush_to_storage() noexcept {
    if (!backend_ || !dirty_) {
        return true;
    }
    if (!backend_->save(&state_, sizeof(state_))) {
        return false;
    }
    dirty_ = false;
    return true;
}

void DiagnosticLog::sync_wall_clock(std::uint64_t epoch_ms, std::uint32_t uptime_ms) noexcept {
    if (epoch_ms == 0) {
        return;
    }
    base_epoch_ms_ = epoch_ms;
    base_uptime_ms_ = uptime_ms;
    clock_synced_ = true;
    state_.saved_epoch_ms = epoch_ms;
    for (std::size_t i = 0; i < state_.count; ++i) {
        auto& item = state_.entries[(state_.head + capacity - state_.count + i) % capacity];
        if (item.epoch_ms == 0 && item.uptime_ms <= uptime_ms) {
            const auto delta = static_cast<std::uint64_t>(uptime_ms - item.uptime_ms);
            if (epoch_ms >= delta) {
                item.epoch_ms = epoch_ms - delta;
            }
        }
    }
    dirty_ = true;
}

std::uint64_t DiagnosticLog::current_epoch_ms(std::uint32_t uptime_ms) const noexcept {
    if (base_epoch_ms_ == 0) {
        return 0;
    }
    if (uptime_ms >= base_uptime_ms_) {
        return base_epoch_ms_ + static_cast<std::uint64_t>(uptime_ms - base_uptime_ms_);
    }
    return base_epoch_ms_;
}

void DiagnosticLog::record(LogSource source, LogSeverity severity, ErrorCode code, std::uint16_t address,
                           const char* detail, std::uint32_t uptime_ms) noexcept {
    const auto epoch = current_epoch_ms(uptime_ms);
    if (state_.count > 0) {
        auto& prev = state_.entries[(state_.head + capacity - 1) % capacity];
        if (prev.source == source && prev.code == code && prev.address == address &&
            uptime_ms >= prev.uptime_ms && (uptime_ms - prev.uptime_ms) <= coalesce_window_ms) {
            if (prev.count < 0xFFFFu) {
                ++prev.count;
            }
            prev.uptime_ms = uptime_ms;
            prev.epoch_ms = epoch;
            if (detail && detail[0] != '\0') {
                copy_detail(prev.detail, detail);
            }
            dirty_ = true;
            return;
        }
    }

    LogEntry entry{};
    entry.seq = state_.next_seq++;
    entry.epoch_ms = epoch;
    entry.uptime_ms = uptime_ms;
    entry.count = 1;
    entry.address = address;
    entry.code = code;
    entry.source = source;
    entry.severity = severity;
    copy_detail(entry.detail, detail);

    state_.entries[state_.head] = entry;
    state_.head = (state_.head + 1) % capacity;
    if (state_.count < capacity) {
        ++state_.count;
    }
    if (epoch > 0) {
        state_.saved_epoch_ms = epoch;
    }
    dirty_ = true;
    if (severity >= LogSeverity::Error && backend_) {
        (void)flush_to_storage();
    }
}

void DiagnosticLog::clear() noexcept {
    state_.head = 0;
    state_.count = 0;
    state_.entries = {};
    dirty_ = true;
    if (backend_) {
        (void)flush_to_storage();
    }
}

const LogEntry& DiagnosticLog::entry_oldest_first(std::size_t index) const noexcept {
    if (state_.count == 0 || index >= state_.count) {
        return state_.entries[0];
    }
    const auto start = (state_.head + capacity - state_.count) % capacity;
    return state_.entries[(start + index) % capacity];
}

const LogEntry* DiagnosticLog::latest() const noexcept {
    if (state_.count == 0) {
        return nullptr;
    }
    return &state_.entries[(state_.head + capacity - 1) % capacity];
}

const char* source_name(LogSource source) noexcept {
    switch (source) {
    case LogSource::System:
        return "SYSTEM";
    case LogSource::Knx:
        return "KNX";
    case LogSource::Dali:
        return "DALI";
    case LogSource::Wifi:
        return "WIFI";
    case LogSource::Mqtt:
        return "MQTT";
    case LogSource::Ota:
        return "OTA";
    }
    return "SYSTEM";
}

const char* severity_name(LogSeverity severity) noexcept {
    switch (severity) {
    case LogSeverity::Info:
        return "INFO";
    case LogSeverity::Warning:
        return "WARN";
    case LogSeverity::Error:
        return "ERROR";
    case LogSeverity::Critical:
        return "CRITICAL";
    }
    return "INFO";
}

}
