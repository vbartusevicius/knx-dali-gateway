#pragma once

#include <cstdint>

#include "control/Command.hpp"
#include "firmware/I2cOled.hpp"
#include "firmware/Queue.hpp"

namespace gateway::firmware {

class NetworkServices final {
public:
    NetworkServices(Queue<control::Command>& commands, I2cOled& oled) noexcept;

    void start() noexcept;
    void start_wifi_setup_portal() noexcept;
    void emergency_power_down() noexcept;

private:
    static void task_entry(void* arg);
    void loop() noexcept;

    Queue<control::Command>& commands_;
    I2cOled& oled_;
};

}
