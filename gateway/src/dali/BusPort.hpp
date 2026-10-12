#pragma once

#include <cstdint>
#include <optional>

#include "dali/Frame.hpp"
#include "dali/bus/LinkLayer.hpp"

namespace gateway::dali {

// One queued transaction on the bus layer.
struct BusRequest {
    std::uint32_t value;
    std::uint8_t bits;  // 8/16/24
    bool expect_reply;
    bus::Priority priority;
    std::uint32_t repeat_gap_us;  // >0: resend once after this spacing
};

// Synchronous port to the physical DALI bus: transact blocks until the
// link layer resolves the transaction. Implemented by RmtDaliBus on
// ESP-IDF and SocketBusPort in the integration lab.
class BusPort {
public:
    virtual ~BusPort() = default;
    virtual bus::TxResult transact(const BusRequest& request) = 0;
    virtual std::optional<BusEvent> poll_event() { return std::nullopt; }
};

}
