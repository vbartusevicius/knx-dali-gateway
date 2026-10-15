#include "device/OledDisplay.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "commissioning/Commissioning.hpp"
#include "device/FrontPanel.hpp"
#include "device/StatusLeds.hpp"
#include "diagnostics/ErrorCode.hpp"

namespace gateway::device {
namespace {

constexpr std::array<std::array<std::uint8_t, 5>, 64> font_5x7{{
    {{0x00, 0x00, 0x00, 0x00, 0x00}},  // ' '
    {{0x00, 0x00, 0x5F, 0x00, 0x00}},  // '!'
    {{0x00, 0x07, 0x00, 0x07, 0x00}},  // '"'
    {{0x14, 0x7F, 0x14, 0x7F, 0x14}},  // '#'
    {{0x24, 0x2A, 0x7F, 0x2A, 0x12}},  // '$'
    {{0x23, 0x13, 0x08, 0x64, 0x62}},  // '%'
    {{0x36, 0x49, 0x55, 0x22, 0x50}},  // '&'
    {{0x00, 0x05, 0x03, 0x00, 0x00}},  // '\''
    {{0x00, 0x1C, 0x22, 0x41, 0x00}},  // '('
    {{0x00, 0x41, 0x22, 0x1C, 0x00}},  // ')'
    {{0x14, 0x08, 0x3E, 0x08, 0x14}},  // '*'
    {{0x08, 0x08, 0x3E, 0x08, 0x08}},  // '+'
    {{0x00, 0x50, 0x30, 0x00, 0x00}},  // ','
    {{0x08, 0x08, 0x08, 0x08, 0x08}},  // '-'
    {{0x00, 0x60, 0x60, 0x00, 0x00}},  // '.'
    {{0x20, 0x10, 0x08, 0x04, 0x02}},  // '/'
    {{0x3E, 0x51, 0x49, 0x45, 0x3E}},  // '0'
    {{0x00, 0x42, 0x7F, 0x40, 0x00}},  // '1'
    {{0x42, 0x61, 0x51, 0x49, 0x46}},  // '2'
    {{0x21, 0x41, 0x45, 0x4B, 0x31}},  // '3'
    {{0x18, 0x14, 0x12, 0x7F, 0x10}},  // '4'
    {{0x27, 0x45, 0x45, 0x45, 0x39}},  // '5'
    {{0x3C, 0x4A, 0x49, 0x49, 0x30}},  // '6'
    {{0x01, 0x71, 0x09, 0x05, 0x03}},  // '7'
    {{0x36, 0x49, 0x49, 0x49, 0x36}},  // '8'
    {{0x06, 0x49, 0x49, 0x29, 0x1E}},  // '9'
    {{0x00, 0x36, 0x36, 0x00, 0x00}},  // ':'
    {{0x00, 0x56, 0x36, 0x00, 0x00}},  // ';'
    {{0x08, 0x14, 0x22, 0x41, 0x00}},  // '<'
    {{0x14, 0x14, 0x14, 0x14, 0x14}},  // '='
    {{0x00, 0x41, 0x22, 0x14, 0x08}},  // '>'
    {{0x02, 0x01, 0x51, 0x09, 0x06}},  // '?'
    {{0x32, 0x49, 0x79, 0x41, 0x3E}},  // '@'
    {{0x7E, 0x11, 0x11, 0x11, 0x7E}},  // 'A'
    {{0x7F, 0x49, 0x49, 0x49, 0x36}},  // 'B'
    {{0x3E, 0x41, 0x41, 0x41, 0x22}},  // 'C'
    {{0x7F, 0x41, 0x41, 0x22, 0x1C}},  // 'D'
    {{0x7F, 0x49, 0x49, 0x49, 0x41}},  // 'E'
    {{0x7F, 0x09, 0x09, 0x09, 0x01}},  // 'F'
    {{0x3E, 0x41, 0x49, 0x49, 0x7A}},  // 'G'
    {{0x7F, 0x08, 0x08, 0x08, 0x7F}},  // 'H'
    {{0x00, 0x41, 0x7F, 0x41, 0x00}},  // 'I'
    {{0x20, 0x40, 0x41, 0x3F, 0x01}},  // 'J'
    {{0x7F, 0x08, 0x14, 0x22, 0x41}},  // 'K'
    {{0x7F, 0x40, 0x40, 0x40, 0x40}},  // 'L'
    {{0x7F, 0x02, 0x0C, 0x02, 0x7F}},  // 'M'
    {{0x7F, 0x04, 0x08, 0x10, 0x7F}},  // 'N'
    {{0x3E, 0x41, 0x41, 0x41, 0x3E}},  // 'O'
    {{0x7F, 0x09, 0x09, 0x09, 0x06}},  // 'P'
    {{0x3E, 0x41, 0x51, 0x21, 0x5E}},  // 'Q'
    {{0x7F, 0x09, 0x19, 0x29, 0x46}},  // 'R'
    {{0x46, 0x49, 0x49, 0x49, 0x31}},  // 'S'
    {{0x01, 0x01, 0x7F, 0x01, 0x01}},  // 'T'
    {{0x3F, 0x40, 0x40, 0x40, 0x3F}},  // 'U'
    {{0x1F, 0x20, 0x40, 0x20, 0x1F}},  // 'V'
    {{0x3F, 0x40, 0x38, 0x40, 0x3F}},  // 'W'
    {{0x63, 0x14, 0x08, 0x14, 0x63}},  // 'X'
    {{0x07, 0x08, 0x70, 0x08, 0x07}},  // 'Y'
    {{0x61, 0x51, 0x49, 0x45, 0x43}},  // 'Z'
    {{0x00, 0x7F, 0x41, 0x41, 0x00}},  // '['
    {{0x02, 0x04, 0x08, 0x10, 0x20}},  // '\\'
    {{0x00, 0x41, 0x41, 0x7F, 0x00}},  // ']'
    {{0x04, 0x02, 0x01, 0x02, 0x04}},  // '^'
    {{0x40, 0x40, 0x40, 0x40, 0x40}},  // '_'
}};

void format_ia(std::uint16_t ia, char* out, std::size_t size) noexcept {
    if (ia == 0 || ia == 0xFFFFu) {
        std::snprintf(out, size, "UNPROG");
        return;
    }
    const unsigned area = (ia >> 12U) & 0x0FU;
    const unsigned line = (ia >> 8U) & 0x0FU;
    const unsigned member = ia & 0xFFU;
    std::snprintf(out, size, "%u.%u.%u", area, line, member);
}

void format_uptime(std::uint32_t seconds, char* out, std::size_t size) noexcept {
    const unsigned days = seconds / 86400U;
    const unsigned hours = (seconds % 86400U) / 3600U;
    const unsigned mins = (seconds % 3600U) / 60U;
    if (days > 0) {
        std::snprintf(out, size, "%ud%02uh", days, hours);
    } else {
        std::snprintf(out, size, "%02u:%02u", hours, mins);
    }
}

bool modal_active(const OledSystemState& state) noexcept {
    return state.hint == PanelHint::FactoryResetArming || state.knx_prog_mode ||
           state.wifi_state == WifiState::Setup || commissioning::active(state.comm_phase);
}

void draw_header_64(OledCanvas& canvas, const char* title, unsigned page_num) noexcept {
    canvas.fill_rect(0, 0, 128, 10, true);
    canvas.draw_text(2, 1, title, false);
    char badge[8]{};
    std::snprintf(badge, sizeof(badge), "%u/%u", page_num, static_cast<unsigned>(oled_page_count));
    canvas.draw_text(108, 1, badge, false);
}

class Oled128x64Strategy final : public OledLayoutStrategy {
public:
    [[nodiscard]] OledGeometry geometry() const noexcept override { return OledGeometry::Oled128x64; }
    [[nodiscard]] std::uint8_t height() const noexcept override { return 64; }
    [[nodiscard]] std::uint8_t ssd1306_mux_ratio() const noexcept override { return 0x3FU; }
    [[nodiscard]] std::uint8_t ssd1306_com_pins() const noexcept override { return 0x12U; }

    void render_modal(OledCanvas& canvas, const OledSystemState& state) const noexcept override {
        canvas.draw_rect(0, 0, 128, 64, true);
        canvas.fill_rect(1, 1, 126, 12, true);
        if (state.hint == PanelHint::FactoryResetArming) {
            canvas.draw_text(10, 3, "!! FACTORY RESET !!", false);
            canvas.draw_text(6, 18, "KEEP HOLDING SW1+SW2", true);
            canvas.draw_text(6, 28, "TO ERASE KNX & NVS.", true);
            canvas.draw_text(6, 40, "RELEASE NOW TO ABORT", true);
            return;
        }
        if (state.knx_prog_mode) {
            canvas.draw_text(8, 3, "KNX PROGRAMMING MODE", false);
            char line[32]{};
            char ia[12]{};
            format_ia(state.knx_individual_address, ia, sizeof(ia));
            std::snprintf(line, sizeof(line), "CURRENT IA: %s", ia);
            canvas.draw_text(6, 18, line, true);
            canvas.draw_text(6, 30, "ETS DOWNLOAD READY", true);
            canvas.draw_text(6, 44, "HOLD SW1 5S TO EXIT", true);
            return;
        }
        if (state.wifi_state == WifiState::Setup) {
            canvas.draw_text(10, 3, "WIFI SETUP PORTAL", false);
            canvas.draw_text(6, 18, "AP: KNX-DALI-SETUP", true);
            canvas.draw_text(6, 30, "IP: 192.168.4.1", true);
            canvas.draw_text(6, 44, "OPEN IN BROWSER", true);
            return;
        }
        canvas.draw_text(10, 3, "DALI COMMISSIONING", false);
        char line[32]{};
        std::snprintf(line, sizeof(line), "PROGRESS: %u%%", static_cast<unsigned>(state.comm_progress));
        canvas.draw_text(6, 18, line, true);
        std::snprintf(line, sizeof(line), "FOUND:    %u GEAR", static_cast<unsigned>(state.comm_found));
        canvas.draw_text(6, 28, line, true);
        canvas.draw_rect(6, 42, 116, 12, true);
        canvas.fill_rect(8, 44, (112 * static_cast<int>(state.comm_progress)) / 100, 8, true);
    }

    void render_page(OledCanvas& canvas, OledPage page,
                     const OledSystemState& state) const noexcept override {
        switch (page) {
        case OledPage::Overview: {
            draw_header_64(canvas, "KNX-DALI GATEWAY", 1);
            char ia[12]{};
            format_ia(state.knx_individual_address, ia, sizeof(ia));
            char row1[32]{};
            std::snprintf(row1, sizeof(row1), "KNX:%s %s", ia, state.knx_bus_ok ? "BUS:OK" : "SAVEB!");
            canvas.draw_text(2, 13, row1);

            char row2[32]{};
            std::snprintf(row2, sizeof(row2), "DALI:%u/64 ON:%u F:%u",
                          static_cast<unsigned>(state.present_gear_count),
                          static_cast<unsigned>(state.active_lamp_count),
                          static_cast<unsigned>(state.faulty_gear_count));
            canvas.draw_text(2, 23, row2);

            char row3[32]{};
            std::snprintf(row3, sizeof(row3), "PSU:%s INP:%u BRI:%u%%", state.dali_psu_enabled ? "ON" : "EXT",
                          static_cast<unsigned>(state.input_device_count),
                          static_cast<unsigned>(state.average_brightness_pct));
            canvas.draw_text(2, 33, row3);

            char row4[32]{};
            if (state.wifi_state == WifiState::Connected) {
                std::snprintf(row4, sizeof(row4), "IP:%s M:%s", state.ipv4.data(),
                              state.mqtt_connected ? "OK" : "--");
            } else {
                std::snprintf(row4, sizeof(row4), "WIFI:%s MQTT:%s",
                              state.wifi_state == WifiState::Connecting ? "CONN" : "OFF",
                              state.mqtt_connected ? "OK" : "OFF");
            }
            canvas.draw_text(2, 43, row4);

            canvas.draw_rect(2, 53, 84, 9, true);
            canvas.fill_rect(4, 55, (80 * static_cast<int>(state.average_brightness_pct)) / 100, 5, true);

            char up[12]{};
            format_uptime(state.uptime_seconds, up, sizeof(up));
            canvas.draw_text(90, 54, up);
            break;
        }
        case OledPage::DaliMatrix: {
            draw_header_64(canvas, "DALI 64 LAMP MAP", 2);
            for (unsigned ch = 0; ch < 64; ++ch) {
                const int col = static_cast<int>(ch % 16);
                const int row = static_cast<int>(ch / 16);
                const int x = 2 + col * 6;
                const int y = 13 + row * 12;
                if (state.gear_faulty.test(ch)) {
                    canvas.draw_rect(x, y, 5, 10, true);
                    canvas.set_pixel(x + 2, y + 3, true);
                    canvas.set_pixel(x + 2, y + 4, true);
                    canvas.set_pixel(x + 2, y + 7, true);
                } else if (state.gear_on.test(ch)) {
                    canvas.fill_rect(x, y, 5, 10, true);
                } else if (state.gear_present.test(ch)) {
                    canvas.draw_rect(x, y, 5, 10, true);
                } else {
                    canvas.set_pixel(x + 2, y + 4, true);
                }
            }
            char side[12]{};
            std::snprintf(side, sizeof(side), "P:%02u", static_cast<unsigned>(state.present_gear_count));
            canvas.draw_text(100, 14, side);
            std::snprintf(side, sizeof(side), "L:%02u", static_cast<unsigned>(state.active_lamp_count));
            canvas.draw_text(100, 26, side);
            std::snprintf(side, sizeof(side), "F:%02u", static_cast<unsigned>(state.faulty_gear_count));
            canvas.draw_text(100, 38, side);
            std::snprintf(side, sizeof(side), "I:%02u", static_cast<unsigned>(state.input_device_count));
            canvas.draw_text(100, 50, side);
            break;
        }
        case OledPage::GroupsScenes: {
            draw_header_64(canvas, "GROUPS G00..G15", 3);
            unsigned active_groups = 0;
            for (unsigned g = 0; g < 16; ++g) {
                const int x = 4 + static_cast<int>(g) * 7;
                const int max_h = 34;
                const int bar_h = (static_cast<int>(state.group_levels[g]) * max_h) / 254;
                canvas.draw_rect(x, 14, 5, max_h, true);
                if (bar_h > 0) {
                    ++active_groups;
                    canvas.fill_rect(x + 1, 14 + (max_h - bar_h), 3, bar_h, true);
                }
            }
            char bottom[32]{};
            if (state.last_scene < 16) {
                std::snprintf(bottom, sizeof(bottom), "ACT:%u/16  SCENE:#%02u", active_groups,
                              static_cast<unsigned>(state.last_scene + 1));
            } else {
                std::snprintf(bottom, sizeof(bottom), "ACT:%u/16  SCENE:NONE", active_groups);
            }
            canvas.draw_text(2, 53, bottom);
            break;
        }
        case OledPage::NetworkHa: {
            draw_header_64(canvas, "NETWORK & HOMEAST", 4);
            char line[32]{};
            std::snprintf(line, sizeof(line), "SSID:%.15s", state.ssid.data());
            canvas.draw_text(2, 13, line);
            std::snprintf(line, sizeof(line), "IP:  %s", state.ipv4.data());
            canvas.draw_text(2, 23, line);
            std::snprintf(line, sizeof(line), "RSSI:%dDBM  WS:%u", static_cast<int>(state.wifi_rssi_dbm),
                          static_cast<unsigned>(state.ws_clients));
            canvas.draw_text(2, 33, line);
            std::snprintf(line, sizeof(line), "MQTT:%s  HA:%s", state.mqtt_connected ? "ONLINE" : "OFFLINE",
                          state.ha_discovery_enabled ? "AUTO" : "OFF");
            canvas.draw_text(2, 43, line);
            char up[12]{};
            format_uptime(state.uptime_seconds, up, sizeof(up));
            std::snprintf(line, sizeof(line), "UPTIME: %s", up);
            canvas.draw_text(2, 53, line);
            break;
        }
        case OledPage::Diagnostics: {
            draw_header_64(canvas, "DIAG & ERROR LOG", 5);
            char line[32]{};
            std::snprintf(line, sizeof(line), "DALI TX:%lu COL:%lu",
                          static_cast<unsigned long>(state.dali_tx_frames),
                          static_cast<unsigned long>(state.dali_collisions));
            canvas.draw_text(2, 13, line);
            std::snprintf(line, sizeof(line), "LOG ENTRIES: %lu",
                          static_cast<unsigned long>(state.logged_errors));
            canvas.draw_text(2, 23, line);
            const auto& desc = diagnostics::describe_error(state.last_error_code);
            std::snprintf(line, sizeof(line), "LAST: E%03u", static_cast<unsigned>(state.last_error_code));
            canvas.draw_text(2, 33, line);
            std::snprintf(line, sizeof(line), "%.20s", desc.symbol);
            canvas.draw_text(2, 43, line);
            if (state.last_error_address != 0xFFFFU) {
                std::snprintf(line, sizeof(line), "ADDR: A%02u",
                              static_cast<unsigned>(state.last_error_address & 0x3FU));
            } else {
                std::snprintf(line, sizeof(line), "ADDR: SYSTEM/BUS");
            }
            canvas.draw_text(2, 53, line);
            break;
        }
        }
    }
};

class Oled128x32Strategy final : public OledLayoutStrategy {
public:
    [[nodiscard]] OledGeometry geometry() const noexcept override { return OledGeometry::Oled128x32; }
    [[nodiscard]] std::uint8_t height() const noexcept override { return 32; }
    [[nodiscard]] std::uint8_t ssd1306_mux_ratio() const noexcept override { return 0x1FU; }
    [[nodiscard]] std::uint8_t ssd1306_com_pins() const noexcept override { return 0x02U; }

    void render_modal(OledCanvas& canvas, const OledSystemState& state) const noexcept override {
        canvas.draw_rect(0, 0, 128, 32, true);
        canvas.fill_rect(1, 1, 126, 9, true);
        if (state.hint == PanelHint::FactoryResetArming) {
            canvas.draw_text(10, 2, "! FACTORY RESET !", false);
            canvas.draw_text(4, 12, "HOLD SW1+SW2 TO WIPE", true);
            canvas.draw_text(4, 21, "RELEASE TO CANCEL", true);
            return;
        }
        if (state.knx_prog_mode) {
            canvas.draw_text(6, 2, "KNX PROG MODE ACTIVE", false);
            char ia[12]{};
            format_ia(state.knx_individual_address, ia, sizeof(ia));
            char line[32]{};
            std::snprintf(line, sizeof(line), "IA:%s ETS READY", ia);
            canvas.draw_text(4, 12, line, true);
            canvas.draw_text(4, 21, "HOLD SW1 5S TO EXIT", true);
            return;
        }
        if (state.wifi_state == WifiState::Setup) {
            canvas.draw_text(10, 2, "WIFI SETUP PORTAL", false);
            canvas.draw_text(4, 12, "AP: KNX-DALI-SETUP", true);
            canvas.draw_text(4, 21, "HTTP://192.168.4.1", true);
            return;
        }
        canvas.draw_text(8, 2, "DALI COMMISSIONING", false);
        char line[32]{};
        std::snprintf(line, sizeof(line), "%u%%  FOUND:%u", static_cast<unsigned>(state.comm_progress),
                      static_cast<unsigned>(state.comm_found));
        canvas.draw_text(4, 12, line, true);
        canvas.draw_rect(4, 21, 120, 8, true);
        canvas.fill_rect(6, 23, (116 * static_cast<int>(state.comm_progress)) / 100, 4, true);
    }

    void render_page(OledCanvas& canvas, OledPage page,
                     const OledSystemState& state) const noexcept override {
        switch (page) {
        case OledPage::Overview: {
            char ia[12]{};
            format_ia(state.knx_individual_address, ia, sizeof(ia));
            char line0[32]{};
            std::snprintf(line0, sizeof(line0), "KNX:%-7s %s 1/5", ia, state.knx_bus_ok ? "OK" : "LOW");
            canvas.draw_text(0, 0, line0);

            char line1[32]{};
            std::snprintf(line1, sizeof(line1), "DALI:%02u/64 ON:%02u F:%u",
                          static_cast<unsigned>(state.present_gear_count),
                          static_cast<unsigned>(state.active_lamp_count),
                          static_cast<unsigned>(state.faulty_gear_count));
            canvas.draw_text(0, 8, line1);

            char line2[32]{};
            if (state.wifi_state == WifiState::Connected) {
                std::snprintf(line2, sizeof(line2), "%-15s M:%s", state.ipv4.data(),
                              state.mqtt_connected ? "OK" : "--");
            } else {
                std::snprintf(line2, sizeof(line2), "WIFI:OFF  PSU:%s",
                              state.dali_psu_enabled ? "INT" : "EXT");
            }
            canvas.draw_text(0, 16, line2);

            canvas.draw_rect(0, 25, 76, 6, true);
            canvas.fill_rect(2, 27, (72 * static_cast<int>(state.average_brightness_pct)) / 100, 2, true);
            char pct[16]{};
            std::snprintf(pct, sizeof(pct), "BRI:%3u%%", static_cast<unsigned>(state.average_brightness_pct));
            canvas.draw_text(80, 24, pct);
            break;
        }
        case OledPage::DaliMatrix: {
            canvas.draw_text(0, 0, "DALI 64-LAMP MAP  2/5");
            for (unsigned ch = 0; ch < 64; ++ch) {
                const int col = static_cast<int>(ch % 32);
                const int row = static_cast<int>(ch / 32);
                const int x = col * 4;
                const int y = 9 + row * 7;
                if (state.gear_faulty.test(ch)) {
                    canvas.draw_rect(x, y, 3, 6, true);
                    canvas.set_pixel(x + 1, y + 2, true);
                } else if (state.gear_on.test(ch)) {
                    canvas.fill_rect(x, y, 3, 6, true);
                } else if (state.gear_present.test(ch)) {
                    canvas.draw_rect(x, y, 3, 6, true);
                } else {
                    canvas.set_pixel(x + 1, y + 2, true);
                }
            }
            char summary[32]{};
            std::snprintf(summary, sizeof(summary), "OK:%02u ON:%02u FLT:%u",
                          static_cast<unsigned>(state.present_gear_count),
                          static_cast<unsigned>(state.active_lamp_count),
                          static_cast<unsigned>(state.faulty_gear_count));
            canvas.draw_text(0, 24, summary);
            break;
        }
        case OledPage::GroupsScenes: {
            canvas.draw_text(0, 0, "GROUPS G00..G15   3/5");
            unsigned active_groups = 0;
            for (unsigned g = 0; g < 16; ++g) {
                const int x = static_cast<int>(g) * 8;
                const int max_h = 13;
                const int bar_h = (static_cast<int>(state.group_levels[g]) * max_h) / 254;
                canvas.draw_rect(x, 9, 6, max_h, true);
                if (bar_h > 0) {
                    ++active_groups;
                    canvas.fill_rect(x + 1, 9 + (max_h - bar_h), 4, bar_h, true);
                }
            }
            char bottom[32]{};
            if (state.last_scene < 16) {
                std::snprintf(bottom, sizeof(bottom), "ACT:%u/16  SCENE:#%02u", active_groups,
                              static_cast<unsigned>(state.last_scene + 1));
            } else {
                std::snprintf(bottom, sizeof(bottom), "ACT:%u/16  SCENE:--", active_groups);
            }
            canvas.draw_text(0, 24, bottom);
            break;
        }
        case OledPage::NetworkHa: {
            char line0[32]{};
            std::snprintf(line0, sizeof(line0), "WIFI:%ddBm WS:%u 4/5", static_cast<int>(state.wifi_rssi_dbm),
                          static_cast<unsigned>(state.ws_clients));
            canvas.draw_text(0, 0, line0);
            char line1[32]{};
            std::snprintf(line1, sizeof(line1), "IP:%s", state.ipv4.data());
            canvas.draw_text(0, 8, line1);
            char line2[32]{};
            std::snprintf(line2, sizeof(line2), "MQTT:%s HA:%s", state.mqtt_connected ? "ON " : "OFF",
                          state.ha_discovery_enabled ? "AUTO" : "OFF");
            canvas.draw_text(0, 16, line2);
            char up[12]{};
            format_uptime(state.uptime_seconds, up, sizeof(up));
            char line3[32]{};
            std::snprintf(line3, sizeof(line3), "UP:%s SSID:%.7s", up, state.ssid.data());
            canvas.draw_text(0, 24, line3);
            break;
        }
        case OledPage::Diagnostics: {
            char line[32]{};
            std::snprintf(line, sizeof(line), "DIAG TX:%lu  5/5",
                          static_cast<unsigned long>(state.dali_tx_frames));
            canvas.draw_text(0, 0, line);
            std::snprintf(line, sizeof(line), "COL:%lu LOG:%lu",
                          static_cast<unsigned long>(state.dali_collisions),
                          static_cast<unsigned long>(state.logged_errors));
            canvas.draw_text(0, 8, line);
            const auto& desc = diagnostics::describe_error(state.last_error_code);
            std::snprintf(line, sizeof(line), "E%03u:%.15s", static_cast<unsigned>(state.last_error_code),
                          desc.symbol);
            canvas.draw_text(0, 16, line);
            if (state.last_error_address != 0xFFFFU) {
                std::snprintf(line, sizeof(line), "TARGET: A%02u",
                              static_cast<unsigned>(state.last_error_address & 0x3FU));
            } else {
                std::snprintf(line, sizeof(line), "TARGET: BUS/SYS");
            }
            canvas.draw_text(0, 24, line);
            break;
        }
        }
    }
};

class DisabledOledStrategy final : public OledLayoutStrategy {
public:
    [[nodiscard]] OledGeometry geometry() const noexcept override { return OledGeometry::Disabled; }
    [[nodiscard]] std::uint8_t height() const noexcept override { return 0; }
    [[nodiscard]] std::uint8_t ssd1306_mux_ratio() const noexcept override { return 0; }
    [[nodiscard]] std::uint8_t ssd1306_com_pins() const noexcept override { return 0; }
    void render_modal(OledCanvas&, const OledSystemState&) const noexcept override {}
    void render_page(OledCanvas&, OledPage, const OledSystemState&) const noexcept override {}
};

const Oled128x64Strategy strategy_64{};
const Oled128x32Strategy strategy_32{};
const DisabledOledStrategy strategy_disabled{};

}  // namespace

const OledLayoutStrategy& select_oled_strategy(OledGeometry geometry) noexcept {
    switch (geometry) {
    case OledGeometry::Oled128x64:
        return strategy_64;
    case OledGeometry::Oled128x32:
    case OledGeometry::Auto:
        return strategy_32;
    case OledGeometry::Disabled:
        return strategy_disabled;
    }
    return strategy_32;
}

void OledCanvas::reset(std::uint8_t height) noexcept {
    height_ = height;
    buffer_.fill(0);
}

void OledCanvas::set_pixel(int x, int y, bool on) noexcept {
    if (x < 0 || x >= static_cast<int>(oled_width) || y < 0 || y >= static_cast<int>(height_)) {
        return;
    }
    const auto index = static_cast<std::size_t>(y / 8) * oled_width + static_cast<std::size_t>(x);
    const auto mask = static_cast<std::uint8_t>(1U << (y & 7));
    if (on) {
        buffer_[index] = static_cast<std::uint8_t>(buffer_[index] | mask);
    } else {
        buffer_[index] = static_cast<std::uint8_t>(buffer_[index] & ~mask);
    }
}

bool OledCanvas::pixel(std::uint8_t x, std::uint8_t y) const noexcept {
    if (x >= oled_width || y >= height_) {
        return false;
    }
    const auto index = static_cast<std::size_t>(y / 8U) * oled_width + x;
    return (buffer_[index] & (1U << (y & 7U))) != 0;
}

void OledCanvas::fill_rect(int x, int y, int w, int h, bool on) noexcept {
    for (int py = y; py < y + h; ++py) {
        for (int px = x; px < x + w; ++px) {
            set_pixel(px, py, on);
        }
    }
}

void OledCanvas::draw_rect(int x, int y, int w, int h, bool on) noexcept {
    if (w <= 0 || h <= 0) {
        return;
    }
    for (int px = x; px < x + w; ++px) {
        set_pixel(px, y, on);
        set_pixel(px, y + h - 1, on);
    }
    for (int py = y; py < y + h; ++py) {
        set_pixel(x, py, on);
        set_pixel(x + w - 1, py, on);
    }
}

void OledCanvas::draw_char(int x, int y, char c, bool on) noexcept {
    auto uc = static_cast<unsigned char>(c);
    if (uc >= 'a' && uc <= 'z') {
        uc = static_cast<unsigned char>(uc - ('a' - 'A'));
    }
    if (uc < 32 || uc > 95) {
        uc = '?';
    }
    const auto& glyph = font_5x7[uc - 32];
    for (int col = 0; col < 5; ++col) {
        const std::uint8_t bits = glyph[static_cast<std::size_t>(col)];
        for (int row = 0; row < 7; ++row) {
            if ((bits & (1U << row)) != 0) {
                set_pixel(x + col, y + row, on);
            }
        }
    }
}

void OledCanvas::draw_text(int x, int y, const char* text, bool on) noexcept {
    if (!text) {
        return;
    }
    int cursor = x;
    for (const char* p = text; *p != '\0'; ++p) {
        if (cursor + 5 > static_cast<int>(oled_width)) {
            break;
        }
        draw_char(cursor, y, *p, on);
        cursor += 6;
    }
}

OledDisplay::OledDisplay(OledGeometry geometry) noexcept : strategy_(&select_oled_strategy(geometry)) {}

void OledDisplay::set_geometry(OledGeometry geometry) noexcept {
    strategy_ = &select_oled_strategy(geometry);
}

void OledDisplay::next_page(std::uint32_t now_ms) noexcept {
    if (sleeping(now_ms)) {
        wake(now_ms);
        return;
    }
    wake(now_ms);
    const auto idx = (static_cast<unsigned>(page_) + 1U) % oled_page_count;
    page_ = static_cast<OledPage>(idx);
}

void OledDisplay::set_page(OledPage page, std::uint32_t now_ms) noexcept {
    wake(now_ms);
    page_ = page;
}

void OledDisplay::wake(std::uint32_t now_ms) noexcept {
    last_wake_ms_ = now_ms;
}

bool OledDisplay::sleeping(std::uint32_t now_ms) const noexcept {
    if (sleep_timeout_ms_ == 0) {
        return false;
    }
    return now_ms >= last_wake_ms_ && (now_ms - last_wake_ms_) >= sleep_timeout_ms_;
}

void OledDisplay::render(const OledSystemState& state, std::uint32_t now_ms) noexcept {
    canvas_.reset(strategy_->height());
    if (strategy_->geometry() == OledGeometry::Disabled) {
        return;
    }
    if (modal_active(state)) {
        wake(now_ms);
        strategy_->render_modal(canvas_, state);
        return;
    }
    if (sleeping(now_ms)) {
        return;
    }
    strategy_->render_page(canvas_, page_, state);
}

}
