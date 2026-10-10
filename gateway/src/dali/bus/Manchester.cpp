#include "dali/bus/Manchester.hpp"

namespace gateway::dali::bus {

namespace {

void push_symbol(EncodedFrame& frame, Line level, std::uint32_t us) {
    if (frame.count < EncodedFrame::max_symbols) {
        frame.symbols[frame.count++] = TxSymbol{level, us};
    }
}

void push_half_bit(EncodedFrame& frame, Line level) {
    if (frame.count > 0 && frame.symbols[frame.count - 1].level == level) {
        frame.symbols[frame.count - 1].us += timing::te_us;
        return;
    }
    push_symbol(frame, level, timing::te_us);
}

}  // namespace

EncodedFrame encode_frame(std::uint32_t value, std::uint8_t bits) noexcept {
    EncodedFrame frame{};
    if (bits != 8 && bits != 16 && bits != 24) {
        return frame;
    }
    const std::uint32_t total = static_cast<std::uint32_t>(bits) + 1;
    for (std::uint32_t i = 0; i < total; ++i) {
        const bool one = (i == 0) || ((value >> (bits - i)) & 1U) != 0;
        push_half_bit(frame, one ? Line::Active : Line::Idle);
        push_half_bit(frame, one ? Line::Idle : Line::Active);
    }
    if (frame.count > 0 && frame.symbols[frame.count - 1].level == Line::Idle) {
        frame.symbols[frame.count - 1].us += timing::stop_us;
    } else {
        push_symbol(frame, Line::Idle, timing::stop_us);
    }
    return frame;
}

std::optional<Decoded> ManchesterDecoder::feed(Line level, std::uint32_t us) {
    if (error_seen_) {
        reset();  // a new burst starts a fresh decode
    }
    if (us < timing::rx_half_min_us) {
        return fail(ErrorKind::Timing);
    }
    std::uint8_t count;
    if (us <= timing::rx_half_max_us) {
        count = 1;
    } else if (us >= timing::rx_full_min_us && us <= timing::rx_full_max_us) {
        count = 2;
    } else if (us > timing::rx_full_max_us && level == Line::Idle) {
        // Long released line: the stop condition ends the frame.
        if (half_count_ == 0) {
            return std::nullopt;
        }
        half_bits_[half_count_++] = Line::Idle;
        return parse();
    } else {
        // Out of tolerance: between the windows, or an over-long active
        // run (garbage, collision break).
        return fail(ErrorKind::Timing);
    }
    if (half_count_ == 0 && level == Line::Idle) {
        return std::nullopt;  // pre-frame idle run; not part of the frame
    }
    if (half_count_ + count > max_half_bits) {
        return fail(ErrorKind::Length);
    }
    while (count-- > 0) {
        half_bits_[half_count_++] = level;
    }
    return std::nullopt;
}

std::optional<Decoded> ManchesterDecoder::finish() {
    if (error_seen_ || half_count_ == 0) {
        reset();
        return std::nullopt;
    }
    return parse();
}

void ManchesterDecoder::reset() {
    half_count_ = 0;
    error_seen_ = false;
}

std::optional<FrameError> ManchesterDecoder::fail(ErrorKind kind) {
    reset();
    error_seen_ = true;
    return FrameError{kind};
}

std::optional<Decoded> ManchesterDecoder::parse() {
    const std::uint8_t n = half_count_;
    reset();

    std::uint8_t i = 0;
    while (i < n && half_bits_[i] == Line::Idle) {
        ++i;  // leading idle run carried in the same burst
    }
    std::uint32_t value = 0;
    std::uint8_t bits = 0;
    for (; i + 1 < n; i += 2) {
        const Line a = half_bits_[i];
        const Line b = half_bits_[i + 1];
        if (a == b) {
            break;  // stop or Manchester violation, handled below
        }
        value = (value << 1) | (a == Line::Active ? 1U : 0U);
        ++bits;
    }
    // Everything after the last complete bit must be the idle stop tail.
    for (std::uint8_t k = i; k < n; ++k) {
        if (half_bits_[k] == Line::Active) {
            return FrameError{ErrorKind::Framing};
        }
    }
    if (bits == 0) {
        return std::nullopt;
    }
    // `bits` counts Manchester cells including the '1' start bit.
    if (bits != 9 && bits != 17 && bits != 25) {
        return FrameError{ErrorKind::Length};
    }
    if ((value & (1U << (bits - 1))) == 0) {
        return FrameError{ErrorKind::Framing};
    }
    const std::uint8_t payload_bits = bits - 1;
    const std::uint32_t payload = value & ((1U << payload_bits) - 1U);
    return ReceivedFrame{payload, payload_bits};
}

}
