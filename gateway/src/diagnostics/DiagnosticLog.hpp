#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "diagnostics/ErrorCode.hpp"

namespace gateway::diagnostics {

enum class LogSource : std::uint8_t {
    System = 0,
    Knx = 1,
    Dali = 2,
    Wifi = 3,
    Mqtt = 4,
    Ota = 5
};

enum class LogSeverity : std::uint8_t {
    Info = 0,
    Warning = 1,
    Error = 2,
    Critical = 3
};

inline constexpr std::uint16_t no_address = 0xFFFF;

struct LogEntry {
    std::uint32_t seq = 0;
    std::uint64_t epoch_ms = 0;
    std::uint32_t uptime_ms = 0;
    std::uint16_t count = 1;
    std::uint16_t address = no_address;
    ErrorCode code = ErrorCode::None;
    LogSource source = LogSource::System;
    LogSeverity severity = LogSeverity::Info;
    std::array<char, 48> detail{};
};

static_assert(std::is_trivially_copyable_v<LogEntry>, "LogEntry persists directly to NVS blob");

class LogBackend {
public:
    virtual ~LogBackend() = default;
    virtual bool load(void* data, std::size_t size) noexcept = 0;
    virtual bool save(const void* data, std::size_t size) noexcept = 0;
};

class DiagnosticLog final {
public:
    static constexpr std::size_t capacity = 64;

    explicit DiagnosticLog(LogBackend* backend = nullptr) noexcept;

    void set_backend(LogBackend* backend) noexcept { backend_ = backend; }
    bool load_from_storage() noexcept;
    bool flush_to_storage() noexcept;

    void sync_wall_clock(std::uint64_t epoch_ms, std::uint32_t uptime_ms) noexcept;
    [[nodiscard]] std::uint64_t current_epoch_ms(std::uint32_t uptime_ms) const noexcept;
    [[nodiscard]] bool clock_synced() const noexcept { return clock_synced_; }

    void record(LogSource source, LogSeverity severity, ErrorCode code, std::uint16_t address,
                const char* detail, std::uint32_t uptime_ms) noexcept;

    void clear() noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return state_.count; }
    [[nodiscard]] std::uint32_t total_logged() const noexcept { return state_.next_seq - 1; }
    [[nodiscard]] const LogEntry& entry_oldest_first(std::size_t index) const noexcept;
    [[nodiscard]] const LogEntry* latest() const noexcept;
    [[nodiscard]] bool dirty() const noexcept { return dirty_; }

private:
    struct PersistedState {
        std::uint32_t magic = 0x4B444C47u;  // "KDLG"
        std::uint32_t next_seq = 1;
        std::uint32_t head = 0;
        std::uint32_t count = 0;
        std::uint64_t saved_epoch_ms = 0;
        std::array<LogEntry, capacity> entries{};
    };

    static_assert(std::is_trivially_copyable_v<PersistedState>);

    LogBackend* backend_ = nullptr;
    PersistedState state_{};
    std::uint64_t base_epoch_ms_ = 0;
    std::uint32_t base_uptime_ms_ = 0;
    bool clock_synced_ = false;
    bool dirty_ = false;
};

[[nodiscard]] const char* source_name(LogSource source) noexcept;
[[nodiscard]] const char* severity_name(LogSeverity severity) noexcept;

}
