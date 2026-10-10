#pragma once

#include <knx_facade.h>

#include "config/Configuration.hpp"
#include "control/Command.hpp"
#include "firmware/Board.hpp"
#include "firmware/Queue.hpp"

namespace gateway::firmware {

// KNX TP device on the NCN5121 UART: maps bus writes on the channel group objects to
// lighting commands and publishes channel status back onto the status objects.
class KnxDevice final {
public:
    KnxDevice(Queue<control::Command>& commands, Queue<control::StatusUpdate>& statuses,
              Queue<config::Configuration>& configuration) noexcept;
    KnxDevice(const KnxDevice&) = delete;
    KnxDevice& operator=(const KnxDevice&) = delete;

    [[noreturn]] void run();

private:
    static void prog_button_isr(void* arg);

    void note_button_edge();
    void handle_button();
    void attach_objects();
    void on_group_object(GroupObject& object);
    void publish_statuses();

    Esp32IdfPlatform platform_{board::knx_uart};
    Bau07B0 bau_{platform_};
    KnxFacade<Esp32IdfPlatform, Bau07B0> knx_{bau_};
    Queue<control::Command>& commands_;
    Queue<control::StatusUpdate>& statuses_;
    Queue<config::Configuration>& configuration_;
    volatile TickType_t last_button_edge_ = 0;
    volatile bool button_pending_ = false;
    bool objects_attached_ = false;
};

}
