#pragma once

#include <cstdint>
#include <optional>
#include <variant>

#include "dali/bus/Timing.hpp"

// IEC 62386-101 physical layer: Manchester-coded frames of 8 (backward),
// 16 (forward, control gear) or 24 (forward, control devices) bits.
// Bit '1' is a rising edge (bus driven then released), bit '0' a falling
// edge; every frame starts with a '1' start bit and ends with the bus
// released for at least the stop condition. Level naming follows the wire:
// Active = bus driven low, Idle = bus released high.
namespace gateway::dali::bus {

enum class Line : bool {
    Active = false,
    Idle = true
};

struct TxSymbol {
    Line level;
    std::uint32_t us;
};

// Run-length encoded frame ready for a waveform transmitter: consecutive
// half-bits of equal level are merged, and a final stop symbol is included.
struct EncodedFrame {
    static constexpr std::uint8_t max_symbols = 54;
    TxSymbol symbols[max_symbols];
    std::uint8_t count = 0;
};

// Encodes `bits` (8, 16 or 24) of `value`, MSB first, with start bit and
// stop symbol. Returns an empty EncodedFrame for unsupported lengths.
EncodedFrame encode_frame(std::uint32_t value, std::uint8_t bits) noexcept;

struct ReceivedFrame {
    std::uint32_t value;
    std::uint8_t bits;
};

enum class ErrorKind {
    Timing,  // a level run outside every tolerance window
    Length,  // valid half-bits but the bit count is not 8/16/24
    Framing  // start bit missing or a mid-frame Manchester violation
};

struct FrameError {
    ErrorKind kind;
};

using Decoded = std::variant<ReceivedFrame, FrameError>;

// Incremental Manchester decoder fed with observed (level, duration) runs.
// Errors are reported eagerly; a frame is emitted as soon as the stop
// condition is observed — either by a long Idle run passed to feed() or by
// an explicit finish() call when the receiver reports end-of-burst.
class ManchesterDecoder {
public:
    // Feeds one level run. Returns a Decoded when the run completes the
    // frame (long Idle tail) or proves it invalid.
    std::optional<Decoded> feed(Line level, std::uint32_t us);
    // Finalizes a burst that ended without a long Idle run in-band
    // (receiver idle interrupt). Returns nothing if no frame was pending.
    std::optional<Decoded> finish();
    void reset();

private:
    static constexpr std::uint8_t max_half_bits = 64;
    Line half_bits_[max_half_bits]{};
    std::uint8_t half_count_ = 0;
    bool error_seen_ = false;

    std::optional<Decoded> parse();
    std::optional<FrameError> fail(ErrorKind kind);
};

}
