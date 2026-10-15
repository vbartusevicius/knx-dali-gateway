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
    st.knx_individual_address = (1U << 12U) | (1U << 8U) | 20U; // 1.1.20
    st.knx_configured = true;
    st.knx_prog_mode = false;
    st.knx_bus_ok = true;
    st.dali_psu_enabled = true;
    st.present_gear_count = 18;
    st.active_lamp_count = 12;
    st.faulty_gear_count = 1;
    st.input_device_count = 4;
    st.average_brightness_pct = 68;
    st.last_scene = 2;
    for (unsigned i = 0; i < 18; ++i) {
        st.gear_present.set(i);
        if (i % 3 != 0) st.gear_on.set(i);
    }
    st.gear_faulty.set(7);
    const std::uint8_t grp[16] = {254, 200, 140, 90, 0, 220, 180, 0, 110, 60, 0, 254, 128, 0, 45, 0};
    for (unsigned g = 0; g < 16; ++g) st.group_levels[g] = grp[g];
    st.wifi_state = WifiState::Connected;
    st.wifi_rssi_dbm = -61;
    std::snprintf(st.ipv4.data(), st.ipv4.size(), "192.168.1.42");
    std::snprintf(st.ssid.data(), st.ssid.size(), "Home-IoT-24G");
    st.mqtt_connected = true;
    st.ha_discovery_enabled = true;
    st.ws_clients = 2;
    st.dali_tx_frames = 14820;
    st.dali_collisions = 1;
    st.logged_errors = 2;
    st.last_error_code = ErrorCode::DaliLampFailure;
    st.last_error_address = 7;
    st.uptime_seconds = 14 * 86400 + 6 * 3600 + 22 * 60;

    auto dump = [&](const char* id, OledGeometry geom, OledPage page, const OledSystemState& s) {
        OledDisplay disp(geom);
        disp.set_page(page, 100);
        disp.render(s, 100);
        std::printf("SCREEN %s %u %u\n", id, disp.width(), disp.height());
        for (unsigned y = 0; y < disp.height(); ++y) {
            for (unsigned x = 0; x < disp.width(); ++x) {
                std::putchar(disp.pixel(x, y) ? '#' : '.');
            }
            std::putchar('\n');
        }
    };

    const OledPage pages[5] = {
        OledPage::Overview, OledPage::DaliMatrix, OledPage::GroupsScenes,
        OledPage::NetworkHa, OledPage::Diagnostics
    };
    const char* names[5] = {"overview", "matrix", "groups", "network", "diagnostics"};
    for (int i = 0; i < 5; ++i) {
        char id64[32], id64p[32];
        std::snprintf(id64, sizeof(id64), "64_%s", names[i]);
        std::snprintf(id64p, sizeof(id64p), "64p_%s", names[i]);
        dump(id64, OledGeometry::Oled128x64, pages[i], st);
        dump(id64p, OledGeometry::Oled64x128, pages[i], st);
    }

    OledSystemState prog = st;
    prog.knx_prog_mode = true;
    dump("64_modal_prog", OledGeometry::Oled128x64, OledPage::Overview, prog);
    dump("64p_modal_prog", OledGeometry::Oled64x128, OledPage::Overview, prog);
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
        ("overview", "Page 1 — Bus &amp; System Overview"),
        ("matrix", "Page 2 — 64-Channel DALI Lamp Matrix"),
        ("groups", "Page 3 — 16 DALI Groups Bar Chart &amp; Scene"),
        ("network", "Page 4 — WiFi, MQTT &amp; Home Assistant"),
        ("diagnostics", "Page 5 — Diagnostics &amp; Latest Error Log"),
        ("modal_prog", "Modal Alert — KNX Programming Mode"),
    ]

    scale = 1.8
    w128 = int(128 * scale)
    w64 = int(64 * scale)
    pair_w = w128 + 18 + w64 + 34
    total_w = 24 + pair_w * 2
    row_h = int(128 * scale) + 48
    total_h = 74 + 3 * row_h

    svg = [
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {total_w} {total_h}" width="{total_w}" height="{total_h}">',
        '<rect width="100%" height="100%" fill="#0f141c"/>',
        (
            '<text x="24" y="28" fill="#e6edf3" font-family="monospace" font-size="14" font-weight="bold">'
            "KNX-DALI Gateway Front-Panel 0.96&quot; OLED Screens — V1/V2 (128x64 Landscape) vs V3 (64x128 Portrait)</text>"
        ),
        (
            '<text x="24" y="48" fill="#8b949e" font-family="monospace" font-size="11">'
            'Each pair: Left = V1/V2 128x64 Landscape (0.96" Winstar WEA012864D) | Right = V3 64x128 Portrait (0.96" 14x28mm)</text>'
        ),
    ]

    for idx_t, (key, label) in enumerate(titles):
        grid_col = idx_t % 2
        grid_row = idx_t // 2
        base_x = 24 + grid_col * pair_w
        base_y = 68 + grid_row * row_h
        svg.append(
            f'<text x="{base_x}" y="{base_y + 12}" fill="#c9d1d9" font-family="monospace" font-size="11" font-weight="bold">{label}</text>'
        )
        for prefix, dx, color in [("64", 0, "#38bdf8"), ("64p", w128 + 18, "#4ade80")]:
            w, h, rows = screens[f"{prefix}_{key}"]
            bx = base_x + dx
            by = base_y + 22 + int(((128 - h) * scale) // 2)
            svg.append(
                f'<rect x="{bx - 3}" y="{by - 3}" width="{w * scale + 6:.1f}" height="{h * scale + 6:.1f}" rx="4" fill="#05070a" stroke="#30363d" stroke-width="1.5"/>'
            )
            for py, r in enumerate(rows):
                for px, ch in enumerate(r):
                    if ch == "#":
                        svg.append(
                            f'<rect x="{bx + px * scale:.2f}" y="{by + py * scale:.2f}" width="{scale - 0.2:.2f}" height="{scale - 0.2:.2f}" fill="{color}"/>'
                        )

    svg.append("</svg>")
    OUT_SVG.write_text("\n".join(svg))
    print(f"Wrote {OUT_SVG}")


if __name__ == "__main__":
    main()
