#pragma once

#include <cstdint>

// All DALI bus timing constants in microseconds (IEC 62386-101 ed2).
// Sources: values below are the publicly documented figures (IEC 62386-101
// ed2 summaries, Microchip AN1465) verified during C5a; the exact collision
// recovery table (IEC 62386-101 Table 25) was not accessible, so the break
// duration is an implementation choice bounded by receiver tolerance rules.
namespace gateway::dali::bus::timing {

// Half-bit period Te (~1/2400 s = 416.67 us). The integer value is fine:
// every consumer below applies receiver tolerance windows, never Te directly.
inline constexpr std::uint32_t te_us = 417;

// Transmitter half-bit limits for a multi-master unit (tighter than the
// single-master 367-467 us range).
inline constexpr std::uint32_t tx_half_min_us = 400;
inline constexpr std::uint32_t tx_half_max_us = 433;

// Receiver tolerance: a level lasting this long counts as one half-bit.
inline constexpr std::uint32_t rx_half_min_us = 333;
inline constexpr std::uint32_t rx_half_max_us = 500;
// Two half-bits.
inline constexpr std::uint32_t rx_full_min_us = 667;
inline constexpr std::uint32_t rx_full_max_us = 1000;
// Anything longer is idle (level high) or a bus fault/garbage (level low).
inline constexpr std::uint32_t idle_min_us = 1001;

// Stop condition / inter-frame idle: the bus stays high at least this long
// after every frame.
inline constexpr std::uint32_t stop_us = 2400;

// Backward frame: the reply's start bit may begin this window after the end
// of the forward frame. Anything earlier or later is not a valid answer.
inline constexpr std::uint32_t backward_min_us = 5500;
inline constexpr std::uint32_t backward_max_us = 10500;

// Multi-master settling windows: bus-idle time a unit must observe before
// starting a forward frame, indexed by (priority - 1). The actual wait is a
// random point inside the window.
inline constexpr std::uint32_t settling_count = 5;
inline constexpr std::uint32_t settling_min_us[settling_count] = {
    13500, 14900, 16300, 17900, 19500};
inline constexpr std::uint32_t settling_max_us[settling_count] = {
    14700, 16100, 17700, 19300, 21100};

// Collision handling: after detecting a collision the unit holds the bus
// active for a break so every receiver sees a timing violation, then
// re-arbitrates through the normal settling window. Break duration is an
// implementation choice (~3 Te) — the normative recovery time in
// IEC 62386-101 Table 25 was not accessible during implementation.
inline constexpr std::uint32_t collision_break_us = 1300;
inline constexpr std::uint8_t collision_attempts_max = 3;

// Send-twice commands: the repeat follows within ~100 ms.
inline constexpr std::uint32_t send_twice_gap_us = 40000;

}
