"""Coloured multi-part 3D STEP models for project parts KiCad ships no 3D model for.

The STEP files next to this script are library assets referenced by
hardware/scripts/layout.py; rerun after changing a body size (`cad` container):

    python3 hardware/circuits/lib/gateway_3d/generate.py
"""

from pathlib import Path

import cadquery as cq

OUT_DIR = Path(__file__).resolve().parent

C_CHARCOAL = cq.Color(0.11, 0.11, 0.13)
C_LABEL = cq.Color(0.22, 0.22, 0.25)
C_BLACK = cq.Color(0.06, 0.06, 0.08)
C_SILVER = cq.Color(0.46, 0.48, 0.52)
C_ALU = cq.Color(0.56, 0.58, 0.62)
C_GOLD = cq.Color(0.75, 0.60, 0.18)
C_GREEN_PCB = cq.Color(0.08, 0.30, 0.15)
C_GREEN_TERM = cq.Color(0.08, 0.46, 0.20)
C_ORANGE = cq.Color(0.88, 0.36, 0.06)
C_IVORY = cq.Color(0.76, 0.74, 0.68)
C_WHITE = cq.Color(0.82, 0.82, 0.84)
C_LED_LENS = cq.Color(0.68, 0.72, 0.65)


def _export_asm(name: str, parts: list[tuple[cq.Workplane, cq.Color]]) -> Path:
    asm = cq.Assembly()
    for idx, (solid, color) in enumerate(parts):
        asm.add(solid, name=f"p{idx}", color=color)
    step_path = OUT_DIR / f"{name}.step"
    asm.save(str(step_path), exportType="STEP")
    print(f"  {name} -> {step_path.name}")
    return step_path


def make_xiao():
    # Main board solid keeps exact (17.8, 22.0, 3.5) dimensions for test_step_geometry.py
    w, d, h = 17.8, 22.0, 3.5
    board = cq.Workplane("XY").box(w, d, h, centered=(True, True, False))
    shield = cq.Workplane("XY").box(10.5, 11.5, 1.8, centered=(True, True, False)).translate((0, -2.0, h))
    usbc = cq.Workplane("XY").box(8.9, 6.8, 3.1, centered=(True, True, False)).translate((0, d / 2 - 2.6, h))
    # Pair of 1x7 female socket headers descending from the XIAO board (z=0 down to z=-11)
    hdr_l = cq.Workplane("XY").box(2.5, 17.8, 10.8, centered=(True, True, False)).translate((-7.62, 0, -10.8))
    hdr_r = cq.Workplane("XY").box(2.5, 17.8, 10.8, centered=(True, True, False)).translate((7.62, 0, -10.8))
    _export_asm("XIAO_ESP32C6", [
        (board, C_GREEN_PCB),
        (shield, C_SILVER),
        (usbc, C_ALU),
        (hdr_l, C_BLACK),
        (hdr_r, C_BLACK),
    ])


def make_rem3():
    # Vertical DIP-24 body matching Converter_DCDC_RECOM_REM3_DIP24_CTRL courtyard (20.3 x 31.8 x 10.7)
    body = cq.Workplane("XY").box(20.3, 31.8, 10.7, centered=(True, True, False))
    label = cq.Workplane("XY").box(16.0, 22.0, 0.08, centered=(True, True, False)).translate((0, 0, 10.7))
    _export_asm("REM3_DIP24", [(body, C_CHARCOAL), (label, C_LABEL)])


def make_traco_sip(name: str, w: float, d: float, h: float):
    body = cq.Workplane("XY").box(w, d, h, centered=(True, True, False))
    stripe = cq.Workplane("XY").box(w * 0.82, d * 0.55, 0.08, centered=(True, True, False)).translate((0, 0, h))
    _export_asm(name, [(body, C_CHARCOAL), (stripe, C_LABEL)])


def make_kf142r():
    w, d, h = 11.7, 14.2, 11.1
    body = cq.Workplane("XY").box(w, d, h, centered=(True, True, False))
    b1 = cq.Workplane("XY").box(2.4, 3.6, 1.4, centered=(True, True, False)).translate((-2.54, 1.2, h))
    b2 = cq.Workplane("XY").box(2.4, 3.6, 1.4, centered=(True, True, False)).translate((2.54, 1.2, h))
    _export_asm("TerminalBlock_Kefa_KF142R-5.08-2P", [(body, C_GREEN_TERM), (b1, C_ORANGE), (b2, C_ORANGE)])


def make_maixu():
    w, d, h = 11.6, 9.1, 8.0
    body = cq.Workplane("XY").box(w, d, h, centered=(True, True, False))
    s1 = cq.Workplane("XY").circle(1.4).extrude(0.3).translate((-2.5, 0, h))
    s2 = cq.Workplane("XY").circle(1.4).extrude(0.3).translate((2.5, 0, h))
    _export_asm("TerminalBlock_MaiXu_MX126-5.0-02P", [(body, C_GREEN_TERM), (s1, C_SILVER), (s2, C_SILVER)])


def make_wago_2086():
    w, d, h, y = 7.7, 13.6, 8.15, -2.25
    body = cq.Workplane("XY").box(w, d, h, centered=(True, True, False)).translate((0, y, 0))
    l1 = cq.Workplane("XY").box(2.2, 3.8, 0.8, centered=(True, True, False)).translate((-1.75, y + 1.5, h))
    l2 = cq.Workplane("XY").box(2.2, 3.8, 0.8, centered=(True, True, False)).translate((1.75, y + 1.5, h))
    _export_asm("TerminalBlock_WAGO_2086-1202_SMD", [(body, C_CHARCOAL), (l1, C_ORANGE), (l2, C_ORANGE)])


def make_phoenix_pst():
    w, d, h = 8.55, 2.8, 9.9
    body = cq.Workplane("XY").box(w, d, h, centered=(True, True, False))
    p1 = cq.Workplane("XY").circle(0.55).extrude(h + 0.15).translate((-2.875, 0, 0))
    p2 = cq.Workplane("XY").circle(0.55).extrude(h + 0.15).translate((2.875, 0, 0))
    _export_asm("Phoenix_PST_1,0-2-5,75_R32", [(body, C_CHARCOAL), (p1, C_GOLD), (p2, C_GOLD)])


def make_fuse_1812():
    w, d, h = 4.5, 3.2, 1.2
    body = cq.Workplane("XY").box(w, d, h, centered=(True, True, False))
    c1 = cq.Workplane("XY").box(0.8, d + 0.04, h + 0.04, centered=(True, True, False)).translate((-w / 2 + 0.4, 0, 0))
    c2 = cq.Workplane("XY").box(0.8, d + 0.04, h + 0.04, centered=(True, True, False)).translate((w / 2 - 0.4, 0, 0))
    _export_asm("Fuse_1812_4532Metric", [(body, C_GREEN_PCB), (c1, C_SILVER), (c2, C_SILVER)])


def make_ws2812b():
    body = cq.Workplane("XY").box(3.5, 3.5, 1.57, centered=(True, True, False))
    lens = cq.Workplane("XY").circle(1.25).extrude(0.15).translate((0, 0, 1.57))
    _export_asm("LED_WS2812B-MINI-V3J_3.5x3.5mm", [(body, C_WHITE), (lens, C_LED_LENS)])


def make_fpc(name: str, w: float, d: float, h: float):
    body = cq.Workplane("XY").box(w, d, h, centered=(True, True, False))
    latch = cq.Workplane("XY").box(w * 0.84, d * 0.45, 0.35, centered=(True, True, False)).translate((0, -d * 0.15, h))
    _export_asm(name, [(body, C_IVORY), (latch, C_CHARCOAL)])


def make_bent_capacitor(name="CP_Radial_D10.0mm_P5.00mm_Horizontal_Bent", diameter=10.0, length=16.0,
                        pitch=5.0, bend=3.0, axis_height=7.0, lead=0.6, courtyard=(-1.25, 19.25)):
    """Capacitor lying on its side: leads rise from the pads, bend 90 deg and
    enter the body end. Centred on the footprint courtyard like the boxes
    (KiCad model y is the footprint's -y)."""
    centre = sum(courtyard) / 2
    pad_y, body_end = centre, centre - bend
    body = (cq.Workplane("XZ").center(0, axis_height).circle(diameter / 2).extrude(length)
            .translate((0, body_end, 0)))
    for x in (-pitch / 2, pitch / 2):
        rise = cq.Workplane("XY").center(x, pad_y).circle(lead / 2).extrude(axis_height)
        run = (cq.Workplane("XZ").center(x, axis_height).circle(lead / 2).extrude(bend)
               .translate((0, pad_y, 0)))
        body = body.union(rise).union(run)
    _export_asm(name, [(body, C_ALU)])


def main():
    print("Generating coloured 3D STEP models:")
    make_xiao()
    make_rem3()
    make_traco_sip("TMR2_SIP8", 21.8, 9.1, 11.2)
    make_traco_sip("TRN3_SIP5", 11.9, 7.7, 11.0)
    make_traco_sip("TMR1_SIP6", 17.0, 6.0, 10.2)
    make_kf142r()
    make_maixu()
    make_wago_2086()
    make_phoenix_pst()
    make_fuse_1812()
    make_ws2812b()
    make_fpc("FPC_Kinghelm_KH-FPC0.5-H1.2SMT-8P", 8.9, 4.2, 1.2)
    make_fpc("FPC_XUNPU_FPC-05F-14PH20", 11.6, 4.5, 2.0)
    make_bent_capacitor()
    print(f"\nAll models saved to {OUT_DIR}")


if __name__ == "__main__":
    main()
