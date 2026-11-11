"""Hand-assembly drawing for one board (`eda` container).

    python3 hardware/scripts/assembly_drawing.py <board> [out.pdf]

Writes <board>_assembly.pdf: one page per populated side (the bottom mirrored,
as seen from below) and a values page. Every part shows its body outline,
reference and value and, only where orientation matters, its polarity mark:
a black bar on a diode's cathode end (the band printed on the part), a "+" on
the electrolytic capacitor's + pad (its can has a dark stripe on the - side),
and a red pin-1 dot on ICs, transistors, LEDs, optocouplers and modules.
Resistors, ceramic capacitors, inductors, fuses and crystals fit either way
and carry no mark. A black corner flag marks parts fitted by hand rather than
by the assembler. Parts with the same value share a
colour. Big parts (modules, terminals, converters) are dashed and drawn under
the small ones, so dense areas stay readable.
"""

import colorsys
import hashlib
import json
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pcbnew
from matplotlib.backends.backend_pdf import PdfPages
from matplotlib.patches import Circle, Rectangle

import layout

BIG_MM2 = 60.0
NON_POLAR = ("R_", "C_", "L_", "Fuse_", "Crystal_", "PinHeader_", "Phoenix_PST", "TerminalBlock_")
DIODE = ("D_SMA", "D_SOD", "D_SMB", "D_SMC")
ELECTROLYTIC = ("CP_",)


def polarity(footprint, value=""):
    """'none', 'cathode' (pad 1 = K), 'plus' (pad 1 = +) or 'pin1'. Bidirectional
    TVS diodes (part numbers ending in CA, e.g. SMAJ40CA) fit either way."""
    if footprint.startswith(DIODE):
        return "none" if (value.split() or [""])[0].upper().endswith("CA") else "cathode"
    if footprint.startswith(ELECTROLYTIC):
        return "plus"
    if footprint.startswith(NON_POLAR):
        return "none"
    return "pin1"
PAGE = (8.27, 11.69)   # A4 portrait, inches


def colour(value):
    """Stable, distinguishable colour per value (hue from a hash)."""
    h = int(hashlib.sha1(value.encode()).hexdigest()[:6], 16) / 0xFFFFFF
    return colorsys.hls_to_rgb(h, 0.55, 0.75)


def mm(v):
    return pcbnew.ToMM(v)


def pin1(fp):
    pads = [p for p in fp.Pads() if p.GetNumber() in ("1", "A1")]
    pad = pads[0] if pads else None
    return (mm(pad.GetPosition().x), mm(pad.GetPosition().y)) if pad else None


def parts(board_name):
    """One dict per footprint that has a reference designator and pads."""
    board = pcbnew.LoadBoard(str(layout.stored_board_path(board_name)))
    sourcing = {p["ref"]: p for p in json.loads(layout.build_dir(board_name).joinpath("parts.json").read_text())}
    out = []
    for fp in board.Footprints():
        ref = fp.GetReference()
        if not fp.Pads() or ref not in sourcing:
            continue
        bb = layout.fab_box(fp)
        box = (mm(bb.GetLeft()), mm(bb.GetTop()), mm(bb.GetRight()), mm(bb.GetBottom()))
        out.append({"ref": ref, "value": sourcing[ref]["value"], "side": "bottom" if fp.IsFlipped() else "top",
                    "box": box, "pin1": pin1(fp), "hand": sourcing[ref]["assembly"] == "hand",
                    "polarity": polarity(str(fp.GetFPID().GetLibItemName()), sourcing[ref]["value"]),
                    "big": (box[2] - box[0]) * (box[3] - box[1]) >= BIG_MM2})
    return board, out


def outline(board):
    bb = board.GetBoardEdgesBoundingBox()
    return mm(bb.GetLeft()), mm(bb.GetTop()), mm(bb.GetRight()), mm(bb.GetBottom())


def mark(ax, p, span, font):
    """Polarity mark in the part's body, on the pad-1 end."""
    if p["polarity"] == "none" or not p["pin1"]:
        return
    bx0, by0, bx1, by1 = p["box"]
    px, py = p["pin1"]
    if p["polarity"] == "pin1":
        ax.add_patch(Circle((px, py), radius=max(0.25, span / 200), color="red", zorder=4))
        ax.text(px, py, "1", ha="center", va="center", fontsize=font * 0.55, color="white", zorder=5,
                fontweight="bold")
        return
    if p["polarity"] == "plus":
        ax.text(px, py, "+", ha="center", va="center", fontsize=font * 1.6, color="red", zorder=5,
                fontweight="bold")
        return
    w, h = bx1 - bx0, by1 - by0
    cx, cy = (bx0 + bx1) / 2, (by0 + by1) / 2
    if w >= h:   # cathode band across the body at the pad-1 end
        x = bx0 if px < cx else bx1 - 0.22 * w
        ax.add_patch(Rectangle((x, by0), 0.22 * w, h, color="black", zorder=4))
    else:
        y = by0 if py < cy else by1 - 0.22 * h
        ax.add_patch(Rectangle((bx0, y), w, 0.22 * h, color="black", zorder=4))


def key_page(pdf, board_name):
    fig, ax = plt.subplots(figsize=PAGE)
    ax.axis("off")
    lines = [
        f"{board_name} - how to read this drawing and the parts",
        "",
        "Black bar = diode cathode (K). The diode body carries a band on that end.",
        '"+" = electrolytic capacitor + pad. The can has a dark stripe (or "-" marks) on the other side.',
        'Red dot "1" = pin 1 of ICs, transistors, LEDs, optocouplers, connectors and modules:',
        "    SOT-23 / SOT-23-5 / TSOT-23-6 / SC-70-6: pin 1 is marked by a dot or bevel, or follow the leg pattern",
        "        (SOT-23: the single leg is pin 3, opposite pins 1 and 2).",
        "    SO-6 optocouplers, the NCN5130 (QFN) and the supervisor: a dot or notch at pin 1 on the package.",
        "    WS2812B-MINI-V3J: a notch / marked corner on the body is pin 1 (VDD); the legs differ, so the",
        "        footprint only fits one way.",
        "    The XIAO, DC-DC converters and the REC3 module: their silkscreen / pin-1 marking and the socket shape.",
        "No mark = fits either way (resistors, ceramic capacitors, inductors, fuse, crystal, pin strips).",
        "Black corner = fitted by hand (not placed by the assembler); dashed outline = large part.",
        "Same colour = same value (see the values page).",
        "",
        "Tools: a 3-5x head magnifier or a 10x loupe and good light are enough to read the bands and",
        "pin-1 dots on SOT-23 and 0805 parts; a USB microscope helps for the NCN5130 QFN and SC-70-6.",
    ]
    for i, text in enumerate(lines):
        ax.text(0.02, 0.97 - i * 0.035, text, fontsize=10 if i == 0 else 8, transform=ax.transAxes, va="top",
                fontweight="bold" if i == 0 else "normal")
    pdf.savefig(fig)
    plt.close(fig)


def side_page(pdf, board_name, edge, items, side):
    fig, ax = plt.subplots(figsize=PAGE)
    x0, y0, x1, y1 = edge
    ax.add_patch(Rectangle((x0, y0), x1 - x0, y1 - y0, fill=False, lw=1.2, color="0.3"))
    span = max(x1 - x0, y1 - y0)
    font = max(4.5, min(8.0, 300 / span))
    flag = max(0.8, span / 60)
    for p in sorted(items, key=lambda p: not p["big"]):
        bx0, by0, bx1, by1 = p["box"]
        c = colour(p["value"])
        ax.add_patch(Rectangle((bx0, by0), bx1 - bx0, by1 - by0, lw=0.8 if p["big"] else 0.6,
                               ls="--" if p["big"] else "-", ec=c, fc=(*c, 0.08 if p["big"] else 0.35),
                               zorder=1 if p["big"] else 2))
        mark(ax, p, span, font)
        if p["hand"]:
            ax.add_patch(Rectangle((bx0, by0), flag, flag, color="black", zorder=3))
        cx, cy = (bx0 + bx1) / 2, (by0 + by1) / 2
        label = f"{p['ref']}\n{p['value']}" if not p["big"] else f"{p['ref']} {p['value']}"
        ax.text(cx, cy, label, ha="center", va="center", fontsize=font * (1.1 if p["big"] else 0.8),
                zorder=5, linespacing=0.9, fontweight="bold" if not p["big"] else "normal")
    ax.set_xlim(x0 - 2, x1 + 2)
    ax.set_ylim(y1 + 2, y0 - 2)   # board y grows downwards
    if side == "bottom":
        ax.invert_xaxis()           # seen from below: mirrored left-right
    ax.set_aspect("equal")
    ax.axis("off")
    ax.set_title(f"{board_name} - {side} side{' (mirrored, seen from below)' if side == 'bottom' else ''}\n"
                 'black bar = diode cathode, "+" = electrolytic +, red "1" = pin 1, no mark = either way;\n'
                 "black corner = hand-fitted, dashed = large part, colour = value (key on the last pages)",
                 fontsize=9)
    pdf.savefig(fig)
    plt.close(fig)


def values_page(pdf, board_name, items):
    groups = {}
    for p in items:
        groups.setdefault((p["value"], p["side"]), []).append(p["ref"])
    rows = sorted(groups.items(), key=lambda kv: (kv[0][1], kv[0][0]))
    fig, ax = plt.subplots(figsize=PAGE)
    ax.axis("off")
    ax.set_title(f"{board_name} - values ({len(items)} parts)", fontsize=10)
    y = 0.97
    for (value, side), refs in rows:
        ax.add_patch(Rectangle((0.0, y - 0.012), 0.025, 0.018, color=colour(value), transform=ax.transAxes))
        ax.text(0.04, y, f"{value}", fontsize=7, transform=ax.transAxes, va="center")
        ax.text(0.36, y, f"x{len(refs)}  {side}", fontsize=7, transform=ax.transAxes, va="center")
        ax.text(0.50, y, ", ".join(sorted(refs, key=lambda r: (r.rstrip("0123456789"), int("0" + r.lstrip("ABCDEFGHIJKLMNOPQRSTUVWXYZ_"))))),
                fontsize=6, transform=ax.transAxes, va="center", wrap=True)
        y -= 0.024
        if y < 0.03:
            pdf.savefig(fig)
            plt.close(fig)
            fig, ax = plt.subplots(figsize=PAGE)
            ax.axis("off")
            y = 0.97
    pdf.savefig(fig)
    plt.close(fig)


def write(board_name, path):
    """Write the PDF; returns the references drawn on each side."""
    board, items = parts(board_name)
    edge = outline(board)
    drawn = {}
    with PdfPages(path) as pdf:
        for side in ("top", "bottom"):
            on_side = [p for p in items if p["side"] == side]
            if on_side:
                side_page(pdf, board_name, edge, on_side, side)
                drawn[side] = sorted(p["ref"] for p in on_side)
        values_page(pdf, board_name, items)
        key_page(pdf, board_name)
    return drawn


if __name__ == "__main__":
    name = sys.argv[1]
    target = Path(sys.argv[2]) if len(sys.argv) > 2 else layout.build_dir(name) / f"{name}_assembly.pdf"
    print(write(name, target))
