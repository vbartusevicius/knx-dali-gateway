#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "dali/bus/Manchester.hpp"
#include "dali/bus/Timing.hpp"

// DALI-2 multi-master link layer (IEC 62386-101 §9): priority-based settling
// before every transmission, echo comparison for collision detection,
// collision break + bounded retry, and the backward-frame window for
// commands expecting a reply. Frames that are not our echo are delivered
// as unsolicited bus events (24-bit input-device events, other masters).
// All time is virtual microseconds; the owner drives it with poll(now).
namespace gateway::dali::bus {

using BusFrame = ReceivedFrame;

enum class Priority : std::uint8_t {
    P1 = 0,
    P2,
    P3,
    P4,
    P5,
};

enum class TxOutcome {
    Sent,            // forward frame transmitted, no reply expected
    Reply,           // a valid backward frame arrived inside the window
    NoReply,         // window closed without a reply
    ReplyCollision,  // framing/timing garbage inside the reply window:
                     // several devices answered at once
    Collision        // echo mismatch persisted through all retries
};

struct TxResult {
    TxOutcome outcome;
    std::uint8_t reply = 0;
};

class LinkLayer {
public:
    struct Sink {
        // Transmit the encoded symbol sequence.
        virtual void start_transmit(const EncodedFrame& frame) = 0;
        // Hold the bus active for `us` microseconds (collision break).
        virtual void drive_break(std::uint32_t us) = 0;

    protected:
        ~Sink() = default;
    };

    // `random` supplies the settling-point jitter inside the priority window.
    LinkLayer(Sink& sink, std::uint32_t (*random)(void*), void* random_context);

    // Queues a forward frame. `repeat_gap_us > 0` resends it once after that
    // spacing (IEC 62386-102 send-twice); replies are only expected on the
    // first transmission. Returns false when the queue is full.
    bool request(std::uint32_t value, std::uint8_t bits, Priority priority, bool expect_reply,
                 std::uint32_t repeat_gap_us = 0);

    // Decoder output. `frame_start_us` is the timestamp of the frame's first
    // half-bit — the backward window is measured on it.
    void on_decoded(const ReceivedFrame& frame, std::uint32_t frame_start_us);
    void on_decoder_error(std::uint32_t now_us);
    void on_tx_complete(std::uint32_t now_us);
    // Bus observed active (e.g. first RX edge after idle); optional input.
    void on_rx_active(std::uint32_t now_us);
    void poll(std::uint32_t now_us);

    std::optional<TxResult> take_result();
    std::optional<BusFrame> take_event();

private:
    static constexpr std::size_t queue_size = 8;
    static constexpr std::size_t result_size = 4;
    static constexpr std::size_t event_size = 8;
    // Grace period after TX complete for our own echo decode to arrive.
    static constexpr std::uint32_t echo_grace_us = 3000;

    enum class State {
        Idle,
        Transmitting,
        AwaitEcho,
        WaitReply,
        Breaking,
        RepeatGap,
    };

    struct Pending {
        std::uint32_t value = 0;
        std::uint32_t repeat_gap_us = 0;
        std::uint8_t bits = 0;
        std::uint8_t repeats_left = 0;
        std::uint8_t attempts = 0;
        Priority priority = Priority::P3;
        bool expect_reply = false;
    };

    Sink& sink_;
    std::uint32_t (*random_)(void*);
    void* random_context_;

    std::array<Pending, queue_size> queue_{};
    std::uint8_t queue_count_ = 0;

    std::array<TxResult, result_size> results_{};
    std::uint8_t result_count_ = 0;

    std::array<BusFrame, event_size> events_{};
    std::uint8_t event_count_ = 0;
    std::uint16_t events_dropped_ = 0;

    State state_ = State::Idle;
    Pending active_{};
    bool has_active_ = false;
    bool collision_seen_ = false;
    bool echo_ok_ = false;
    bool reply_resolved_ = false;

    std::uint32_t last_activity_us_ = 0;
    std::uint32_t settle_target_us_ = 0;
    bool settle_computed_ = false;
    std::uint32_t tx_end_us_ = 0;
    std::uint32_t wait_until_us_ = 0;
    std::uint32_t repeat_at_us_ = 0;

    void emit_result(TxOutcome outcome, std::uint8_t reply);
    void emit_event(const BusFrame& frame);
    void note_activity(std::uint32_t now_us);
    bool next_request();
    void begin_transmission(std::uint32_t now_us);
    void begin_settling();
    void complete_attempt(std::uint32_t now_us);
    void finish_request(TxOutcome outcome, std::uint8_t reply, std::uint32_t now_us);
    void enter_break(std::uint32_t now_us);
};

}
