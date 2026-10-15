#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>

#include "commissioning/Commissioning.hpp"
#include "device/FrontPanel.hpp"
#include "device/StatusLeds.hpp"
#include "diagnostics/ErrorCode.hpp"

namespace gateway::device {

enum class OledGeometry : std::uint8_t {
    Auto = 0,
    Oled128x64 = 1,  // V1 / V2: Winstar WEA012864D (0.96" 128x64 landscape SSD1306)
    Oled64x128 = 2,  // V3: 0.96" 64x128 portrait (14.0x28.0mm SSD1312 / SH1107)
    Disabled = 3
};

enum class OledPage : std::uint8_t {
    Overview = 0,
    DaliMatrix = 1,
    GroupsScenes = 2,
    NetworkHa = 3,
    Diagnostics = 4
};

inline constexpr std::size_t oled_page_count = 5;
inline constexpr std::size_t oled_width = 128;
inline constexpr std::size_t oled_max_height = 64;
inline constexpr std::size_t oled_max_pages = oled_max_height / 8;
inline constexpr std::size_t oled_buffer_size = oled_width * oled_max_pages;  // 1024 bytes

struct OledSystemState {
    std::uint16_t knx_individual_address = 0xFFFF;
    bool knx_configured = false;
    bool knx_prog_mode = false;
    bool knx_bus_ok = true;
    bool dali_psu_enabled = true;
    std::uint8_t present_gear_count = 0;
    std::uint8_t active_lamp_count = 0;
    std::uint8_t faulty_gear_count = 0;
    std::uint8_t input_device_count = 0;
    std::uint8_t average_brightness_pct = 0;
    std::uint8_t last_scene = 0xFF;
    std::bitset<64> gear_present{};
    std::bitset<64> gear_on{};
    std::bitset<64> gear_faulty{};
    std::array<std::uint8_t, 16> group_levels{};
    WifiState wifi_state = WifiState::Off;
    std::int8_t wifi_rssi_dbm = 0;
    std::array<char, 16> ipv4{"0.0.0.0"};
    std::array<char, 24> ssid{"-"};
    bool mqtt_connected = false;
    bool ha_discovery_enabled = false;
    std::uint8_t ws_clients = 0;
    std::uint32_t dali_tx_frames = 0;
    std::uint32_t dali_collisions = 0;
    std::uint32_t logged_errors = 0;
    diagnostics::ErrorCode last_error_code = diagnostics::ErrorCode::None;
    std::uint16_t last_error_address = 0xFFFF;
    std::uint32_t uptime_seconds = 0;
    commissioning::Phase comm_phase = commissioning::Phase::Idle;
    std::uint8_t comm_progress = 0;
    std::uint8_t comm_found = 0;
    PanelHint hint = PanelHint::None;
};

class OledCanvas final {
public:
    void reset(std::uint8_t width, std::uint8_t height) noexcept;
    [[nodiscard]] std::uint8_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint8_t height() const noexcept { return height_; }
    void set_pixel(int x, int y, bool on = true) noexcept;
    [[nodiscard]] bool pixel(std::uint8_t x, std::uint8_t y) const noexcept;
    void fill_rect(int x, int y, int w, int h, bool on = true) noexcept;
    void draw_rect(int x, int y, int w, int h, bool on = true) noexcept;
    void draw_char(int x, int y, char c, bool on = true) noexcept;
    void draw_text(int x, int y, const char* text, bool on = true) noexcept;
    [[nodiscard]] const std::array<std::uint8_t, oled_buffer_size>& buffer() const noexcept {
        return buffer_;
    }

private:
    std::uint8_t width_ = 64;
    std::uint8_t height_ = 128;
    std::array<std::uint8_t, oled_buffer_size> buffer_{};
};

// Strategy interface for different OLED panel geometries (128x64 V1/V2
// landscape, 64x128 V3 portrait, or Disabled).
class OledLayoutStrategy {
public:
    virtual ~OledLayoutStrategy() = default;
    [[nodiscard]] virtual OledGeometry geometry() const noexcept = 0;
    [[nodiscard]] virtual std::uint8_t width() const noexcept { return 128; }
    [[nodiscard]] virtual std::uint8_t height() const noexcept = 0;
    [[nodiscard]] virtual std::uint8_t ssd1306_mux_ratio() const noexcept = 0;
    [[nodiscard]] virtual std::uint8_t ssd1306_com_pins() const noexcept = 0;
    virtual void render_modal(OledCanvas& canvas, const OledSystemState& state) const noexcept = 0;
    virtual void render_page(OledCanvas& canvas, OledPage page,
                             const OledSystemState& state) const noexcept = 0;
};

[[nodiscard]] const OledLayoutStrategy& select_oled_strategy(OledGeometry geometry) noexcept;

class OledDisplay final {
public:
    static constexpr std::uint32_t default_sleep_timeout_ms = 60'000;

    explicit OledDisplay(OledGeometry geometry = OledGeometry::Oled64x128) noexcept;

    void set_geometry(OledGeometry geometry) noexcept;
    [[nodiscard]] const OledLayoutStrategy& strategy() const noexcept { return *strategy_; }
    [[nodiscard]] OledGeometry resolved_geometry() const noexcept { return strategy_->geometry(); }
    [[nodiscard]] std::uint8_t width() const noexcept { return strategy_->width(); }
    [[nodiscard]] std::uint8_t height() const noexcept { return strategy_->height(); }
    [[nodiscard]] std::size_t framebuffer_bytes() const noexcept {
        return static_cast<std::size_t>(width()) * (height() / 8U);
    }

    void next_page(std::uint32_t now_ms) noexcept;
    void set_page(OledPage page, std::uint32_t now_ms) noexcept;
    [[nodiscard]] OledPage page() const noexcept { return page_; }

    void wake(std::uint32_t now_ms) noexcept;
    void set_sleep_timeout_ms(std::uint32_t timeout_ms) noexcept { sleep_timeout_ms_ = timeout_ms; }
    [[nodiscard]] bool sleeping(std::uint32_t now_ms) const noexcept;

    void render(const OledSystemState& state, std::uint32_t now_ms) noexcept;

    [[nodiscard]] bool pixel(std::uint8_t x, std::uint8_t y) const noexcept { return canvas_.pixel(x, y); }
    [[nodiscard]] const std::array<std::uint8_t, oled_buffer_size>& buffer() const noexcept {
        return canvas_.buffer();
    }

private:
    const OledLayoutStrategy* strategy_;
    OledPage page_ = OledPage::Overview;
    std::uint32_t last_wake_ms_ = 0;
    std::uint32_t sleep_timeout_ms_ = default_sleep_timeout_ms;
    OledCanvas canvas_{};
};

}
