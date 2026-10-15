#include "device/OledDisplay.hpp"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "commissioning/Commissioning.hpp"
#include "device/FrontPanel.hpp"
#include "device/StatusLeds.hpp"
#include "diagnostics/ErrorCode.hpp"

namespace gateway::device {
namespace {

// 1. Normal 5x7 font (6 px pitch x 8 px row):
// Used for all page headers, IPv4 address, and modal primary lines.
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

// 2. Medium-small 4x6 / 5x6 font (variable width: 4 cols for standard glyphs,
// 5 cols for M/N/W/X/Y/m so 3-stem letters have a true center column, and
// 2 cols for '.' ':' ',').
struct MediumGlyph {
    std::uint8_t width;
    std::array<std::uint8_t, 5> cols;
};

constexpr std::array<MediumGlyph, 64> font_4x6{{
    {3, {{0x00, 0x00, 0x00, 0x00, 0x00}}},  // ' '
    {4, {{0x00, 0x2F, 0x00, 0x00, 0x00}}},  // '!'
    {4, {{0x03, 0x00, 0x03, 0x00, 0x00}}},  // '"'
    {4, {{0x14, 0x3F, 0x14, 0x3F, 0x00}}},  // '#'
    {4, {{0x12, 0x3F, 0x25, 0x00, 0x00}}},  // '$'
    {4, {{0x23, 0x0C, 0x32, 0x00, 0x00}}},  // '%'
    {4, {{0x1A, 0x25, 0x1A, 0x20, 0x00}}},  // '&'
    {4, {{0x00, 0x03, 0x00, 0x00, 0x00}}},  // '\''
    {4, {{0x00, 0x1E, 0x21, 0x00, 0x00}}},  // '('
    {4, {{0x21, 0x1E, 0x00, 0x00, 0x00}}},  // ')'
    {4, {{0x14, 0x08, 0x14, 0x00, 0x00}}},  // '*'
    {4, {{0x08, 0x1C, 0x08, 0x00, 0x00}}},  // '+'
    {2, {{0x20, 0x10, 0x00, 0x00, 0x00}}},  // ','
    {4, {{0x08, 0x08, 0x08, 0x00, 0x00}}},  // '-'
    {2, {{0x20, 0x00, 0x00, 0x00, 0x00}}},  // '.'
    {4, {{0x20, 0x18, 0x06, 0x01, 0x00}}},  // '/'
    {4, {{0x1E, 0x29, 0x25, 0x1E, 0x00}}},  // '0'
    {4, {{0x22, 0x3F, 0x20, 0x00, 0x00}}},  // '1'
    {4, {{0x32, 0x29, 0x25, 0x22, 0x00}}},  // '2'
    {4, {{0x21, 0x25, 0x25, 0x1A, 0x00}}},  // '3'
    {4, {{0x0F, 0x08, 0x3F, 0x08, 0x00}}},  // '4'
    {4, {{0x27, 0x25, 0x25, 0x19, 0x00}}},  // '5'
    {4, {{0x1E, 0x25, 0x25, 0x18, 0x00}}},  // '6'
    {4, {{0x01, 0x31, 0x0D, 0x03, 0x00}}},  // '7'
    {4, {{0x1A, 0x25, 0x25, 0x1A, 0x00}}},  // '8'
    {4, {{0x06, 0x29, 0x29, 0x1E, 0x00}}},  // '9'
    {2, {{0x14, 0x00, 0x00, 0x00, 0x00}}},  // ':'
    {4, {{0x20, 0x14, 0x00, 0x00, 0x00}}},  // ';'
    {4, {{0x08, 0x14, 0x22, 0x00, 0x00}}},  // '<'
    {4, {{0x14, 0x14, 0x14, 0x00, 0x00}}},  // '='
    {4, {{0x22, 0x14, 0x08, 0x00, 0x00}}},  // '>'
    {4, {{0x02, 0x29, 0x05, 0x02, 0x00}}},  // '?'
    {4, {{0x1E, 0x21, 0x2D, 0x1E, 0x00}}},  // '@'
    {4, {{0x3E, 0x09, 0x09, 0x3E, 0x00}}},  // 'A'
    {4, {{0x3F, 0x25, 0x25, 0x1A, 0x00}}},  // 'B'
    {4, {{0x1E, 0x21, 0x21, 0x12, 0x00}}},  // 'C'
    {4, {{0x3F, 0x21, 0x21, 0x1E, 0x00}}},  // 'D'
    {4, {{0x3F, 0x25, 0x25, 0x21, 0x00}}},  // 'E'
    {4, {{0x3F, 0x05, 0x05, 0x01, 0x00}}},  // 'F'
    {4, {{0x1E, 0x21, 0x29, 0x3A, 0x00}}},  // 'G'
    {4, {{0x3F, 0x08, 0x08, 0x3F, 0x00}}},  // 'H'
    {4, {{0x21, 0x3F, 0x21, 0x00, 0x00}}},  // 'I'
    {4, {{0x10, 0x20, 0x21, 0x1F, 0x00}}},  // 'J'
    {4, {{0x3F, 0x08, 0x14, 0x23, 0x00}}},  // 'K'
    {4, {{0x3F, 0x20, 0x20, 0x20, 0x00}}},  // 'L'
    {5, {{0x3F, 0x02, 0x04, 0x02, 0x3F}}},  // 'M'
    {5, {{0x3F, 0x02, 0x04, 0x08, 0x3F}}},  // 'N'
    {4, {{0x1E, 0x21, 0x21, 0x1E, 0x00}}},  // 'O'
    {4, {{0x3F, 0x09, 0x09, 0x06, 0x00}}},  // 'P'
    {4, {{0x1E, 0x21, 0x31, 0x3E, 0x00}}},  // 'Q'
    {4, {{0x3F, 0x09, 0x19, 0x26, 0x00}}},  // 'R'
    {4, {{0x12, 0x25, 0x25, 0x18, 0x00}}},  // 'S'
    {4, {{0x01, 0x3F, 0x01, 0x01, 0x00}}},  // 'T'
    {4, {{0x1F, 0x20, 0x20, 0x1F, 0x00}}},  // 'U'
    {4, {{0x0F, 0x10, 0x20, 0x1F, 0x00}}},  // 'V'
    {5, {{0x3F, 0x10, 0x08, 0x10, 0x3F}}},  // 'W'
    {5, {{0x23, 0x14, 0x08, 0x14, 0x23}}},  // 'X'
    {5, {{0x03, 0x04, 0x38, 0x04, 0x03}}},  // 'Y'
    {4, {{0x31, 0x29, 0x25, 0x23, 0x00}}},  // 'Z'
    {4, {{0x3F, 0x21, 0x00, 0x00, 0x00}}},  // '['
    {4, {{0x01, 0x06, 0x18, 0x20, 0x00}}},  // '\\'
    {4, {{0x00, 0x21, 0x3F, 0x00, 0x00}}},  // ']'
    {4, {{0x02, 0x01, 0x02, 0x00, 0x00}}},  // '^'
    {4, {{0x20, 0x20, 0x20, 0x20, 0x00}}},  // '_'
}};

constexpr MediumGlyph glyph_lowercase_m{5, {{0x3C, 0x04, 0x38, 0x04, 0x38}}};

const MediumGlyph& lookup_medium_glyph(char c) noexcept {
    if (c == 'm') {
        return glyph_lowercase_m;
    }
    auto uc = static_cast<unsigned char>(c);
    if (uc >= 'a' && uc <= 'z') {
        uc = static_cast<unsigned char>(uc - ('a' - 'A'));
    }
    if (uc < 32 || uc > 95) {
        uc = '?';
    }
    return font_4x6[uc - 32];
}

// 3. Compact 3x5 micro-font (4 px pitch x 6 px row) for Page 3 Error Log body
constexpr std::array<std::array<std::uint8_t, 3>, 64> font_3x5{{
    {{0x00, 0x00, 0x00}},  // ' '
    {{0x00, 0x17, 0x00}},  // '!'
    {{0x03, 0x00, 0x03}},  // '"'
    {{0x1F, 0x0A, 0x1F}},  // '#'
    {{0x0D, 0x1F, 0x16}},  // '$'
    {{0x19, 0x04, 0x13}},  // '%'
    {{0x0A, 0x15, 0x18}},  // '&'
    {{0x00, 0x03, 0x00}},  // '\''
    {{0x00, 0x0E, 0x11}},  // '('
    {{0x11, 0x0E, 0x00}},  // ')'
    {{0x0A, 0x04, 0x0A}},  // '*'
    {{0x04, 0x0E, 0x04}},  // '+'
    {{0x00, 0x10, 0x08}},  // ','
    {{0x04, 0x04, 0x04}},  // '-'
    {{0x00, 0x10, 0x00}},  // '.'
    {{0x18, 0x04, 0x03}},  // '/'
    {{0x1F, 0x11, 0x1F}},  // '0'
    {{0x12, 0x1F, 0x10}},  // '1'
    {{0x1D, 0x15, 0x17}},  // '2'
    {{0x15, 0x15, 0x1F}},  // '3'
    {{0x07, 0x04, 0x1F}},  // '4'
    {{0x17, 0x15, 0x1D}},  // '5'
    {{0x1F, 0x15, 0x1D}},  // '6'
    {{0x01, 0x01, 0x1F}},  // '7'
    {{0x1F, 0x15, 0x1F}},  // '8'
    {{0x17, 0x15, 0x1F}},  // '9'
    {{0x00, 0x0A, 0x00}},  // ':'
    {{0x00, 0x12, 0x0A}},  // ';'
    {{0x04, 0x0A, 0x11}},  // '<'
    {{0x0A, 0x0A, 0x0A}},  // '='
    {{0x11, 0x0A, 0x04}},  // '>'
    {{0x01, 0x15, 0x03}},  // '?'
    {{0x0E, 0x15, 0x16}},  // '@'
    {{0x1E, 0x05, 0x1E}},  // 'A'
    {{0x1F, 0x15, 0x0A}},  // 'B'
    {{0x0E, 0x11, 0x11}},  // 'C'
    {{0x1F, 0x11, 0x0E}},  // 'D'
    {{0x1F, 0x15, 0x11}},  // 'E'
    {{0x1F, 0x05, 0x01}},  // 'F'
    {{0x0E, 0x11, 0x1D}},  // 'G'
    {{0x1F, 0x04, 0x1F}},  // 'H'
    {{0x11, 0x1F, 0x11}},  // 'I'
    {{0x08, 0x10, 0x0F}},  // 'J'
    {{0x1F, 0x04, 0x1B}},  // 'K'
    {{0x1F, 0x10, 0x10}},  // 'L'
    {{0x1F, 0x02, 0x1F}},  // 'M'
    {{0x1F, 0x02, 0x1C}},  // 'N'
    {{0x0E, 0x11, 0x0E}},  // 'O'
    {{0x1F, 0x05, 0x02}},  // 'P'
    {{0x0E, 0x19, 0x1E}},  // 'Q'
    {{0x1F, 0x05, 0x1A}},  // 'R'
    {{0x12, 0x15, 0x09}},  // 'S'
    {{0x01, 0x1F, 0x01}},  // 'T'
    {{0x1F, 0x10, 0x1F}},  // 'U'
    {{0x0F, 0x10, 0x0F}},  // 'V'
    {{0x1F, 0x08, 0x1F}},  // 'W'
    {{0x1B, 0x04, 0x1B}},  // 'X'
    {{0x03, 0x1C, 0x03}},  // 'Y'
    {{0x19, 0x15, 0x13}},  // 'Z'
    {{0x1F, 0x11, 0x00}},  // '['
    {{0x03, 0x04, 0x18}},  // '\\'
    {{0x00, 0x11, 0x1F}},  // ']'
    {{0x02, 0x01, 0x02}},  // '^'
    {{0x10, 0x10, 0x10}},  // '_'
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

void format_uptime_short(std::uint32_t seconds, char* out, std::size_t size) noexcept {
    const unsigned days = seconds / 86400U;
    const unsigned hours = (seconds % 86400U) / 3600U;
    const unsigned mins = (seconds % 3600U) / 60U;
    if (days > 0) {
        std::snprintf(out, size, "%uD %02uH", days, hours);
    } else {
        std::snprintf(out, size, "%uH %02uM", hours, mins);
    }
}

void format_fault_csv(const OledSystemState& state, char* out, std::size_t size,
                      std::size_t max_items) noexcept {
    std::size_t pos = 0;
    std::size_t shown = 0;
    const std::size_t total_faults = state.gear_faulty.count();
    for (unsigned ch = 0; ch < 64; ++ch) {
        if (!state.gear_faulty.test(ch)) {
            continue;
        }
        if (shown >= max_items) {
            const unsigned remaining = static_cast<unsigned>(total_faults - shown);
            (void)std::snprintf(out + pos, size - pos, " +%u", remaining);
            return;
        }
        const int written = std::snprintf(out + pos, size - pos, "%sA%02u", shown > 0 ? "," : "", ch);
        if (written <= 0 || static_cast<std::size_t>(written) >= size - pos) {
            break;
        }
        pos += static_cast<std::size_t>(written);
        ++shown;
    }
    if (shown == 0 && state.last_error_code != diagnostics::ErrorCode::None) {
        std::snprintf(out, size, "E%03u", static_cast<unsigned>(state.last_error_code));
        return;
    }
    if (shown == 0) {
        std::snprintf(out, size, "NONE");
    }
}

void format_error_target(std::uint16_t addr, char* out, std::size_t size) noexcept {
    if (addr < 64U) {
        std::snprintf(out, size, "A%02u", static_cast<unsigned>(addr));
    } else {
        std::snprintf(out, size, "BUS");
    }
}

const char* short_error_meaning(diagnostics::ErrorCode code) noexcept {
    using diagnostics::ErrorCode;
    switch (code) {
    case ErrorCode::None:
        return "NO ERROR";
    case ErrorCode::DaliBusShort:
        return "DALI BUS SHORT";
    case ErrorCode::DaliNoPower:
        return "NO DALI POWER";
    case ErrorCode::DaliFrameCollision:
        return "FRAME COLLISION";
    case ErrorCode::DaliReplyTimeout:
        return "NO REPLY TIMEOUT";
    case ErrorCode::DaliInvalidReply:
        return "INVALID REPLY";
    case ErrorCode::DaliLampFailure:
        return "LAMP FAILURE";
    case ErrorCode::DaliGearFailure:
        return "BALLAST FAULT";
    case ErrorCode::DaliLimitError:
        return "ARC LIMIT ERR";
    case ErrorCode::DaliPowerCycleSeen:
        return "POWER CYCLED";
    case ErrorCode::DaliUnsupportedColour:
        return "DT8 UNSUPPORTED";
    case ErrorCode::DaliAddressingFailed:
        return "ADDR SCAN FAIL";
    case ErrorCode::KnxBusPowerLoss:
        return "KNX POWER DIP";
    case ErrorCode::KnxBusPowerRestored:
        return "KNX POWER OK";
    default:
        return diagnostics::describe_error(code).title;
    }
}

bool modal_active(const OledSystemState& state) noexcept {
    return state.hint == PanelHint::FactoryResetArming || state.dali_psu_short_tripped ||
           state.knx_prog_mode || state.wifi_state == WifiState::Setup ||
           commissioning::active(state.comm_phase);
}

// Normal-height (10 px) inverted header bar with 5x7 font, vertical divider, and dot-and-bar pagination
void draw_normal_header(OledCanvas& canvas, int width, const char* left_text, unsigned page_num,
                        const OledSystemState* status_icons = nullptr) noexcept {
    canvas.fill_rect(0, 0, width, 10, true);
    canvas.draw_text(2, 1, left_text, false);
    const int dots_w = static_cast<int>(oled_page_count) * 4 - 2;
    const int dots_x = width - dots_w - 2;
    const int div_x = dots_x - 4;
    canvas.fill_rect(div_x, 0, 2, 10, false);
    canvas.draw_page_dots(dots_x, 2, page_num, static_cast<unsigned>(oled_page_count), false);
    if (status_icons != nullptr) {
        canvas.draw_wifi_icon(div_x - 13, 2, status_icons->wifi_state, status_icons->wifi_rssi_dbm, false);
    }
}

void draw_ip_two_lines_v3(OledCanvas& canvas, const char* ip) noexcept {
    if (!ip) {
        return;
    }
    const std::size_t len = std::strlen(ip);
    if (len <= 10) {
        canvas.draw_text(2, 17, ip, true);
        return;
    }
    // Find second '.' to split cleanly across two 5x7 lines (e.g. "255.255." / "255.255")
    unsigned dots = 0;
    std::size_t split = 0;
    for (std::size_t i = 0; i < len; ++i) {
        if (ip[i] == '.') {
            ++dots;
            if (dots == 2) {
                split = i + 1;
                break;
            }
        }
    }
    if (split > 0 && split < 11) {
        char line1[12]{};
        std::memcpy(line1, ip, split);
        line1[split] = '\0';
        canvas.draw_text(2, 13, line1, true);
        canvas.draw_text(2, 22, ip + split, true);
    } else {
        char line1[11]{};
        std::memcpy(line1, ip, 10);
        canvas.draw_text(2, 13, line1, true);
        canvas.draw_text(2, 22, ip + 10, true);
    }
}

void draw_lamp_cell(OledCanvas& canvas, int x, int y, int w, int h, unsigned ch, const OledSystemState& state,
                    std::uint32_t now_ms) noexcept {
    if (state.lamp_finder_active && ch == state.lamp_finder_channel) {
        const unsigned phase = (now_ms / 200U) % 4U;
        if (phase == 0U) {
            canvas.fill_rect(x - 1, y - 1, w + 2, h + 2, true);
        } else if (phase == 1U || phase == 3U) {
            canvas.draw_rect(x - 1, y - 1, w + 2, h + 2, true);
            canvas.fill_rect(x + 1, y + 1, w - 2, h - 2, true);
        } else {
            canvas.draw_rect(x, y, w, h, true);
        }
        return;
    }
    if (state.gear_faulty.test(ch)) {
        canvas.draw_rect(x, y, w, h, true);
        const int inner_w = w - 2;
        const int inner_h = h - 2;
        for (int dy = 0; dy < inner_h; ++dy) {
            const int dx = (dy * (inner_w - 1) + (inner_h - 1) / 2) / (inner_h > 1 ? inner_h - 1 : 1);
            canvas.set_pixel(x + 1 + dx, y + 1 + dy, true);
            canvas.set_pixel(x + w - 2 - dx, y + 1 + dy, true);
        }
    } else if (state.gear_on.test(ch)) {
        canvas.fill_rect(x, y, w, h, true);
    } else if (state.gear_present.test(ch)) {
        canvas.draw_rect(x, y, w, h, true);
    } else {
        canvas.set_pixel(x + w / 2, y + h / 2, true);
    }
}

class Oled128x64Strategy final : public OledLayoutStrategy {
public:
    [[nodiscard]] OledGeometry geometry() const noexcept override { return OledGeometry::Oled128x64; }
    [[nodiscard]] std::uint8_t height() const noexcept override { return 64; }
    [[nodiscard]] std::uint8_t ssd1306_mux_ratio() const noexcept override { return 0x3FU; }
    [[nodiscard]] std::uint8_t ssd1306_com_pins() const noexcept override { return 0x12U; }

    void render_modal(OledCanvas& canvas, const OledSystemState& state) const noexcept override {
        canvas.draw_rect(0, 0, 128, 64, true);
        canvas.fill_rect(1, 1, 126, 11, true);
        if (state.hint == PanelHint::FactoryResetArming) {
            canvas.draw_text(6, 3, "!! FACTORY RESET !!", false);
            canvas.draw_text(6, 18, "HOLD RST+FUNC 10S", true);
            canvas.draw_text_medium(6, 32, "ERASES KNX & NVS", true);
            canvas.draw_text_medium(6, 44, "RELEASE TO ABORT", true);
            return;
        }
        if (state.dali_psu_short_tripped) {
            canvas.draw_text(6, 3, "DALI BUS SHORT!", false);
            canvas.draw_text(6, 18, "PSU LATCHED OFF", true);
            canvas.draw_text_medium(6, 32, "FIX DA+/DA- WIRING", true);
            canvas.draw_text_medium(6, 44, "RESET IN WEBUI / RST", true);
            return;
        }
        if (state.knx_prog_mode) {
            canvas.draw_text(8, 3, "KNX PROG MODE", false);
            char ia[12]{};
            format_ia(state.knx_individual_address, ia, sizeof(ia));
            char line[32]{};
            std::snprintf(line, sizeof(line), "KNX: %s", ia);
            canvas.draw_text(6, 18, line, true);
            canvas.draw_text_medium(6, 32, "ETS DOWNLOAD READY", true);
            canvas.draw_text_medium(6, 44, "HOLD RST 5S TO EXIT", true);
            return;
        }
        if (state.wifi_state == WifiState::Setup) {
            canvas.draw_text(8, 3, "WIFI SETUP AP", false);
            canvas.draw_text_medium(6, 17, "SSID:", true);
            char ssid[36]{};
            std::snprintf(ssid, sizeof(ssid), "KNX-DALI-SETUP-%.7s", state.chip_id.data());
            canvas.draw_text_medium(6, 26, ssid, true);
            canvas.draw_text(6, 37, "192.168.4.1", true);
            canvas.draw_text_medium(6, 51, "HOLD FUNC 5S: EXIT", true);
            return;
        }
        canvas.draw_text(8, 3, "DALI BUS SCAN", false);
        char line[32]{};
        std::snprintf(line, sizeof(line), "DONE: %u%%  FOUND: %u", static_cast<unsigned>(state.comm_progress),
                      static_cast<unsigned>(state.comm_found));
        canvas.draw_text_medium(6, 18, line, true);
        canvas.draw_rect(6, 32, 116, 12, true);
        canvas.fill_rect(8, 34, (112 * static_cast<int>(state.comm_progress)) / 100, 8, true);
    }

    void render_page(OledCanvas& canvas, OledPage page, const OledSystemState& state,
                     std::uint32_t now_ms) const noexcept override {
        switch (page) {
        case OledPage::Overview: {
            draw_normal_header(canvas, 128, "STATUS", 1, &state);

            // Row 1: IPv4 address (5x7) when connected, or inline prompt to hold FUNC 5s
            if (state.wifi_state == WifiState::Connected) {
                canvas.draw_text(3, 13, state.ipv4.data(), true);
            } else {
                canvas.draw_text_medium(3, 14, "NO WIFI: HOLD FUNC 5S", true);
            }

            // Two-column compact checkbox grid
            const bool dali_bus_ok = state.dali_bus_ok && !state.dali_psu_short_tripped;
            const bool dali_psu_ok = state.dali_psu_enabled && !state.dali_psu_short_tripped;

            canvas.draw_text_medium(3, 25, "KNX BUS", true);
            canvas.draw_checkbox(46, 24, state.knx_bus_ok, true);
            canvas.draw_text_medium(65, 25, "DALI BUS", true);
            canvas.draw_checkbox(114, 24, dali_bus_ok, true);

            canvas.draw_text_medium(3, 35, "DALI PSU", true);
            canvas.draw_checkbox(46, 34, dali_psu_ok, true);
            canvas.draw_text_medium(65, 35, "MQTT", true);
            canvas.draw_checkbox(114, 34, state.mqtt_connected, true);

            // Footer: ERR (x): comma-separated list with +N overflow
            char flt[28]{};
            format_fault_csv(state, flt, sizeof(flt), 3);
            const unsigned err_cnt = state.faulty_gear_count > 0
                                         ? static_cast<unsigned>(state.faulty_gear_count)
                                         : (state.last_error_code != diagnostics::ErrorCode::None ? 1U : 0U);
            char line[40]{};
            if (err_cnt >= 10) {
                std::snprintf(line, sizeof(line), "ERR(%u): %.18s", err_cnt, flt);
            } else {
                std::snprintf(line, sizeof(line), "ERR (%u): %.18s", err_cnt, flt);
            }
            if (err_cnt > 0) {
                canvas.fill_rect(0, 52, 128, 12, true);
                canvas.draw_text_medium(3, 55, line, false);
            } else {
                canvas.draw_rect(0, 52, 128, 12, true);
                canvas.draw_text_medium(3, 55, line, true);
            }
            break;
        }
        case OledPage::DaliMatrix: {
            char hdr[24]{};
            if (state.lamp_finder_active) {
                std::snprintf(hdr, sizeof(hdr), "FIND A%02u (%u/%u)",
                              static_cast<unsigned>(state.lamp_finder_channel),
                              static_cast<unsigned>(state.active_lamp_count),
                              static_cast<unsigned>(state.present_gear_count));
            } else {
                std::snprintf(hdr, sizeof(hdr), "BALLASTS %u/%u",
                              static_cast<unsigned>(state.active_lamp_count),
                              static_cast<unsigned>(state.present_gear_count));
            }
            draw_normal_header(canvas, 128, hdr, 2);

            const char* rlbl[4] = {"00", "16", "32", "48"};
            for (int r = 0; r < 4; ++r) {
                canvas.draw_text_medium(1, 14 + r * 13, rlbl[r]);
            }
            for (unsigned ch = 0; ch < 64; ++ch) {
                const int col = static_cast<int>(ch % 16);
                const int row = static_cast<int>(ch / 16);
                const int x = 13 + col * 7;
                const int y = 12 + row * 13;
                draw_lamp_cell(canvas, x, y, 6, 11, ch, state, now_ms);
            }
            break;
        }
        case OledPage::Errors: {
            char hdr[24]{};
            std::snprintf(hdr, sizeof(hdr), "ERRORS (%lu)", static_cast<unsigned long>(state.logged_errors));
            draw_normal_header(canvas, 128, hdr, 3);

            if (state.recent_error_count == 0) {
                canvas.draw_text_medium(4, 28, "NO LOGGED ERRORS.", true);
                return;
            }
            const unsigned rows = state.recent_error_count < 8U ? state.recent_error_count : 8U;
            for (unsigned i = 0; i < rows; ++i) {
                const auto& item = state.recent_errors[i];
                char tgt[6]{};
                format_error_target(item.address, tgt, sizeof(tgt));
                char line[48]{};
                std::snprintf(line, sizeof(line), "%-3s E%03u %.21s", tgt, static_cast<unsigned>(item.code),
                              short_error_meaning(item.code));
                canvas.draw_text_small(2, 12 + static_cast<int>(i) * 6, line, true);
            }
            break;
        }
        case OledPage::DeviceInfo: {
            draw_normal_header(canvas, 128, "INFO", 4);
            char ia[12]{};
            format_ia(state.knx_individual_address, ia, sizeof(ia));
            char up[16]{};
            format_uptime_short(state.uptime_seconds, up, sizeof(up));
            char line[32]{};

            std::snprintf(line, sizeof(line), "KNX: %s", ia);
            canvas.draw_text_medium(3, 15, line, true);
            std::snprintf(line, sizeof(line), "ON: %u/%u", static_cast<unsigned>(state.active_lamp_count),
                          static_cast<unsigned>(state.present_gear_count));
            canvas.draw_text_medium_right(125, 15, line, true);

            std::snprintf(line, sizeof(line), "PWR: %uW", static_cast<unsigned>(state.lamp_power_watts));
            canvas.draw_text_medium(3, 27, line, true);
            std::snprintf(line, sizeof(line), "BUS: %umA", static_cast<unsigned>(state.bus_current_ma));
            canvas.draw_text_medium_right(125, 27, line, true);

            std::snprintf(line, sizeof(line), "UP:  %s", up);
            canvas.draw_text_medium(3, 39, line, true);
            break;
        }
        }
    }
};

class Oled64x128Strategy final : public OledLayoutStrategy {
public:
    [[nodiscard]] OledGeometry geometry() const noexcept override { return OledGeometry::Oled64x128; }
    [[nodiscard]] std::uint8_t width() const noexcept override { return 64; }
    [[nodiscard]] std::uint8_t height() const noexcept override { return 128; }
    [[nodiscard]] std::uint8_t ssd1306_mux_ratio() const noexcept override { return 0x7FU; }
    [[nodiscard]] std::uint8_t ssd1306_com_pins() const noexcept override { return 0x12U; }

    void render_modal(OledCanvas& canvas, const OledSystemState& state) const noexcept override {
        canvas.draw_rect(0, 0, 64, 128, true);
        canvas.fill_rect(1, 1, 62, 11, true);
        if (state.hint == PanelHint::FactoryResetArming) {
            canvas.draw_text(4, 3, "! RESET !", false);
            canvas.draw_text(4, 20, "HOLD 10S", true);
            canvas.draw_text_medium(4, 36, "WIPE NVS", true);
            canvas.draw_text_medium(4, 52, "RELEASE TO", true);
            canvas.draw_text_medium(4, 62, "ABORT", true);
            return;
        }
        if (state.dali_psu_short_tripped) {
            canvas.draw_text(4, 3, "BUS SHORT", false);
            canvas.draw_text(4, 20, "PSU OFF!", true);
            canvas.draw_text_medium(4, 36, "FIX DA+/DA-", true);
            canvas.draw_text_medium(4, 46, "WIRING", true);
            canvas.draw_text_medium(4, 64, "RESET IN UI", true);
            canvas.draw_text_medium(4, 74, "OR TAP RST", true);
            return;
        }
        if (state.knx_prog_mode) {
            canvas.draw_text(4, 3, "KNX PROG", false);
            char ia[12]{};
            format_ia(state.knx_individual_address, ia, sizeof(ia));
            char line[24]{};
            std::snprintf(line, sizeof(line), "KNX:%s", ia);
            canvas.draw_text_medium(4, 18, line, true);
            canvas.draw_text(4, 34, "ETS READY", true);
            canvas.draw_text_medium(4, 54, "HOLD RST 5S", true);
            canvas.draw_text_medium(4, 64, "TO EXIT", true);
            return;
        }
        if (state.wifi_state == WifiState::Setup) {
            canvas.draw_text(4, 3, "WIFI AP", false);
            canvas.draw_text_medium(4, 18, "SSID:", true);
            canvas.draw_text_medium(4, 28, "KNX-DALI-", true);
            canvas.draw_text_medium(4, 37, "SETUP-", true);
            char tail[16]{};
            std::snprintf(tail, sizeof(tail), "%.7s", state.chip_id.data());
            canvas.draw_text_medium(4, 46, tail, true);
            canvas.draw_text_medium(4, 62, "WEB URL:", true);
            canvas.draw_text(4, 72, "192.168.", true);
            canvas.draw_text(4, 82, "4.1", true);
            canvas.draw_text_medium(4, 102, "HOLD FUNC", true);
            canvas.draw_text_medium(4, 112, "5S: EXIT", true);
            return;
        }
        canvas.draw_text(4, 3, "DALI SCAN", false);
        char line[20]{};
        std::snprintf(line, sizeof(line), "DONE: %u%%", static_cast<unsigned>(state.comm_progress));
        canvas.draw_text_medium(4, 20, line, true);
        std::snprintf(line, sizeof(line), "FOUND: %u", static_cast<unsigned>(state.comm_found));
        canvas.draw_text_medium(4, 32, line, true);
        canvas.draw_rect(4, 46, 56, 12, true);
        canvas.fill_rect(6, 48, (52 * static_cast<int>(state.comm_progress)) / 100, 8, true);
    }

    void render_page(OledCanvas& canvas, OledPage page, const OledSystemState& state,
                     std::uint32_t now_ms) const noexcept override {
        switch (page) {
        case OledPage::Overview: {
            draw_normal_header(canvas, 64, "STAT", 1, &state);

            // 2-line reserved IP area (y=13..30)
            if (state.wifi_state == WifiState::Connected) {
                draw_ip_two_lines_v3(canvas, state.ipv4.data());
            } else {
                canvas.draw_text_medium(2, 14, "NO WIFI", true);
                canvas.draw_text_medium(2, 23, "HOLD FUNC 5S", true);
            }

            canvas.fill_rect(0, 32, 64, 1, true);

            const bool dali_bus_ok = state.dali_bus_ok && !state.dali_psu_short_tripped;
            const bool dali_psu_ok = state.dali_psu_enabled && !state.dali_psu_short_tripped;

            canvas.draw_text_medium(2, 37, "KNX BUS", true);
            canvas.draw_checkbox(50, 36, state.knx_bus_ok, true);
            canvas.draw_text_medium(2, 46, "DALI BUS", true);
            canvas.draw_checkbox(50, 45, dali_bus_ok, true);
            canvas.draw_text_medium(2, 55, "DALI PSU", true);
            canvas.draw_checkbox(50, 54, dali_psu_ok, true);
            canvas.draw_text_medium(2, 64, "MQTT", true);
            canvas.draw_checkbox(50, 63, state.mqtt_connected, true);

            char flt[20]{};
            format_fault_csv(state, flt, sizeof(flt), 2);
            const unsigned err_cnt = state.faulty_gear_count > 0
                                         ? static_cast<unsigned>(state.faulty_gear_count)
                                         : (state.last_error_code != diagnostics::ErrorCode::None ? 1U : 0U);
            char line[24]{};
            std::snprintf(line, sizeof(line), "ERR (%u):", err_cnt);
            if (err_cnt > 0) {
                canvas.fill_rect(0, 106, 64, 22, true);
                canvas.draw_text_medium(3, 109, line, false);
                canvas.draw_text_medium(3, 118, flt, false);
            } else {
                canvas.draw_rect(0, 106, 64, 22, true);
                canvas.draw_text_medium(3, 109, "ERR (0):", true);
                canvas.draw_text_medium(3, 118, "NONE", true);
            }
            break;
        }
        case OledPage::DaliMatrix: {
            char hdr[16]{};
            if (state.lamp_finder_active) {
                std::snprintf(hdr, sizeof(hdr), "FIND%02u", static_cast<unsigned>(state.lamp_finder_channel));
            } else {
                std::snprintf(hdr, sizeof(hdr), "%u/%u", static_cast<unsigned>(state.active_lamp_count),
                              static_cast<unsigned>(state.present_gear_count));
            }
            draw_normal_header(canvas, 64, hdr, 2);

            for (unsigned ch = 0; ch < 64; ++ch) {
                const int col = static_cast<int>(ch % 8);
                const int row = static_cast<int>(ch / 8);
                const int x = 12 + col * 6;
                const int y = 12 + row * 14;
                if (col == 0) {
                    char rlbl[4]{};
                    std::snprintf(rlbl, sizeof(rlbl), "%02u", row * 8U);
                    canvas.draw_text_medium(1, y + 3, rlbl);
                }
                draw_lamp_cell(canvas, x, y, 5, 12, ch, state, now_ms);
            }
            break;
        }
        case OledPage::Errors: {
            char hdr[16]{};
            std::snprintf(hdr, sizeof(hdr), "ERR(%lu)", static_cast<unsigned long>(state.logged_errors));
            draw_normal_header(canvas, 64, hdr, 3);

            if (state.recent_error_count == 0) {
                canvas.draw_text_medium(3, 26, "NO ERRORS.", true);
                return;
            }
            const unsigned rows = state.recent_error_count < 9U ? state.recent_error_count : 9U;
            for (unsigned i = 0; i < rows; ++i) {
                const auto& item = state.recent_errors[i];
                char tgt[6]{};
                format_error_target(item.address, tgt, sizeof(tgt));
                char line[32]{};
                std::snprintf(line, sizeof(line), "%s E%03u %.7s", tgt, static_cast<unsigned>(item.code),
                              short_error_meaning(item.code));
                canvas.draw_text_small(1, 12 + static_cast<int>(i) * 13, line, true);
                const char* meaning = short_error_meaning(item.code);
                if (std::strlen(meaning) > 7) {
                    std::snprintf(line, sizeof(line), "  %.13s", meaning + 7);
                    canvas.draw_text_small(1, 18 + static_cast<int>(i) * 13, line, true);
                }
            }
            break;
        }
        case OledPage::DeviceInfo: {
            draw_normal_header(canvas, 64, "INFO", 4);
            char ia[12]{};
            format_ia(state.knx_individual_address, ia, sizeof(ia));
            char up[16]{};
            format_uptime_short(state.uptime_seconds, up, sizeof(up));
            char line[24]{};

            std::snprintf(line, sizeof(line), "KNX:%s", ia);
            canvas.draw_text_medium(2, 15, line, true);
            std::snprintf(line, sizeof(line), "ON:  %u/%u", static_cast<unsigned>(state.active_lamp_count),
                          static_cast<unsigned>(state.present_gear_count));
            canvas.draw_text_medium(2, 27, line, true);
            std::snprintf(line, sizeof(line), "PWR: %uW", static_cast<unsigned>(state.lamp_power_watts));
            canvas.draw_text_medium(2, 39, line, true);
            std::snprintf(line, sizeof(line), "BUS: %umA", static_cast<unsigned>(state.bus_current_ma));
            canvas.draw_text_medium(2, 51, line, true);
            std::snprintf(line, sizeof(line), "UP:  %s", up);
            canvas.draw_text_medium(2, 63, line, true);
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
    void render_page(OledCanvas&, OledPage, const OledSystemState&, std::uint32_t) const noexcept override {}
};

const Oled128x64Strategy strategy_64{};
const Oled64x128Strategy strategy_64x128{};
const DisabledOledStrategy strategy_disabled{};

}  // namespace

const OledLayoutStrategy& select_oled_strategy(OledGeometry geometry) noexcept {
    switch (geometry) {
    case OledGeometry::Oled128x64:
        return strategy_64;
    case OledGeometry::Oled64x128:
    case OledGeometry::Auto:
        return strategy_64x128;
    case OledGeometry::Disabled:
        return strategy_disabled;
    }
    return strategy_64x128;
}

void OledCanvas::reset(std::uint8_t width, std::uint8_t height) noexcept {
    width_ = width;
    height_ = height;
    buffer_.fill(0);
}

void OledCanvas::set_pixel(int x, int y, bool on) noexcept {
    if (x < 0 || x >= static_cast<int>(width_) || y < 0 || y >= static_cast<int>(height_)) {
        return;
    }
    const auto index =
        static_cast<std::size_t>(y / 8) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x);
    const auto mask = static_cast<std::uint8_t>(1U << (y & 7));
    if (on) {
        buffer_[index] = static_cast<std::uint8_t>(buffer_[index] | mask);
    } else {
        buffer_[index] = static_cast<std::uint8_t>(buffer_[index] & ~mask);
    }
}

bool OledCanvas::pixel(std::uint8_t x, std::uint8_t y) const noexcept {
    if (x >= width_ || y >= height_) {
        return false;
    }
    const auto index = static_cast<std::size_t>(y / 8U) * static_cast<std::size_t>(width_) + x;
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
        if (cursor + 5 > static_cast<int>(width_)) {
            break;
        }
        draw_char(cursor, y, *p, on);
        cursor += 6;
    }
}

int OledCanvas::draw_char_medium(int x, int y, char c, bool on) noexcept {
    const auto& glyph = lookup_medium_glyph(c);
    const int gw = static_cast<int>(glyph.width);
    for (int col = 0; col < gw; ++col) {
        const std::uint8_t bits = glyph.cols[static_cast<std::size_t>(col)];
        for (int row = 0; row < 6; ++row) {
            if ((bits & (1U << row)) != 0) {
                set_pixel(x + col, y + row, on);
            }
        }
    }
    return gw + 1;
}

void OledCanvas::draw_text_medium(int x, int y, const char* text, bool on) noexcept {
    if (!text) {
        return;
    }
    int cursor = x;
    for (const char* p = text; *p != '\0'; ++p) {
        const auto& glyph = lookup_medium_glyph(*p);
        if (cursor + static_cast<int>(glyph.width) > static_cast<int>(width_)) {
            break;
        }
        cursor += draw_char_medium(cursor, y, *p, on);
    }
}

int OledCanvas::measure_text_medium(const char* text) const noexcept {
    if (!text || *text == '\0') {
        return 0;
    }
    int total = 0;
    for (const char* p = text; *p != '\0'; ++p) {
        const auto& glyph = lookup_medium_glyph(*p);
        total += static_cast<int>(glyph.width) + (*(p + 1) != '\0' ? 1 : 0);
    }
    return total;
}

void OledCanvas::draw_text_medium_right(int right_x, int y, const char* text, bool on) noexcept {
    const int w = measure_text_medium(text);
    const int x = right_x > w ? (right_x - w) : 0;
    draw_text_medium(x, y, text, on);
}

void OledCanvas::draw_char_small(int x, int y, char c, bool on) noexcept {
    auto uc = static_cast<unsigned char>(c);
    if (uc >= 'a' && uc <= 'z') {
        uc = static_cast<unsigned char>(uc - ('a' - 'A'));
    }
    if (uc < 32 || uc > 95) {
        uc = '?';
    }
    const auto& glyph = font_3x5[uc - 32];
    for (int col = 0; col < 3; ++col) {
        const std::uint8_t bits = glyph[static_cast<std::size_t>(col)];
        for (int row = 0; row < 5; ++row) {
            if ((bits & (1U << row)) != 0) {
                set_pixel(x + col, y + row, on);
            }
        }
    }
}

void OledCanvas::draw_text_small(int x, int y, const char* text, bool on) noexcept {
    if (!text) {
        return;
    }
    int cursor = x;
    for (const char* p = text; *p != '\0'; ++p) {
        if (cursor + 3 > static_cast<int>(width_)) {
            break;
        }
        draw_char_small(cursor, y, *p, on);
        cursor += 4;
    }
}

void OledCanvas::draw_wifi_icon(int x, int y, WifiState state, std::int8_t rssi_dbm, bool on) noexcept {
    if (state != WifiState::Connected && state != WifiState::Setup) {
        for (int i = 0; i < 5; ++i) {
            set_pixel(x + i, y + 1 + i, on);
            set_pixel(x + 4 - i, y + 1 + i, on);
        }
        for (int b = 1; b < 4; ++b) {
            set_pixel(x + 3 + b * 2, y + 5, on);
        }
        return;
    }
    const int bars = (state == WifiState::Setup || rssi_dbm >= -55)
                         ? 4
                         : (rssi_dbm >= -67 ? 3 : (rssi_dbm >= -78 ? 2 : 1));
    for (int b = 0; b < 4; ++b) {
        const int bh = (b + 1) * 2 - 1;
        const int bx = x + b * 3;
        const int by = y + (6 - bh);
        if (b < bars) {
            fill_rect(bx, by, 2, bh, on);
        } else {
            fill_rect(bx, y + 5, 2, 1, on);
        }
    }
}

void OledCanvas::draw_checkbox(int x, int y, bool checked, bool on) noexcept {
    draw_rect(x, y, 7, 7, on);
    if (checked) {
        set_pixel(x + 1, y + 3, on);
        set_pixel(x + 2, y + 4, on);
        set_pixel(x + 3, y + 5, on);
        set_pixel(x + 4, y + 3, on);
        set_pixel(x + 5, y + 2, on);
    }
}

void OledCanvas::draw_page_dots(int x, int y, unsigned page_num, unsigned total_pages, bool on) noexcept {
    int cur = x;
    for (unsigned idx = 1; idx <= total_pages; ++idx) {
        if (idx == page_num) {
            fill_rect(cur, y, 2, 6, on);
        } else {
            fill_rect(cur, y + 2, 2, 2, on);
        }
        cur += 4;
    }
}

OledDisplay::OledDisplay(OledGeometry geometry) noexcept : strategy_(&select_oled_strategy(geometry)) {}

void OledDisplay::set_geometry(OledGeometry geometry) noexcept {
    strategy_ = &select_oled_strategy(geometry);
}

void OledDisplay::next_page(std::uint32_t now_ms) noexcept {
    if (sleeping(now_ms)) {
        page_ = OledPage::Overview;
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
    canvas_.reset(strategy_->width(), strategy_->height());
    if (strategy_->geometry() == OledGeometry::Disabled) {
        return;
    }
    const bool new_fault = (state.gear_faulty & ~prev_faulty_).any();
    const bool new_err =
        state.last_error_code != diagnostics::ErrorCode::None && state.last_error_code != prev_error_code_;
    prev_faulty_ = state.gear_faulty;
    prev_error_code_ = state.last_error_code;
    if (new_fault || new_err || !state.knx_bus_ok) {
        page_ = OledPage::Overview;
        wake(now_ms);
    }
    if (state.lamp_finder_active) {
        page_ = OledPage::DaliMatrix;
        wake(now_ms);
    }
    if (modal_active(state)) {
        wake(now_ms);
        strategy_->render_modal(canvas_, state);
        return;
    }
    if (sleeping(now_ms)) {
        return;
    }
    strategy_->render_page(canvas_, page_, state, now_ms);
}

}  // namespace gateway::device
