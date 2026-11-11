"""Hand-assembly drawing for one board (`eda` container).

    python3 hardware/scripts/assembly_drawing.py <board> [out.pdf]

Writes <board>_assembly.pdf: one page per populated side (the bottom mirrored,
as seen from below) and a values page. Every part shows its body outline,
reference and value, a red dot on pin 1, and a black corner flag when it is
fitted by hand rather than by the assembler. Parts with the same value share a
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
                    "big": (box[2] - box[0]) * (box[3] - box[1]) >= BIG_MM2})
    return board, out


def outline(board):
    bb = board.GetBoardEdgesBoundingBox()
    return mm(bb.GetLeft()), mm(bb.GetTop()), mm(bb.GetRight()), mm(bb.GetBottom())


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
        if p["pin1"]:
            ax.add_patch(Circle(p["pin1"], radius=max(0.18, span / 250), color="red", zorder=4))
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
                 "red dot = pin 1, black corner = hand-fitted, dashed = large part, colour = value",
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
    return drawn


if __name__ == "__main__":
    name = sys.argv[1]
    target = Path(sys.argv[2]) if len(sys.argv) > 2 else layout.build_dir(name) / f"{name}_assembly.pdf"
    print(write(name, target))
