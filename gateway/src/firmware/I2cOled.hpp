#pragma once

#include <cstdint>

#include <driver/i2c_master.h>

#include "device/OledDisplay.hpp"

namespace gateway::firmware {

class I2cOled final {
public:
    I2cOled() noexcept = default;
    ~I2cOled();
    I2cOled(const I2cOled&) = delete;
    I2cOled& operator=(const I2cOled&) = delete;

    bool init(device::OledGeometry requested = device::OledGeometry::Auto) noexcept;
    void reconfigure(device::OledGeometry geometry) noexcept;
    void flush(const device::OledDisplay& display, bool sleep) noexcept;

    [[nodiscard]] bool present() const noexcept { return present_; }
    [[nodiscard]] std::uint8_t i2c_address() const noexcept { return addr_; }
    [[nodiscard]] device::OledGeometry active_geometry() const noexcept { return active_geometry_; }

private:
    bool send_commands(const std::uint8_t* cmds, std::size_t len) noexcept;
    bool init_controller(device::OledGeometry geometry) noexcept;

    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t dev_ = nullptr;
    std::uint8_t addr_ = 0x3C;
    device::OledGeometry active_geometry_ = device::OledGeometry::Oled128x32;
    bool present_ = false;
    bool display_on_ = false;
};

}
