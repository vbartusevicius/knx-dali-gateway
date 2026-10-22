"""Compile OledDisplay.cpp on host and render pixel-accurate SVG previews of all 128x64 and 64x128 OLED screens."""

from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT_SVG = ROOT / "docs" / "oled-previews.svg"

HARNESS_CPP = r"""
#include <cstdio>
#include "device/OledDisplay.hpp"

using namespace gateway::device;
using namespace gateway::diagnostics;

int main() {
    OledSystemState st{};
    st.knx_individual_address = (15U << 12U) | (15U << 8U) | 255U; // 15.15.255 max KNX IA
    st.knx_prog_mode = false;
    st.knx_bus_ok = true;
    st.dali_bus_ok = true;
    st.dali_psu_enabled = true;
    st.present_gear_count = 27;
    st.active_lamp_count = 4;
    st.faulty_gear_count = 15;
    st.lamp_power_watts = 58;
    for (unsigned i = 0; i < 27; ++i) {
        st.gear_present.set(i);
    }
    st.gear_on.set(0);
    st.gear_on.set(2);
    st.gear_on.set(5);
    st.gear_on.set(11);
    const unsigned fault_addrs[15] = {0, 7, 14, 16, 18, 20, 21, 22, 24, 26, 30, 35, 42, 50, 55};
    for (unsigned f : fault_addrs) {
        st.gear_faulty.set(f);
    }
    st.wifi_state = WifiState::Connected;
    st.wifi_rssi_dbm = -58;
    std::snprintf(st.ipv4.data(), st.ipv4.size(), "255.255.255.255");
    std::snprintf(st.chip_id.data(), st.chip_id.size(), "f5aa11f");
    st.mqtt_connected = true;
    st.logged_errors = 15;
    st.last_error_code = ErrorCode::DaliLampFailure;
    st.last_error_address = 7;
    st.recent_error_count = 9;
    st.recent_errors[0] = {ErrorCode::DaliLampFailure, 7};
    st.recent_errors[1] = {ErrorCode::DaliGearFailure, 14};
    st.recent_errors[2] = {ErrorCode::DaliReplyTimeout, 3};
    st.recent_errors[3] = {ErrorCode::DaliFrameCollision, 0xFFFF};
    st.recent_errors[4] = {ErrorCode::DaliLampFailure, 21};
    st.recent_errors[5] = {ErrorCode::KnxBusPowerLoss, 0xFFFF};
    st.recent_errors[6] = {ErrorCode::DaliLampFailure, 30};
    st.recent_errors[7] = {ErrorCode::DaliGearFailure, 42};
    st.recent_errors[8] = {ErrorCode::DaliReplyTimeout, 55};
    st.uptime_seconds = 14 * 86400 + 6 * 3600 + 22 * 60;

    auto dump = [&](const char* id, OledGeometry geom, OledPage page, const OledSystemState& s, std::uint32_t t = 100) {
        OledDisplay disp(geom);
        disp.render(s, t);
        if (!s.lamp_finder_active) {
            disp.set_page(page, t);
        }
        disp.render(s, t);
        std::printf("SCREEN %s %u %u\n", id, disp.width(), disp.height());
        for (unsigned y = 0; y < disp.height(); ++y) {
            for (unsigned x = 0; x < disp.width(); ++x) {
                std::putchar(disp.pixel(x, y) ? '#' : '.');
            }
            std::putchar('\n');
        }
    };

    OledSystemState map_st = st;
    map_st.gear_faulty.reset();
    map_st.gear_faulty.set(7);
    map_st.gear_faulty.set(14);
    map_st.faulty_gear_count = 2;

    dump("64_overview", OledGeometry::Oled128x64, OledPage::Overview, st);
    dump("64p_overview", OledGeometry::Oled64x128, OledPage::Overview, st);
    dump("64_matrix", OledGeometry::Oled128x64, OledPage::DaliMatrix, map_st);
    dump("64p_matrix", OledGeometry::Oled64x128, OledPage::DaliMatrix, map_st);
    dump("64_errors", OledGeometry::Oled128x64, OledPage::Errors, st);
    dump("64p_errors", OledGeometry::Oled64x128, OledPage::Errors, st);
    dump("64_info", OledGeometry::Oled128x64, OledPage::DeviceInfo, st);
    dump("64p_info", OledGeometry::Oled64x128, OledPage::DeviceInfo, st);

    OledSystemState no_wifi = map_st;
    no_wifi.wifi_state = WifiState::Off;
    no_wifi.mqtt_connected = false;
    no_wifi.gear_faulty.reset();
    no_wifi.faulty_gear_count = 0;
    no_wifi.last_error_code = ErrorCode::None;
    dump("64_wifi_idle", OledGeometry::Oled128x64, OledPage::Overview, no_wifi);
    dump("64p_wifi_idle", OledGeometry::Oled64x128, OledPage::Overview, no_wifi);

    OledSystemState wifi_ap = st;
    wifi_ap.wifi_state = WifiState::Setup;
    dump("64_modal_wifi", OledGeometry::Oled128x64, OledPage::Overview, wifi_ap);
    dump("64p_modal_wifi", OledGeometry::Oled64x128, OledPage::Overview, wifi_ap);

    OledSystemState prog = st;
    prog.knx_prog_mode = true;
    dump("64_modal_prog", OledGeometry::Oled128x64, OledPage::Overview, prog);
    dump("64p_modal_prog", OledGeometry::Oled64x128, OledPage::Overview, prog);

    OledSystemState shorted = st;
    shorted.dali_psu_short_tripped = true;
    dump("64_modal_short", OledGeometry::Oled128x64, OledPage::Overview, shorted);
    dump("64p_modal_short", OledGeometry::Oled64x128, OledPage::Overview, shorted);
    return 0;
}
"""


def main() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        harness = tmp_path / "harness.cpp"
        binary = tmp_path / "harness"
        harness.write_text(HARNESS_CPP)
        subprocess.run(
            [
                "c++",
                "-std=c++17",
                f"-I{ROOT / 'src'}",
                str(harness),
                str(ROOT / "src/device/OledDisplay.cpp"),
                str(ROOT / "src/diagnostics/ErrorCode.cpp"),
                "-o",
                str(binary),
            ],
            check=True,
        )
        raw = subprocess.check_output([str(binary)], text=True)

    screens: dict[str, tuple[int, int, list[str]]] = {}
    lines = raw.splitlines()
    idx = 0
    while idx < len(lines):
        if lines[idx].startswith("SCREEN "):
            _, name, w_s, h_s = lines[idx].split()
            w, h = int(w_s), int(h_s)
            rows = lines[idx + 1 : idx + 1 + h]
            screens[name] = (w, h, rows)
            idx += 1 + h
        else:
            idx += 1

    titles = [
        ("overview", "Page 1 — WiFi Connected (Max IP 255.255.255.255 + 15 Errors)"),
        ("wifi_idle", "Page 1 — No WiFi Connected (Inline 'NO WIFI: HOLD FUNC 5S' + 0 Errors)"),
        ("matrix", "Page 2 — Full 64-Ballast Map (Solid=ON, Box=OFF, [X]=ERR, Dot=FREE)"),
        ("errors", "Page 3 — Recent Error Log (5x7 Header + Dense 3x5 Body, Newest First)"),
        ("info", "Page 4 — Device Info (KNX, ON, PWR W, BUS mA, Uptime)"),
        ("modal_wifi", "Auto-Wake Modal — WiFi Setup Portal Active (After Holding FUNC 5s)"),
        ("modal_prog", "Auto-Wake Modal — KNX Programming Mode (Max KNX: 15.15.255)"),
        ("modal_short", "Auto-Wake Modal — DALI Bus Short Latched Off"),
    ]

    scale = 3
    px_size = 3
    w128 = 128 * scale
    w64 = 64 * scale
    pair_w = w128 + 24 + w64 + 40
    total_w = 28 + pair_w * 2
    row_h = 128 * scale + 56
    total_h = 80 + 4 * row_h

    svg = [
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {total_w} {total_h}" width="{total_w}" height="{total_h}" shape-rendering="crispEdges">',
        '<rect width="100%" height="100%" fill="#0f141c"/>',
        (
            '<text x="28" y="30" fill="#e6edf3" font-family="monospace" font-size="16" font-weight="bold">'
            'KNX-DALI Gateway 0.96" OLED Screens — V2 (128x64 Landscape) vs V3 (64x128 Portrait)</text>'
        ),
        (
            '<text x="28" y="52" fill="#8b949e" font-family="monospace" font-size="12">'
            'Compiled from gateway/src/device/OledDisplay.cpp | Dot-and-Bar Pagination | 2-Line IP on V3 | Right-Aligned Info Col 2</text>'
        ),
    ]

    for idx_t, (key, label) in enumerate(titles):
        grid_col = idx_t % 2
        grid_row = idx_t // 2
        base_x = 28 + grid_col * pair_w
        base_y = 74 + grid_row * row_h
        svg.append(
            f'<text x="{base_x}" y="{base_y + 14}" fill="#c9d1d9" font-family="monospace" font-size="11.5" font-weight="bold">{label}</text>'
        )
        for prefix, dx, color in [("64", 0, "#38bdf8"), ("64p", w128 + 24, "#4ade80")]:
            w, h, rows = screens[f"{prefix}_{key}"]
            bx = base_x + dx
            by = base_y + 26 + ((128 - h) * scale) // 2
            svg.append(
                f'<rect x="{bx - 4}" y="{by - 4}" width="{w * scale + 8}" height="{h * scale + 8}" rx="4" fill="#05070a" stroke="#30363d" stroke-width="2"/>'
            )
            for py, r in enumerate(rows):
                for px, ch in enumerate(r):
                    if ch == "#":
                        svg.append(
                            f'<rect x="{bx + px * scale}" y="{by + py * scale}" width="{px_size}" height="{px_size}" fill="{color}"/>'
                        )

    svg.append("</svg>")
    OUT_SVG.write_text("\n".join(svg))
    print(f"Wrote {OUT_SVG}")

    # Also write dedicated V2 (128x64 landscape) and V3 (64x128 portrait) showcase strips for README.md
    strip_items = [
        ("overview", "1. Status"),
        ("matrix", "2. 64-Lamp Map"),
        ("errors", "3. Error Log"),
        ("info", "4. Device Info"),
        ("modal_wifi", "WiFi Setup AP"),
        ("modal_short", "DALI Bus Short"),
    ]

    # V2 (128x64 landscape, 3x2 grid)
    v2_svg_path = ROOT / "docs" / "oled-v2-landscape.svg"
    v2_cell_w = 128 * scale + 28
    v2_cell_h = 64 * scale + 48
    v2_w = 24 + 3 * v2_cell_w
    v2_h = 54 + 2 * v2_cell_h
    v2 = [
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {v2_w} {v2_h}" width="{v2_w}" height="{v2_h}" shape-rendering="crispEdges">',
        '<rect width="100%" height="100%" rx="8" fill="#0d1117"/>',
        '<text x="24" y="30" fill="#e6edf3" font-family="monospace" font-size="14" font-weight="bold">V2 Front Display — 0.96" 128×64 Landscape OLED (SSD1306)</text>',
    ]
    for i, (key, lbl) in enumerate(strip_items):
        col, row = i % 3, i // 3
        bx = 24 + col * v2_cell_w
        by = 68 + row * v2_cell_h
        w, h, rows = screens[f"64_{key}"]
        v2.append(f'<text x="{bx}" y="{by - 10}" fill="#c9d1d9" font-family="monospace" font-size="11.5" font-weight="bold">{lbl}</text>')
        v2.append(f'<rect x="{bx - 4}" y="{by - 4}" width="{w * scale + 8}" height="{h * scale + 8}" rx="4" fill="#05070a" stroke="#30363d" stroke-width="2"/>')
        for py, r in enumerate(rows):
            for px, ch in enumerate(r):
                if ch == "#":
                    v2.append(f'<rect x="{bx + px * scale}" y="{by + py * scale}" width="{px_size}" height="{px_size}" fill="#38bdf8"/>')
    v2.append("</svg>")
    v2_svg_path.write_text("\n".join(v2))
    print(f"Wrote {v2_svg_path}")

    # V3 (64x128 portrait, 6-column horizontal strip)
    v3_svg_path = ROOT / "docs" / "oled-v3-portrait.svg"
    v3_cell_w = 64 * scale + 24
    v3_w = 24 + 6 * v3_cell_w
    v3_h = 84 + 128 * scale
    v3 = [
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {v3_w} {v3_h}" width="{v3_w}" height="{v3_h}" shape-rendering="crispEdges">',
        '<rect width="100%" height="100%" rx="8" fill="#0d1117"/>',
        '<text x="24" y="30" fill="#e6edf3" font-family="monospace" font-size="14" font-weight="bold">V3 (1-DIN) Front Display — 0.96" 64×128 Portrait OLED (SSD1312)</text>',
    ]
    for i, (key, lbl) in enumerate(strip_items):
        bx = 24 + i * v3_cell_w
        by = 66
        w, h, rows = screens[f"64p_{key}"]
        v3.append(f'<text x="{bx}" y="{by - 10}" fill="#c9d1d9" font-family="monospace" font-size="11.5" font-weight="bold">{lbl}</text>')
        v3.append(f'<rect x="{bx - 4}" y="{by - 4}" width="{w * scale + 8}" height="{h * scale + 8}" rx="4" fill="#05070a" stroke="#30363d" stroke-width="2"/>')
        for py, r in enumerate(rows):
            for px, ch in enumerate(r):
                if ch == "#":
                    v3.append(f'<rect x="{bx + px * scale}" y="{by + py * scale}" width="{px_size}" height="{px_size}" fill="#4ade80"/>')
    v3.append("</svg>")
    v3_svg_path.write_text("\n".join(v3))
    print(f"Wrote {v3_svg_path}")

    bmp_py = ROOT.parent / "mechanical" / "scripts" / "oled_page1_bitmap.py"
    bmp_py.write_text(
        '"""Page 1 (Status) OLED pixel bitmaps compiled from gateway/src/device/OledDisplay.cpp."""\n\n'
        f"PAGE1_128X64 = {screens['64_overview'][2]!r}\n\n"
        f"PAGE1_64X128 = {screens['64p_overview'][2]!r}\n"
    )
    print(f"Wrote {bmp_py}")


if __name__ == "__main__":
    main()
