// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2025 the knx-dali-gateway contributors
#pragma once

#include "dali/BusPort.hpp"

namespace gateway::espidf {

// dali::BusPort on the ESP32 RMT peripheral: a TX channel emits the
// Manchester waveform, an RX channel in continuous receive reports bursts
// to a dedicated high-priority task that runs the decoder and the
// multi-master link layer. Worker calls to transact() block until the link
// layer resolves the transaction; unsolicited frames queue for poll_event().
class RmtDaliBus final : public dali::BusPort {
public:
    struct Config {
        int tx_gpio;
        int rx_gpio;
        bool invert_tx;  // board optocoupler inverts the drive direction
        bool invert_rx;
    };

    explicit RmtDaliBus(const Config& config) noexcept;
    ~RmtDaliBus() override;
    RmtDaliBus(const RmtDaliBus&) = delete;
    RmtDaliBus& operator=(const RmtDaliBus&) = delete;

    // Creates the RMT channels, queues and the bus task; false leaves the
    // bus unconfigured and transact() reports Failure.
    bool start() noexcept;
    bool configured() const noexcept;

    dali::bus::TxResult transact(const dali::BusRequest& request) override;
    std::optional<dali::BusEvent> poll_event() override;

private:
    struct Impl;
    Impl* impl_;
    Config config_;
};

}
