#include "firmware/I2cOled.hpp"

#include <array>
#include <cstring>

#include <esp_log.h>

#include "firmware/Board.hpp"

namespace gateway::firmware {
namespace {

constexpr const char* log_tag = "oled";
constexpr std::uint32_t i2c_clock_hz = 400'000;
constexpr int i2c_timeout_ms = 25;

}  // namespace

I2cOled::~I2cOled() {
    if (dev_) {
        i2c_master_bus_rm_device(dev_);
    }
    if (bus_) {
        i2c_del_master_bus(bus_);
    }
}

bool I2cOled::init(device::OledGeometry requested) noexcept {
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = board::i2c_sda,
        .scl_io_num = board::i2c_scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {.enable_internal_pullup = true, .allow_pd = false},
    };
    if (i2c_new_master_bus(&bus_cfg, &bus_) != ESP_OK) {
        ESP_LOGW(log_tag, "I2C master bus init failed");
        return false;
    }

    for (const std::uint8_t candidate : {std::uint8_t{0x3C}, std::uint8_t{0x3D}}) {
        if (i2c_master_probe(bus_, candidate, i2c_timeout_ms) == ESP_OK) {
            addr_ = candidate;
            present_ = true;
            break;
        }
    }
    if (!present_) {
        ESP_LOGI(log_tag, "No SSD1306 OLED detected on I2C (0x3C/0x3D)");
        return false;
    }

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr_,
        .scl_speed_hz = i2c_clock_hz,
        .scl_wait_us = 0,
        .flags = {.disable_ack_check = false},
    };
    if (i2c_master_bus_add_device(bus_, &dev_cfg, &dev_) != ESP_OK) {
        present_ = false;
        return false;
    }

    const auto geom =
        (requested == device::OledGeometry::Auto) ? device::OledGeometry::Oled64x128 : requested;
    return init_controller(geom);
}

void I2cOled::reconfigure(device::OledGeometry geometry) noexcept {
    if (!present_) {
        return;
    }
    if (geometry == device::OledGeometry::Disabled) {
        const std::array<std::uint8_t, 1> off_cmd{0xAE};
        (void)send_commands(off_cmd.data(), off_cmd.size());
        display_on_ = false;
        active_geometry_ = geometry;
        return;
    }
    const auto resolved =
        (geometry == device::OledGeometry::Auto) ? device::OledGeometry::Oled64x128 : geometry;
    if (resolved != active_geometry_ || !display_on_) {
        (void)init_controller(resolved);
    }
}

bool I2cOled::send_commands(const std::uint8_t* cmds, std::size_t len) noexcept {
    if (!dev_ || len == 0 || len > 32) {
        return false;
    }
    std::array<std::uint8_t, 33> payload{};
    payload[0] = 0x00;  // Co = 0, D/C# = 0 (command stream)
    std::memcpy(payload.data() + 1, cmds, len);
    return i2c_master_transmit(dev_, payload.data(), len + 1, i2c_timeout_ms) == ESP_OK;
}

bool I2cOled::init_controller(device::OledGeometry geometry) noexcept {
    const auto& strategy = device::select_oled_strategy(geometry);
    active_geometry_ = strategy.geometry();
    if (active_geometry_ == device::OledGeometry::Disabled) {
        const std::array<std::uint8_t, 1> off_cmd{0xAE};
        display_on_ = false;
        return send_commands(off_cmd.data(), off_cmd.size());
    }
    const std::uint8_t mux = strategy.ssd1306_mux_ratio();
    const std::uint8_t com_pins = strategy.ssd1306_com_pins();
    const std::array<std::uint8_t, 25> init_seq{
        0xAE,                        // Display OFF
        0xD5, 0x80,                  // Set oscillator clock
        0xA8, mux,                   // Multiplex ratio from OledLayoutStrategy
        0xD3, 0x00,                  // Display offset 0
        0x40,                        // Start line 0
        0x8D, 0x14,                  // Enable internal charge pump
        0x20, 0x00,                  // Horizontal addressing mode
        0xA1,                        // Segment remap
        0xC8,                        // COM scan direction remapped
        0xDA, com_pins, 0x81, 0xCF,  // COM pins from OledLayoutStrategy + Contrast
        0xD9, 0xF1,                  // Pre-charge period
        0xDB, 0x40,                  // VCOMH deselect level
        0xA4,                        // Entire display ON from RAM
        0xA6,                        // Normal (non-inverted) display
        0xAF                         // Display ON
    };
    display_on_ = send_commands(init_seq.data(), init_seq.size());
    ESP_LOGI(log_tag, "Initialized SSD1306 at 0x%02X as 128x%u", addr_,
             static_cast<unsigned>(strategy.height()));
    return display_on_;
}

void I2cOled::flush(const device::OledDisplay& display, bool sleep) noexcept {
    if (!present_ || !dev_ || active_geometry_ == device::OledGeometry::Disabled) {
        return;
    }
    if (sleep) {
        if (display_on_) {
            const std::array<std::uint8_t, 1> off_cmd{0xAE};
            (void)send_commands(off_cmd.data(), off_cmd.size());
            display_on_ = false;
        }
        return;
    }
    if (!display_on_) {
        const std::array<std::uint8_t, 1> on_cmd{0xAF};
        (void)send_commands(on_cmd.data(), on_cmd.size());
        display_on_ = true;
    }

    const std::uint8_t pages = display.height() / 8U;
    const std::size_t row_w = display.width();
    const auto& fb = display.buffer();
    for (std::uint8_t page = 0; page < pages; ++page) {
        const std::array<std::uint8_t, 3> pos_cmd{
            static_cast<std::uint8_t>(0xB0U | page),
            0x00U,
            0x10U,
        };
        if (!send_commands(pos_cmd.data(), pos_cmd.size())) {
            return;
        }
        std::array<std::uint8_t, 1 + device::oled_width> page_buf{};
        page_buf[0] = 0x40U;  // Co = 0, D/C# = 1 (GDDRAM data stream)
        std::memcpy(page_buf.data() + 1, fb.data() + static_cast<std::size_t>(page) * row_w, row_w);
        (void)i2c_master_transmit(dev_, page_buf.data(), row_w + 1, i2c_timeout_ms);
    }
}

}
