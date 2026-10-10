// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2025 the knx-dali-gateway contributors
#include <unity.h>

#include <vector>

#include "dali/bus/LinkLayer.hpp"
#include "dali/bus/Manchester.hpp"
#include "dali/bus/Timing.hpp"

namespace {

using gateway::dali::bus::Decoded;
using gateway::dali::bus::encode_frame;
using gateway::dali::bus::EncodedFrame;
using gateway::dali::bus::ErrorKind;
using gateway::dali::bus::FrameError;
using gateway::dali::bus::Line;
using gateway::dali::bus::LinkLayer;
using gateway::dali::bus::ManchesterDecoder;
using gateway::dali::bus::Priority;
using gateway::dali::bus::ReceivedFrame;
using gateway::dali::bus::TxOutcome;
using namespace gateway::dali::bus::timing;

const ReceivedFrame* frame_of(const std::optional<Decoded>& result) {
    return result ? std::get_if<ReceivedFrame>(&*result) : nullptr;
}

const FrameError* error_of(const std::optional<Decoded>& result) {
    return result ? std::get_if<FrameError>(&*result) : nullptr;
}

std::optional<Decoded> decode_runs(
    std::initializer_list<std::pair<Line, std::uint32_t>> runs) {
    ManchesterDecoder decoder;
    std::optional<Decoded> out;
    for (const auto& run : runs) {
        if (const auto result = decoder.feed(run.first, run.second)) {
            out = result;
        }
    }
    if (!out) {
        out = decoder.finish();
    }
    return out;
}

// Re-encodes half-bit pairs for a raw bit cell sequence (1 = active->idle).
std::optional<Decoded> decode_cells(std::initializer_list<bool> cells,
                                  std::uint32_t half_us = te_us,
                                  std::uint32_t stop_us_ = stop_us) {
    ManchesterDecoder decoder;
    std::optional<Decoded> out;
    for (const bool cell : cells) {
        const Line first = cell ? Line::Active : Line::Idle;
        const Line second = cell ? Line::Idle : Line::Active;
        for (const Line level : {first, second}) {
            if (const auto result = decoder.feed(level, half_us)) {
                out = result;
            }
        }
    }
    if (const auto result = decoder.feed(Line::Idle, stop_us_)) {
        out = result;
    }
    return out;
}

// -- encoder ---------------------------------------------------------------

void test_encode_starts_with_start_bit() {
    const auto frame = encode_frame(0xFF00, 16);
    TEST_ASSERT_TRUE(frame.count > 0);
    TEST_ASSERT_EQUAL_UINT32(te_us, frame.symbols[0].us);
    TEST_ASSERT_EQUAL(Line::Active, frame.symbols[0].level);
    TEST_ASSERT_EQUAL(Line::Idle, frame.symbols[frame.count - 1].level);
    TEST_ASSERT_TRUE(frame.symbols[frame.count - 1].us >= stop_us);
}

void test_encode_8bit_all_ones_is_alternating() {
    const auto frame = encode_frame(0xFF, 8);
    TEST_ASSERT_EQUAL_UINT8(18, frame.count);
    TEST_ASSERT_EQUAL(Line::Active, frame.symbols[0].level);
    for (std::uint8_t i = 1; i + 1 < frame.count; ++i) {
        TEST_ASSERT_NOT_EQUAL(frame.symbols[i - 1].level, frame.symbols[i].level);
        TEST_ASSERT_EQUAL_UINT32(te_us, frame.symbols[i].us);
    }
    TEST_ASSERT_EQUAL_UINT32(te_us + stop_us, frame.symbols[17].us);
}

void test_encode_8bit_zero_merges_double_idle() {
    const auto frame = encode_frame(0x00, 8);
    TEST_ASSERT_EQUAL_UINT8(18, frame.count);
    TEST_ASSERT_EQUAL_UINT32(te_us, frame.symbols[0].us);
    TEST_ASSERT_EQUAL(Line::Idle, frame.symbols[1].level);
    TEST_ASSERT_EQUAL_UINT32(2 * te_us, frame.symbols[1].us);
    TEST_ASSERT_EQUAL_UINT32(stop_us, frame.symbols[17].us);
    TEST_ASSERT_EQUAL(Line::Idle, frame.symbols[17].level);
}

void test_encode_rejects_bad_lengths() {
    TEST_ASSERT_EQUAL_UINT8(0, encode_frame(0, 0).count);
    TEST_ASSERT_EQUAL_UINT8(0, encode_frame(0, 7).count);
    TEST_ASSERT_EQUAL_UINT8(0, encode_frame(0, 32).count);
}

// -- decoder: round trips ---------------------------------------------------

std::optional<Decoded> round_trip(std::uint32_t value, std::uint8_t bits) {
    const auto frame = encode_frame(value, bits);
    ManchesterDecoder decoder;
    std::optional<Decoded> out;
    for (std::uint8_t i = 0; i < frame.count; ++i) {
        if (const auto result = decoder.feed(frame.symbols[i].level,
                                             frame.symbols[i].us)) {
            out = result;
        }
    }
    if (!out) {
        out = decoder.finish();
    }
    return out;
}

void test_round_trip_frames() {
    const std::pair<std::uint32_t, std::uint8_t> vectors[] = {
        {0x00, 8},   {0xFF, 8},     {0xA5, 8},    {0x0000, 16},
        {0xFF00, 16}, {0xA3FF, 16}, {0xFFFF, 16}, {0x000000, 24},
        {0xFFFE1D, 24}, {0xABCDEF, 24}, {0x123456, 24},
    };
    for (const auto& vector : vectors) {
        const auto decoded = round_trip(vector.first, vector.second);
        const auto* decoded_frame = frame_of(decoded);
        TEST_ASSERT_NOT_NULL(decoded_frame);
        TEST_ASSERT_EQUAL_UINT32(vector.first, decoded_frame->value);
        TEST_ASSERT_EQUAL_UINT8(vector.second, decoded_frame->bits);
    }
}

// -- decoder: tolerance and errors ------------------------------------------

void test_decoder_accepts_tolerance_edges() {
    // Start + 8 payload ones at the half-bit extremes.
    for (const std::uint32_t half : {rx_half_min_us, rx_half_max_us}) {
        const auto result = decode_cells(
            {true, true, true, true, true, true, true, true, true}, half);
        const auto* frame = frame_of(result);
        TEST_ASSERT_NOT_NULL(frame);
        TEST_ASSERT_EQUAL_UINT8(8, frame->bits);
        TEST_ASSERT_EQUAL_UINT32(0xFF, frame->value);
    }
    // A '1'->'0' boundary merges two Idle half-bits into one full-bit run;
    // check both ends of the full-bit window. Frame: start bit + 0x00.
    for (const std::uint32_t span : {rx_full_min_us, rx_full_max_us}) {
        const auto merged = decode_runs({
            {Line::Active, te_us}, {Line::Idle, span},
            {Line::Active, te_us}, {Line::Idle, te_us},
            {Line::Active, te_us}, {Line::Idle, te_us},
            {Line::Active, te_us}, {Line::Idle, te_us},
            {Line::Active, te_us}, {Line::Idle, te_us},
            {Line::Active, te_us}, {Line::Idle, te_us},
            {Line::Active, te_us}, {Line::Idle, te_us},
            {Line::Active, te_us}, {Line::Idle, te_us},
            {Line::Active, te_us},
            {Line::Idle, stop_us},
        });
        const auto* frame = frame_of(merged);
        TEST_ASSERT_NOT_NULL(frame);
        TEST_ASSERT_EQUAL_UINT8(8, frame->bits);
        TEST_ASSERT_EQUAL_UINT32(0x00, frame->value);
    }
    // '0'->'1' and '1'->'0' boundaries also merge Active and Idle runs.
    const auto active_merge = decode_runs({
        {Line::Active, te_us},
        {Line::Idle, rx_full_min_us},
        {Line::Active, rx_full_max_us},
        {Line::Idle, rx_full_min_us},
        {Line::Active, te_us}, {Line::Idle, te_us},
        {Line::Active, te_us}, {Line::Idle, te_us},
        {Line::Active, te_us}, {Line::Idle, te_us},
        {Line::Active, te_us}, {Line::Idle, te_us},
        {Line::Active, te_us}, {Line::Idle, te_us},
        {Line::Active, te_us},
        {Line::Idle, stop_us},
    });
    const auto* merged_frame = frame_of(active_merge);
    TEST_ASSERT_NOT_NULL(merged_frame);
    TEST_ASSERT_EQUAL_UINT8(8, merged_frame->bits);
    TEST_ASSERT_EQUAL_UINT32(0x40, merged_frame->value);
}

void test_decoder_rejects_out_of_window_durations() {
    for (const std::uint32_t bad : {rx_half_min_us - 1, rx_half_max_us + 1,
                                    rx_full_min_us - 1}) {
        ManchesterDecoder decoder;
        const auto result = decoder.feed(Line::Active, bad);
        TEST_ASSERT_TRUE(result.has_value());
        TEST_ASSERT_EQUAL(ErrorKind::Timing, error_of(result)->kind);
    }
    // Over-long active run is a timing violation, not a stop.
    ManchesterDecoder decoder;
    const auto result = decoder.feed(Line::Active, rx_full_max_us + 1);
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_EQUAL(ErrorKind::Timing, error_of(result)->kind);
}

void test_decoder_rejects_wrong_bit_count() {
    // Start + 9 payload bits is not a legal frame length.
    const auto result = decode_cells(
        {true, false, false, false, false, false, false, false, false, false});
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_EQUAL(ErrorKind::Length, error_of(result)->kind);
}

void test_decoder_rejects_bad_start_bit() {
    const auto result = decode_cells(
        {false, true, true, true, true, true, true, true, true});
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_EQUAL(ErrorKind::Framing, error_of(result)->kind);
}

void test_decoder_rejects_manchester_violation() {
    const auto result = decode_runs({
        {Line::Active, te_us}, {Line::Active, te_us},
        {Line::Idle, te_us}, {Line::Active, te_us},
        {Line::Idle, stop_us},
    });
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_EQUAL(ErrorKind::Framing, error_of(result)->kind);
}

void test_decoder_finishes_without_explicit_stop_symbol() {
    // A receiver that ends the burst at the last edge: feed only the frame.
    ManchesterDecoder decoder;
    for (const bool cell : {true, true, true, true, true, true, true, true, true}) {
        decoder.feed(cell ? Line::Active : Line::Idle, te_us);
        decoder.feed(cell ? Line::Idle : Line::Active, te_us);
    }
    const auto result = decoder.finish();
    const auto* frame = frame_of(result);
    TEST_ASSERT_NOT_NULL(frame);
    TEST_ASSERT_EQUAL_UINT8(8, frame->bits);
    TEST_ASSERT_EQUAL_UINT32(0xFF, frame->value);
}

void test_decoder_recovers_after_garbage() {
    ManchesterDecoder decoder;
    const auto garbage = decoder.feed(Line::Active, 100);
    TEST_ASSERT_TRUE(garbage.has_value());
    TEST_ASSERT_EQUAL(ErrorKind::Timing, error_of(garbage)->kind);
    // The garbage burst ends (bus idle), then a clean frame decodes.
    decoder.feed(Line::Idle, stop_us);
    const auto frame = encode_frame(0x42, 8);
    std::optional<Decoded> out;
    for (std::uint8_t i = 0; i < frame.count; ++i) {
        if (const auto r = decoder.feed(frame.symbols[i].level, frame.symbols[i].us)) {
            out = r;
        }
    }
    const auto* decoded_frame = frame_of(out);
    TEST_ASSERT_NOT_NULL(decoded_frame);
    TEST_ASSERT_EQUAL_UINT32(0x42, decoded_frame->value);
}

// -- link layer --------------------------------------------------------------

struct FakeSink final : LinkLayer::Sink {
    std::vector<EncodedFrame> transmits;
    std::vector<std::uint32_t> breaks;

    void start_transmit(const EncodedFrame& frame) override {
        transmits.push_back(frame);
    }
    void drive_break(std::uint32_t us) override {
        breaks.push_back(us);
    }
};

std::uint32_t fixed_random(void* ctx) {
    return *static_cast<std::uint32_t*>(ctx);
}

struct Fixture {
    std::uint32_t jitter = 0;
    FakeSink sink;
    LinkLayer link{sink, &fixed_random, &jitter};
};

std::optional<Decoded> round_trip_symbols(const EncodedFrame& encoded) {
    ManchesterDecoder decoder;
    std::optional<Decoded> out;
    for (std::uint8_t i = 0; i < encoded.count; ++i) {
        if (const auto r = decoder.feed(encoded.symbols[i].level,
                                        encoded.symbols[i].us)) {
            out = r;
        }
    }
    if (!out) {
        out = decoder.finish();
    }
    return out;
}

void test_settling_per_priority() {
    for (std::uint8_t p = 0; p < 5; ++p) {
        Fixture f;
        f.link.request(0x1000 + p, 16, static_cast<Priority>(p), false);
        f.link.poll(0);
        TEST_ASSERT_TRUE(f.sink.transmits.empty());
        f.link.poll(settling_min_us[p] - 1);
        TEST_ASSERT_TRUE(f.sink.transmits.empty());
        f.link.poll(settling_min_us[p]);
        TEST_ASSERT_EQUAL_size_t(1, f.sink.transmits.size());
    }
}

void test_activity_restarts_settling() {
    Fixture f;
    f.link.request(0x55AA, 16, Priority::P2, false);
    f.link.poll(0);
    // Foreign frame at t=10 ms restarts the idle timer.
    f.link.on_decoded(ReceivedFrame{0xAA, 8}, 10000);
    f.link.poll(10000 + settling_min_us[1] - 1);
    TEST_ASSERT_TRUE(f.sink.transmits.empty());
    f.link.poll(10000 + settling_min_us[1]);
    TEST_ASSERT_EQUAL_size_t(1, f.sink.transmits.size());
}

void test_jittered_settling_uses_random_point() {
    Fixture f;
    f.jitter = 600;  // inside the priority-1 span of 1201 us
    f.link.request(0x55AA, 16, Priority::P1, false);
    f.link.poll(settling_min_us[0] + 599);
    TEST_ASSERT_TRUE(f.sink.transmits.empty());
    f.link.poll(settling_min_us[0] + 600);
    TEST_ASSERT_EQUAL_size_t(1, f.sink.transmits.size());
}

void test_clean_send_returns_sent() {
    Fixture f;
    f.link.request(0x55AA, 16, Priority::P1, false);
    const std::uint32_t tx_start = settling_min_us[0];
    f.link.poll(tx_start);
    f.link.on_decoded(ReceivedFrame{0x55AA, 16}, tx_start);
    f.link.on_tx_complete(tx_start + 17 * 2 * te_us);
    const auto result = f.link.take_result();
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_EQUAL(TxOutcome::Sent, result->outcome);
}

void test_echo_mismatch_retries_then_collides() {
    Fixture f;
    f.link.request(0x55AA, 16, Priority::P1, false);
    std::uint32_t now = settling_min_us[0];
    for (std::uint8_t attempt = 0; attempt < collision_attempts_max; ++attempt) {
        f.link.poll(now);
        TEST_ASSERT_EQUAL_size_t(attempt + 1, f.sink.transmits.size());
        f.link.on_decoded(ReceivedFrame{0xDEAD, 16}, now);  // not our echo
        now += 17 * 2 * te_us;
        f.link.on_tx_complete(now);
        if (attempt + 1 < collision_attempts_max) {
            TEST_ASSERT_EQUAL_size_t(attempt + 1, f.sink.breaks.size());
            now += collision_break_us;
            f.link.poll(now);            // leave Breaking state
            now += settling_min_us[0];   // re-settle
        }
    }
    const auto result = f.link.take_result();
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_EQUAL(TxOutcome::Collision, result->outcome);
    TEST_ASSERT_EQUAL_size_t(collision_attempts_max, f.sink.transmits.size());
    TEST_ASSERT_EQUAL_size_t(collision_attempts_max - 1, f.sink.breaks.size());
}

void test_decoder_garbage_during_tx_is_collision() {
    Fixture f;
    f.link.request(0x55AA, 16, Priority::P1, false);
    f.link.poll(settling_min_us[0]);
    f.link.on_decoder_error(settling_min_us[0] + 100);
    f.link.on_tx_complete(settling_min_us[0] + 17 * 2 * te_us);
    TEST_ASSERT_EQUAL_size_t(1, f.sink.breaks.size());
}

void test_missing_echo_is_collision() {
    Fixture f;
    f.link.request(0x55AA, 16, Priority::P1, false);
    f.link.poll(settling_min_us[0]);
    const std::uint32_t tx_end = settling_min_us[0] + 17 * 2 * te_us;
    f.link.on_tx_complete(tx_end);
    f.link.poll(tx_end + 4000);  // past the 3 ms echo grace
    TEST_ASSERT_EQUAL_size_t(1, f.sink.breaks.size());
}

void drive_query(Fixture& f, std::uint32_t frame = 0x03A0,
                 std::uint32_t tx_start = settling_min_us[1]) {
    f.link.request(frame, 16, Priority::P2, true);
    f.link.poll(tx_start);
    f.link.on_decoded(ReceivedFrame{frame, 16}, tx_start);
    f.link.on_tx_complete(tx_start + 17 * 2 * te_us);
}

void test_reply_inside_window_is_accepted() {
    for (const std::uint32_t offset : {5600u, 10400u}) {
        Fixture f;
        drive_query(f);
        const std::uint32_t tx_end = settling_min_us[1] + 17 * 2 * te_us;
        f.link.on_decoded(ReceivedFrame{0xAB, 8}, tx_end + offset);
        const auto result = f.link.take_result();
        TEST_ASSERT_TRUE(result.has_value());
        TEST_ASSERT_EQUAL(TxOutcome::Reply, result->outcome);
        TEST_ASSERT_EQUAL_UINT8(0xAB, result->reply);
    }
}

void test_reply_outside_window_is_not_an_answer() {
    for (const std::uint32_t offset : {5400u, 10600u}) {
        Fixture f;
        drive_query(f);
        const std::uint32_t tx_end = settling_min_us[1] + 17 * 2 * te_us;
        f.link.on_decoded(ReceivedFrame{0xAB, 8}, tx_end + offset);
        const auto event = f.link.take_event();
        TEST_ASSERT_TRUE(event.has_value());
        TEST_ASSERT_EQUAL_UINT32(0xAB, event->value);
        f.link.poll(tx_end + backward_max_us + 1);
        const auto result = f.link.take_result();
        TEST_ASSERT_TRUE(result.has_value());
        TEST_ASSERT_EQUAL(TxOutcome::NoReply, result->outcome);
    }
}

void test_reply_window_timeout_gives_no_reply() {
    Fixture f;
    drive_query(f);
    const std::uint32_t tx_end = settling_min_us[1] + 17 * 2 * te_us;
    f.link.poll(tx_end + backward_max_us + 1);
    const auto result = f.link.take_result();
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_EQUAL(TxOutcome::NoReply, result->outcome);
}

void test_garbage_in_reply_window_is_reply_collision() {
    Fixture f;
    drive_query(f);
    const std::uint32_t tx_end = settling_min_us[1] + 17 * 2 * te_us;
    f.link.on_decoder_error(tx_end + 7000);
    const auto result = f.link.take_result();
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_EQUAL(TxOutcome::ReplyCollision, result->outcome);
}

void test_unsolicited_frames_delivered_while_idle_and_between_transactions() {
    Fixture f;
    f.link.on_decoded(ReceivedFrame{0xAABBCC, 24}, 5000);
    const auto event = f.link.take_event();
    TEST_ASSERT_TRUE(event.has_value());
    TEST_ASSERT_EQUAL_UINT32(0xAABBCC, event->value);
    TEST_ASSERT_EQUAL_UINT8(24, event->bits);
    TEST_ASSERT_FALSE(f.link.take_event().has_value());

    // Between transactions: a forward frame from another master while a
    // reply window is open is an event, not an answer. The earlier foreign
    // frame pushed bus activity to 5 ms, so settling restarts from there.
    const std::uint32_t tx_start = 5000 + settling_min_us[1];
    drive_query(f, 0x03A0, tx_start);
    const std::uint32_t tx_end = tx_start + 17 * 2 * te_us;
    f.link.on_decoded(ReceivedFrame{0xFF00, 16}, tx_end + 7000);
    const auto between = f.link.take_event();
    TEST_ASSERT_TRUE(between.has_value());
    TEST_ASSERT_EQUAL_UINT32(0xFF00, between->value);
}

void test_queue_prefers_lower_priority_number() {
    Fixture f;
    f.link.request(0x3333, 16, Priority::P3, false);
    f.link.request(0x1111, 16, Priority::P1, false);
    f.link.request(0x2222, 16, Priority::P2, false);
    f.link.poll(settling_min_us[0]);
    TEST_ASSERT_EQUAL_size_t(1, f.sink.transmits.size());
    // Decode the transmitted waveform back to its value.
    const auto decoded = round_trip_symbols(f.sink.transmits[0]);
    const auto* decoded_frame = frame_of(decoded);
    TEST_ASSERT_EQUAL_UINT32(0x1111, decoded_frame->value);
}

void test_send_twice_repeats_after_gap() {
    Fixture f;
    f.link.request(0x55AA, 16, Priority::P2, false, send_twice_gap_us);
    std::uint32_t now = settling_min_us[1];
    f.link.poll(now);
    TEST_ASSERT_EQUAL_size_t(1, f.sink.transmits.size());
    f.link.on_decoded(ReceivedFrame{0x55AA, 16}, now);
    now += 17 * 2 * te_us;
    f.link.on_tx_complete(now);
    TEST_ASSERT_FALSE(f.link.take_result().has_value());
    now += send_twice_gap_us;
    f.link.poll(now);        // enters settling
    f.link.poll(now + 100);  // idle long enough already
    TEST_ASSERT_EQUAL_size_t(2, f.sink.transmits.size());
    f.link.on_decoded(ReceivedFrame{0x55AA, 16}, now);
    f.link.on_tx_complete(now + 17 * 2 * te_us);
    const auto result = f.link.take_result();
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_EQUAL(TxOutcome::Sent, result->outcome);
}

void test_device_frame_24bit_encodes() {
    Fixture f;
    f.link.request(0xFFFE1D, 24, Priority::P2, false);
    f.link.poll(settling_min_us[1]);
    TEST_ASSERT_EQUAL_size_t(1, f.sink.transmits.size());
    const auto decoded = round_trip_symbols(f.sink.transmits[0]);
    const auto* decoded_frame = frame_of(decoded);
    TEST_ASSERT_NOT_NULL(decoded_frame);
    TEST_ASSERT_EQUAL_UINT32(0xFFFE1D, decoded_frame->value);
    TEST_ASSERT_EQUAL_UINT8(24, decoded_frame->bits);
}

}

void run_bus_tests() {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_encode_starts_with_start_bit);
    RUN_TEST(test_encode_8bit_all_ones_is_alternating);
    RUN_TEST(test_encode_8bit_zero_merges_double_idle);
    RUN_TEST(test_encode_rejects_bad_lengths);
    RUN_TEST(test_round_trip_frames);
    RUN_TEST(test_decoder_accepts_tolerance_edges);
    RUN_TEST(test_decoder_rejects_out_of_window_durations);
    RUN_TEST(test_decoder_rejects_wrong_bit_count);
    RUN_TEST(test_decoder_rejects_bad_start_bit);
    RUN_TEST(test_decoder_rejects_manchester_violation);
    RUN_TEST(test_decoder_finishes_without_explicit_stop_symbol);
    RUN_TEST(test_decoder_recovers_after_garbage);
    RUN_TEST(test_settling_per_priority);
    RUN_TEST(test_activity_restarts_settling);
    RUN_TEST(test_jittered_settling_uses_random_point);
    RUN_TEST(test_clean_send_returns_sent);
    RUN_TEST(test_echo_mismatch_retries_then_collides);
    RUN_TEST(test_decoder_garbage_during_tx_is_collision);
    RUN_TEST(test_missing_echo_is_collision);
    RUN_TEST(test_reply_inside_window_is_accepted);
    RUN_TEST(test_reply_outside_window_is_not_an_answer);
    RUN_TEST(test_reply_window_timeout_gives_no_reply);
    RUN_TEST(test_garbage_in_reply_window_is_reply_collision);
    RUN_TEST(test_unsolicited_frames_delivered_while_idle_and_between_transactions);
    RUN_TEST(test_queue_prefers_lower_priority_number);
    RUN_TEST(test_send_twice_repeats_after_gap);
    RUN_TEST(test_device_frame_24bit_encodes);
}
