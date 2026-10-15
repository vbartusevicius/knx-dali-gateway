#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "dali/Transport.hpp"

namespace gateway::diagnostics {

enum class TraceKind : std::uint8_t {
    Command16 = 0,
    Query16 = 1,
    Special16 = 2,
    DeviceCommand24 = 3,
    DeviceQuery24 = 4,
    UnsolicitedEvent24 = 5,
    BusFault = 6
};

struct TraceEntry {
    std::uint32_t seq = 0;
    std::uint64_t epoch_ms = 0;
    std::uint32_t uptime_ms = 0;
    std::uint32_t frame = 0;
    std::uint8_t bits = 16;
    std::uint8_t reply = 0;
    bool has_reply = false;
    bool send_twice = false;
    TraceKind kind = TraceKind::Command16;
    dali::TransportStatus status = dali::TransportStatus::Ok;
};

static_assert(std::is_trivially_copyable_v<TraceEntry>);

struct DecodedTrace {
    std::array<char, 24> target{};
    std::array<char, 40> mnemonic{};
    std::array<char, 64> detail{};
    const char* status_text = "OK";
};

class DaliDebugTrace final {
public:
    static constexpr std::size_t capacity = 512;

    DaliDebugTrace() noexcept = default;

    void start_session(std::uint32_t now_ms, bool clear_existing = false) noexcept;
    void stop_session() noexcept;
    void set_auto_trigger_from_dali(bool enabled) noexcept { auto_trigger_ = enabled; }
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] bool auto_trigger_from_dali() const noexcept { return auto_trigger_; }
    [[nodiscard]] std::uint32_t session_started_ms() const noexcept { return started_ms_; }

    void record(TraceKind kind, std::uint32_t frame, std::uint8_t bits, bool send_twice,
                dali::TransportStatus status, bool has_reply, std::uint8_t reply, std::uint64_t epoch_ms,
                std::uint32_t uptime_ms) noexcept;

    void clear() noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] std::uint32_t total_frames() const noexcept { return total_frames_; }
    [[nodiscard]] std::uint32_t total_errors() const noexcept { return total_errors_; }
    [[nodiscard]] std::uint32_t total_collisions() const noexcept { return total_collisions_; }
    [[nodiscard]] const TraceEntry& entry_oldest_first(std::size_t index) const noexcept;

private:
    std::array<TraceEntry, capacity> entries_{};
    std::uint32_t next_seq_ = 1;
    std::size_t head_ = 0;
    std::size_t count_ = 0;
    std::uint32_t started_ms_ = 0;
    std::uint32_t total_frames_ = 0;
    std::uint32_t total_errors_ = 0;
    std::uint32_t total_collisions_ = 0;
    bool active_ = true;
    bool auto_trigger_ = true;
};

void decode_trace_entry(const TraceEntry& entry, DecodedTrace& out) noexcept;

}
