// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2025 the knx-dali-gateway contributors
#include "espidf/RmtDaliBus.hpp"

#include <cstring>
#include <new>

#include <driver/gpio.h>
#include <driver/rmt_encoder.h>
#include <driver/rmt_rx.h>
#include <driver/rmt_tx.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "dali/bus/Manchester.hpp"
#include "dali/bus/Timing.hpp"

namespace gateway::espidf {
namespace {

constexpr const char* log_tag = "dali-bus";
// 1 MHz RMT clock: one tick is one microsecond, matching the link layer's
// virtual time base exactly.
constexpr std::uint32_t resolution_hz = 1000000;
// An idle level longer than this ends a receive burst; it must exceed the
// 2.4 ms stop condition so every frame arrives as one burst.
constexpr std::uint32_t rx_idle_threshold_ns = 3000000;
constexpr std::uint32_t rx_glitch_filter_ns = 2000;
// 64 words cover a worst-case 25-cell frame plus garbage.
constexpr std::size_t rx_buffer_symbols = 64;
// RX bursts, TX completions, requests and one carrier-sense edge per
// half-bit edge all land in the same queue; 16 keeps a full frame's edges.
constexpr std::size_t message_queue_len = 16;
constexpr std::size_t event_queue_len = 8;
// Settling + retries + one send-twice gap stay under 150 ms; the timeout
// only guards against a wedged link layer.
constexpr std::uint32_t transact_timeout_us = 500000;
constexpr std::uint32_t task_poll_ms = 2;
constexpr std::uint32_t task_priority = 10;
constexpr std::uint32_t task_stack = 4096;

}  // namespace

struct RmtDaliBus::Impl final : dali::bus::LinkLayer::Sink {
    struct RequestMsg {
        dali::BusRequest request;
        TaskHandle_t waiter;
        std::uint32_t seq;
    };
    struct RxDoneMsg {
        std::uint32_t isr_us;
        std::uint16_t count;
    };
    struct Message {
        enum class Kind : std::uint8_t { RxDone, RxActive, TxDone, Request };
        Kind kind;
        union {
            RxDoneMsg rx;
            std::uint32_t rx_active_us;
            std::uint32_t tx_end_us;
            RequestMsg request;
        };
    };

    explicit Impl(const Config& config)
        : config_(config),
          link_(static_cast<dali::bus::LinkLayer::Sink&>(*this), &Impl::random,
                this) {}

    static std::uint32_t random(void* /*ctx*/) { return esp_random(); }

    static std::uint32_t now_us() {
        return static_cast<std::uint32_t>(esp_timer_get_time());
    }

    std::uint16_t pin_level(dali::bus::Line line) const {
        return line == dali::bus::Line::Active
                   ? (config_.invert_tx ? 1u : 0u)
                   : (config_.invert_tx ? 0u : 1u);
    }

    bool start() {
        call_mutex_ = xSemaphoreCreateMutex();
        messages_ = xQueueCreate(message_queue_len, sizeof(Message));
        events_ = xQueueCreate(event_queue_len, sizeof(dali::BusEvent));
        if (call_mutex_ == nullptr || messages_ == nullptr || events_ == nullptr) {
            return false;
        }

        const rmt_rx_channel_config_t rx_cfg = {
            .gpio_num = static_cast<gpio_num_t>(config_.rx_gpio),
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = resolution_hz,
            .mem_block_symbols = rx_buffer_symbols,
            .intr_priority = 0,
            .flags = {.invert_in = config_.invert_rx ? 1u : 0u,
                      .with_dma = 0u,
                      .io_loop_back = 0u,
                      .allow_pd = 0u},
        };
        if (rmt_new_rx_channel(&rx_cfg, &rx_channel_) != ESP_OK) {
            return false;
        }
        const rmt_rx_event_callbacks_t rx_cbs = {.on_recv_done = &Impl::on_rx_done};
        if (rmt_rx_register_event_callbacks(rx_channel_, &rx_cbs, this) != ESP_OK) {
            return false;
        }

        const rmt_tx_channel_config_t tx_cfg = {
            .gpio_num = static_cast<gpio_num_t>(config_.tx_gpio),
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = resolution_hz,
            .mem_block_symbols = rx_buffer_symbols,
            .trans_queue_depth = 2,
            .intr_priority = 0,
            .flags = {.invert_out = 0u,
                      .with_dma = 0u,
                      .io_loop_back = 0u,
                      .io_od_mode = 0u,
                      .allow_pd = 0u},
        };
        if (rmt_new_tx_channel(&tx_cfg, &tx_channel_) != ESP_OK) {
            return false;
        }
        const rmt_tx_event_callbacks_t tx_cbs = {.on_trans_done = &Impl::on_tx_done};
        if (rmt_tx_register_event_callbacks(tx_channel_, &tx_cbs, this) != ESP_OK) {
            return false;
        }
        const rmt_copy_encoder_config_t encoder_cfg = {};
        if (rmt_new_copy_encoder(&encoder_cfg, &encoder_) != ESP_OK) {
            return false;
        }
        if (rmt_enable(rx_channel_) != ESP_OK || rmt_enable(tx_channel_) != ESP_OK) {
            return false;
        }
        // Carrier sense: the RMT only reports a finished burst, so a GPIO
        // edge interrupt tells the link layer the moment the bus leaves
        // idle and it must not start transmitting into someone else's
        // frame. The pad level is the bus level XOR the board's inversion.
        const gpio_config_t rx_sense = {
            .pin_bit_mask = 1ULL << config_.rx_gpio,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = config_.invert_rx ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE,
        };
        gpio_config(&rx_sense);
        const auto isr_status = gpio_install_isr_service(0);
        if (isr_status == ESP_OK || isr_status == ESP_ERR_INVALID_STATE) {
            gpio_isr_handler_add(static_cast<gpio_num_t>(config_.rx_gpio),
                                 &Impl::on_rx_edge, this);
        }
        if (xTaskCreate(&Impl::task_entry, "dali-bus", task_stack, this,
                        task_priority, &task_) != pdPASS) {
            return false;
        }
        arm_rx();
        return true;
    }

    void arm_rx() {
        const rmt_receive_config_t receive_cfg = {
            .signal_range_min_ns = rx_glitch_filter_ns,
            .signal_range_max_ns = rx_idle_threshold_ns,
            .flags = {.en_partial_rx = 0u},
        };
        const auto error =
            rmt_receive(rx_channel_, rx_buffer_, sizeof(rx_buffer_), &receive_cfg);
        if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(log_tag, "rmt_receive failed: %s", esp_err_to_name(error));
        }
    }

    bool IRAM_ATTR on_rx_done_isr(const rmt_rx_done_event_data_t* edata) {
        Message msg{};
        msg.kind = Message::Kind::RxDone;
        msg.rx.isr_us = now_us();
        msg.rx.count = static_cast<std::uint16_t>(edata->num_symbols);
        BaseType_t woken = pdFALSE;
        xQueueSendFromISR(messages_, &msg, &woken);
        return woken == pdTRUE;
    }

    bool IRAM_ATTR on_tx_done_isr() {
        Message msg{};
        msg.kind = Message::Kind::TxDone;
        msg.tx_end_us = now_us();
        BaseType_t woken = pdFALSE;
        xQueueSendFromISR(messages_, &msg, &woken);
        return woken == pdTRUE;
    }

    void IRAM_ATTR on_rx_edge_isr() {
        Message msg{};
        msg.kind = Message::Kind::RxActive;
        msg.rx_active_us = now_us();
        BaseType_t woken = pdFALSE;
        // A full queue just means the bus is still busy; the edges already
        // queued carry the same information.
        xQueueSendFromISR(messages_, &msg, &woken);
        if (woken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    }

    void handle_rx(const RxDoneMsg& done) {
        // Re-arm before decoding so the deaf window stays minimal.
        arm_rx();
        std::uint32_t total_us = 0;
        std::optional<dali::bus::Decoded> decoded;
        for (std::uint16_t i = 0; i < done.count && i < rx_buffer_symbols; ++i) {
            const rmt_symbol_word_t& word = rx_buffer_[i];
            total_us += word.duration0 + word.duration1;
            const auto first = decoder_.feed(
                word.level0 == 0 ? dali::bus::Line::Active : dali::bus::Line::Idle,
                word.duration0);
            if (first) {
                decoded = first;
            }
            const auto second = decoder_.feed(
                word.level1 == 0 ? dali::bus::Line::Active : dali::bus::Line::Idle,
                word.duration1);
            if (second) {
                decoded = second;
            }
        }
        if (!decoded) {
            // The receive ended without an in-band idle tail (the RMT may
            // not report the truncated stop run).
            decoded = decoder_.finish();
        }
        if (!decoded) {
            return;
        }
        if (const auto* frame = std::get_if<dali::bus::ReceivedFrame>(&*decoded)) {
            link_.on_decoded(*frame, done.isr_us - total_us);
        } else {
            link_.on_decoder_error(done.isr_us);
        }
    }

    void respond(const dali::bus::TxResult& result) {
        if (waiter_ == nullptr) {
            return;
        }
        pending_result_ = result;
        result_seq_ = waiter_seq_;
        xTaskNotifyGive(waiter_);
        waiter_ = nullptr;
    }

    void dispatch(const Message& msg) {
        switch (msg.kind) {
        case Message::Kind::RxDone:
            handle_rx(msg.rx);
            return;
        case Message::Kind::RxActive:
            link_.on_rx_active(msg.rx_active_us);
            return;
        case Message::Kind::TxDone:
            link_.on_tx_complete(msg.tx_end_us);
            return;
        case Message::Kind::Request:
            if (!link_.request(msg.request.request.value,
                               msg.request.request.bits,
                               msg.request.request.priority,
                               msg.request.request.expect_reply,
                               msg.request.request.repeat_gap_us)) {
                waiter_seq_ = msg.request.seq;
                waiter_ = msg.request.waiter;
                respond(dali::bus::TxResult{dali::bus::TxOutcome::Collision, 0});
                return;
            }
            waiter_ = msg.request.waiter;
            waiter_seq_ = msg.request.seq;
            return;
        }
    }

    void run() {
        Message msg{};
        for (;;) {
            if (xQueueReceive(messages_, &msg, pdMS_TO_TICKS(task_poll_ms)) == pdTRUE) {
                do {
                    dispatch(msg);
                } while (xQueueReceive(messages_, &msg, 0) == pdTRUE);
            }
            link_.poll(now_us());
            while (const auto result = link_.take_result()) {
                respond(*result);
            }
            while (const auto event = link_.take_event()) {
                const dali::BusEvent bus_event{event->value, event->bits};
                // Bounded queue: when the worker falls behind, drop.
                xQueueSend(events_, &bus_event, 0);
            }
        }
    }

    static void task_entry(void* arg) {
        static_cast<Impl*>(arg)->run();
    }

    static bool IRAM_ATTR on_rx_done(rmt_channel_handle_t,
                                     const rmt_rx_done_event_data_t* edata,
                                     void* ctx) {
        return static_cast<Impl*>(ctx)->on_rx_done_isr(edata);
    }

    static bool IRAM_ATTR on_tx_done(rmt_channel_handle_t,
                                     const rmt_tx_done_event_data_t*,
                                     void* ctx) {
        return static_cast<Impl*>(ctx)->on_tx_done_isr();
    }

    static void IRAM_ATTR on_rx_edge(void* ctx) {
        static_cast<Impl*>(ctx)->on_rx_edge_isr();
    }

    // LinkLayer::Sink ------------------------------------------------------

    void start_transmit(const dali::bus::EncodedFrame& frame) override {
        std::size_t count = 0;
        std::uint8_t i = 0;
        while (i + 1 < frame.count && count < tx_items_capacity) {
            tx_items_[count++] = {
                .duration0 = static_cast<std::uint16_t>(frame.symbols[i].us),
                .level0 = pin_level(frame.symbols[i].level),
                .duration1 = static_cast<std::uint16_t>(frame.symbols[i + 1].us),
                .level1 = pin_level(frame.symbols[i + 1].level),
            };
            i += 2;
        }
        if (i < frame.count && count < tx_items_capacity) {
            // Odd tail: pad with a tiny released-level half so the last
            // symbol is a full word.
            tx_items_[count++] = {
                .duration0 = static_cast<std::uint16_t>(frame.symbols[i].us),
                .level0 = pin_level(frame.symbols[i].level),
                .duration1 = 1,
                .level1 = pin_level(dali::bus::Line::Idle),
            };
        }
        const rmt_transmit_config_t tx_cfg = {
            .loop_count = 0,
            .flags = {.eot_level = pin_level(dali::bus::Line::Idle),
                      .queue_nonblocking = 0u},
        };
        const auto error =
            rmt_transmit(tx_channel_, encoder_, tx_items_,
                         count * sizeof(rmt_symbol_word_t), &tx_cfg);
        if (error != ESP_OK) {
            ESP_LOGW(log_tag, "rmt_transmit failed: %s", esp_err_to_name(error));
        }
    }

    void drive_break(std::uint32_t us) override {
        // Hold the bus driven so every receiver sees a timing violation.
        break_symbol_ = {
            .duration0 = static_cast<std::uint16_t>(us),
            .level0 = pin_level(dali::bus::Line::Active),
            .duration1 = 10,
            .level1 = pin_level(dali::bus::Line::Idle),
        };
        const rmt_transmit_config_t tx_cfg = {
            .loop_count = 0,
            .flags = {.eot_level = pin_level(dali::bus::Line::Idle),
                      .queue_nonblocking = 0u},
        };
        rmt_transmit(tx_channel_, encoder_, &break_symbol_,
                     sizeof(break_symbol_), &tx_cfg);
    }

    // Public surface used by RmtDaliBus ------------------------------------

    dali::bus::TxResult transact(const dali::BusRequest& request) {
        xSemaphoreTake(call_mutex_, portMAX_DELAY);
        const std::uint32_t seq = ++request_seq_;
        Message msg{};
        msg.kind = Message::Kind::Request;
        msg.request = {request, xTaskGetCurrentTaskHandle(), seq};
        if (xQueueSend(messages_, &msg, pdMS_TO_TICKS(20)) != pdTRUE) {
            xSemaphoreGive(call_mutex_);
            return {dali::bus::TxOutcome::Collision, 0};
        }
        dali::bus::TxResult result{dali::bus::TxOutcome::Collision, 0};
        const std::uint32_t deadline = now_us() + transact_timeout_us;
        while (static_cast<std::int32_t>(deadline - now_us()) > 0) {
            xTaskNotifyWait(0, 0, nullptr, pdMS_TO_TICKS(20));
            if (result_seq_ == seq) {
                result = pending_result_;
                break;
            }
        }
        xSemaphoreGive(call_mutex_);
        return result;
    }

    Config config_;
    dali::bus::LinkLayer link_;
    dali::bus::ManchesterDecoder decoder_;

    rmt_channel_handle_t rx_channel_ = nullptr;
    rmt_channel_handle_t tx_channel_ = nullptr;
    rmt_encoder_handle_t encoder_ = nullptr;
    TaskHandle_t task_ = nullptr;
    QueueHandle_t messages_ = nullptr;
    QueueHandle_t events_ = nullptr;
    SemaphoreHandle_t call_mutex_ = nullptr;

    // Single receive buffer: re-armed only after the task consumed the
    // burst, so the ISR never sees it being overwritten.
    rmt_symbol_word_t rx_buffer_[rx_buffer_symbols] = {};

    static constexpr std::size_t tx_items_capacity =
        (dali::bus::EncodedFrame::max_symbols + 1) / 2;
    rmt_symbol_word_t tx_items_[tx_items_capacity] = {};
    rmt_symbol_word_t break_symbol_ = {};

    TaskHandle_t waiter_ = nullptr;
    std::uint32_t waiter_seq_ = 0;
    std::uint32_t request_seq_ = 0;
    // Written by the bus task before xTaskNotifyGive, read by the waiter
    // after xTaskNotifyWait; the notification provides the memory ordering.
    std::uint32_t result_seq_ = 0;
    dali::bus::TxResult pending_result_{dali::bus::TxOutcome::Collision, 0};
};

RmtDaliBus::RmtDaliBus(const Config& config) noexcept : impl_(nullptr), config_(config) {}

RmtDaliBus::~RmtDaliBus() {
    delete impl_;
}

bool RmtDaliBus::start() noexcept {
    impl_ = new (std::nothrow) Impl(config_);
    if (impl_ == nullptr) {
        ESP_LOGE(log_tag, "no memory for the DALI bus driver");
        return false;
    }
    if (!impl_->start()) {
        ESP_LOGE(log_tag, "RMT DALI bus initialization failed");
        delete impl_;
        impl_ = nullptr;
        return false;
    }
    return true;
}

bool RmtDaliBus::configured() const noexcept {
    return impl_ != nullptr;
}

dali::bus::TxResult RmtDaliBus::transact(const dali::BusRequest& request) {
    if (impl_ == nullptr) {
        return {dali::bus::TxOutcome::Collision, 0};
    }
    return impl_->transact(request);
}

std::optional<dali::BusEvent> RmtDaliBus::poll_event() {
    if (impl_ == nullptr) {
        return std::nullopt;
    }
    dali::BusEvent event{};
    if (xQueueReceive(impl_->events_, &event, 0) != pdTRUE) {
        return std::nullopt;
    }
    return event;
}

}
