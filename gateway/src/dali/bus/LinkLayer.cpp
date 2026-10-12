#include "dali/bus/LinkLayer.hpp"

#include <cstdint>
#include <optional>

#include "dali/bus/Manchester.hpp"
#include "dali/bus/Timing.hpp"

namespace gateway::dali::bus {

namespace {

bool reached(std::uint32_t now, std::uint32_t target) {
    return static_cast<std::int32_t>(now - target) >= 0;
}

}  // namespace

LinkLayer::LinkLayer(Sink& sink, std::uint32_t (*random)(void*), void* random_context)
    : sink_(sink), random_(random), random_context_(random_context) {}

bool LinkLayer::request(std::uint32_t value, std::uint8_t bits, Priority priority, bool expect_reply,
                        std::uint32_t repeat_gap_us) {
    if (queue_count_ >= queue_size) {
        return false;
    }
    Pending& pending = queue_[queue_count_++];
    pending = Pending{};
    pending.value = value;
    pending.bits = bits;
    pending.priority = priority;
    pending.expect_reply = expect_reply;
    pending.repeat_gap_us = repeat_gap_us;
    pending.repeats_left = repeat_gap_us > 0 ? 1 : 0;
    return true;
}

void LinkLayer::note_activity(std::uint32_t now_us) {
    last_activity_us_ = now_us;
    settle_computed_ = false;
}

void LinkLayer::emit_result(TxOutcome outcome, std::uint8_t reply) {
    if (result_count_ >= result_size) {
        return;
    }
    results_[result_count_++] = TxResult{outcome, reply};
}

void LinkLayer::emit_event(const BusFrame& frame) {
    if (event_count_ >= event_size) {
        ++events_dropped_;
        return;
    }
    events_[event_count_++] = frame;
}

std::optional<TxResult> LinkLayer::take_result() {
    if (result_count_ == 0) {
        return std::nullopt;
    }
    const TxResult result = results_[0];
    for (std::uint8_t i = 1; i < result_count_; ++i) {
        results_[i - 1] = results_[i];
    }
    --result_count_;
    return result;
}

std::optional<BusFrame> LinkLayer::take_event() {
    if (event_count_ == 0) {
        return std::nullopt;
    }
    const BusFrame frame = events_[0];
    for (std::uint8_t i = 1; i < event_count_; ++i) {
        events_[i - 1] = events_[i];
    }
    --event_count_;
    return frame;
}

bool LinkLayer::next_request() {
    if (queue_count_ == 0) {
        return false;
    }
    std::uint8_t best = 0;
    for (std::uint8_t i = 1; i < queue_count_; ++i) {
        if (static_cast<std::uint8_t>(queue_[i].priority) <
            static_cast<std::uint8_t>(queue_[best].priority)) {
            best = i;
        }
    }
    active_ = queue_[best];
    for (std::uint8_t i = best + 1; i < queue_count_; ++i) {
        queue_[i - 1] = queue_[i];
    }
    --queue_count_;
    has_active_ = true;
    return true;
}

void LinkLayer::begin_settling() {
    state_ = State::Idle;
    settle_computed_ = false;
}

void LinkLayer::begin_transmission(std::uint32_t now_us) {
    const EncodedFrame frame = encode_frame(active_.value, active_.bits);
    collision_seen_ = false;
    echo_ok_ = false;
    reply_resolved_ = false;
    sink_.start_transmit(frame);
    state_ = State::Transmitting;
    note_activity(now_us);
}

void LinkLayer::enter_break(std::uint32_t now_us) {
    ++active_.attempts;
    if (active_.attempts >= timing::collision_attempts_max) {
        finish_request(TxOutcome::Collision, 0, now_us);
        return;
    }
    sink_.drive_break(timing::collision_break_us);
    wait_until_us_ = now_us + timing::collision_break_us;
    state_ = State::Breaking;
}

void LinkLayer::complete_attempt(std::uint32_t now_us) {
    if (active_.expect_reply) {
        wait_until_us_ = tx_end_us_ + timing::backward_max_us;
        state_ = State::WaitReply;
        return;
    }
    finish_request(TxOutcome::Sent, 0, now_us);
}

void LinkLayer::finish_request(TxOutcome outcome, std::uint8_t reply, std::uint32_t now_us) {
    if (outcome == TxOutcome::Sent && active_.repeats_left > 0) {
        --active_.repeats_left;
        repeat_at_us_ = now_us + active_.repeat_gap_us;
        state_ = State::RepeatGap;
        return;
    }
    emit_result(outcome, reply);
    has_active_ = false;
    state_ = State::Idle;
    last_activity_us_ = now_us;
}

void LinkLayer::on_decoded(const ReceivedFrame& frame, std::uint32_t frame_start_us) {
    switch (state_) {
    case State::Transmitting:
    case State::AwaitEcho:
        if (!echo_ok_ && frame.value == active_.value && frame.bits == active_.bits) {
            echo_ok_ = true;
        } else {
            collision_seen_ = true;
        }
        note_activity(frame_start_us);
        return;
    case State::WaitReply:
        if (!reply_resolved_ && frame.bits == 8 &&
            reached(frame_start_us, tx_end_us_ + timing::backward_min_us) &&
            !reached(frame_start_us, tx_end_us_ + timing::backward_max_us + 1)) {
            reply_resolved_ = true;
            finish_request(TxOutcome::Reply, static_cast<std::uint8_t>(frame.value), frame_start_us);
            return;
        }
        emit_event(frame);
        note_activity(frame_start_us);
        return;
    case State::Idle:
    case State::Breaking:
    case State::RepeatGap:
        emit_event(frame);
        note_activity(frame_start_us);
        return;
    }
}

void LinkLayer::on_decoder_error(std::uint32_t now_us) {
    switch (state_) {
    case State::Transmitting:
    case State::AwaitEcho:
        collision_seen_ = true;
        break;
    case State::WaitReply:
        if (!reply_resolved_) {
            reply_resolved_ = true;
            finish_request(TxOutcome::ReplyCollision, 0, now_us);
        }
        break;
    case State::Idle:
    case State::Breaking:
    case State::RepeatGap:
        break;
    }
    note_activity(now_us);
}

void LinkLayer::on_tx_complete(std::uint32_t now_us) {
    tx_end_us_ = now_us;
    note_activity(now_us);
    if (state_ != State::Transmitting) {
        return;
    }
    if (collision_seen_) {
        enter_break(now_us);
        return;
    }
    if (echo_ok_) {
        complete_attempt(now_us);
        return;
    }
    // The echo burst may still be decoding; give it a short grace.
    wait_until_us_ = now_us + echo_grace_us;
    state_ = State::AwaitEcho;
}

void LinkLayer::on_rx_active(std::uint32_t now_us) {
    note_activity(now_us);
}

void LinkLayer::poll(std::uint32_t now_us) {
    switch (state_) {
    case State::Idle: {
        if (!has_active_ && !next_request()) {
            return;
        }
        if (!settle_computed_) {
            const auto index = static_cast<std::uint8_t>(active_.priority);
            const std::uint32_t min = timing::settling_min_us[index];
            const std::uint32_t span = timing::settling_max_us[index] - min + 1;
            const std::uint32_t jitter = random_ != nullptr ? random_(random_context_) % span : 0;
            settle_target_us_ = last_activity_us_ + min + jitter;
            settle_computed_ = true;
        }
        if (reached(now_us, settle_target_us_)) {
            begin_transmission(now_us);
        }
        return;
    }
    case State::AwaitEcho:
        if (!reached(now_us, wait_until_us_)) {
            return;
        }
        if (echo_ok_ && !collision_seen_) {
            complete_attempt(now_us);
            return;
        }
        // Mismatch, garbage, or no echo at all: treat as a collision.
        enter_break(now_us);
        return;
    case State::WaitReply:
        if (!reply_resolved_ && now_us > wait_until_us_) {
            finish_request(TxOutcome::NoReply, 0, now_us);
        }
        return;
    case State::Breaking:
        if (reached(now_us, wait_until_us_)) {
            // Bus idle resumes when the break ends.
            last_activity_us_ = wait_until_us_;
            begin_settling();
        }
        return;
    case State::RepeatGap:
        if (reached(now_us, repeat_at_us_)) {
            begin_settling();
        }
        return;
    case State::Transmitting:
        return;
    }
}

}
