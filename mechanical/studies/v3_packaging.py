"""V3 (1 DIN) packaging study: does the concept in hardware/docs/variants.md fit?

    python3 mechanical/studies/v3_packaging.py

Boards and the large parts as boxes at their datasheet sizes, in enclosure
coordinates (x across the module, y up, z from the back towards the front).
Checks every box against the 1-module envelope (walls, 0.5 mm clearance) and
against every other box, prints the result and writes front/side/top views to
mechanical/build/v3/packaging_{front,side,top}.svg. No board layout: this only
decides whether the concept is worth laying out.
"""

import sys
from dataclasses import dataclass
from itertools import combinations
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle

REPO = Path(__file__).resolve().parents[2]
OUT = REPO / "mechanical/build/v3"

WIDTH, HIDDEN_H, HIDDEN_D = 18.0, 81.0, 44.0
FRONT_H, FRONT_D = 45.0, 17.0
WALL, CLEAR = 1.6, 0.5
FRONT_Y0 = (HIDDEN_H - FRONT_H) / 2
FRONT_Y1 = FRONT_Y0 + FRONT_H
TOTAL_D = HIDDEN_D + FRONT_D
IN_X = WIDTH / 2 - WALL - CLEAR
BOARD = 1.6
MAIN_X0 = -IN_X
MAIN_X1 = MAIN_X0 + BOARD


@dataclass
class Box:
    name: str
    x0: float
    x1: float
    y0: float
    y1: float
    z0: float
    z1: float
    kind: str = "part"
    through: str = ""
    group: str = ""

    def overlaps(self, other, gap=0.0):
        return (self.x0 < other.x1 + gap and other.x0 < self.x1 + gap and self.y0 < other.y1 + gap
                and other.y0 < self.y1 + gap and self.z0 < other.z1 + gap and other.z0 < self.z1 + gap)


def on_main(name, height, y0, y1, z0, z1, **kw):
    """A part on the front face of the main board (single-sided placement)."""
    return Box(name, MAIN_X1, MAIN_X1 + height, y0, y1, z0, z1, group="main", **kw)


CARRIER = 1.2


def on_carrier(name, height, y0, y1, z0, z1, **kw):
    """A part on the front face of the carrier (single-sided)."""
    return Box(name, MAIN_X0, MAIN_X0 + CARRIER + height, y0, y1, z0, z1, group="main", **kw)


def layout():
    face = MAIN_X0 + CARRIER
    boxes = [
        # Carrier: vertical, along the side wall; tall parts and the interconnects only
        Box("carrier", MAIN_X0, face, 14.5, 78.9, 2.1, 41.9, "board", group="main"),
        Box("carrier (front extension)", MAIN_X0, face, 22.0, 60.0, 41.9, 52.0, "board", group="main"),
        on_carrier("REM3-2415S/A/CTRL (10.4 +0.5)", 10.9, 46.0, 77.8, 2.5, 22.8),
        on_carrier("XIAO, 5 mm sockets", 11.0, 24.0, 41.8, 31.0, 52.0),
        on_carrier("UI FFC connector", 1.2, 44.0, 52.6, 47.0, 51.4),
        # KNX board: horizontal, NCN5130 + storage + 5 V buck on both sides
        Box("KNX board", face + 0.1, IN_X, 27.0, 28.2, 2.1, 30.5, "board", group="knx"),
        Box("470uF SMD (KNX board underside)", face + 1.0, face + 11.3, 16.5, 27.0, 2.6, 13.6, group="knx"),
        Box("NCN5130 + L1 + FFC (KNX board top)", face + 0.1, IN_X - 0.3, 28.2, 32.7, 2.6, 30.0, group="knx"),
        Box("passives (KNX board underside)", face + 0.5, IN_X - 0.3, 25.0, 27.0, 14.0, 30.0, group="knx"),
        # DALI board: parallel to the front, extending from the top down behind the
        # upper half of the UI board (above the KNX board at y 28.2); WAGO terminal at
        # the upper right, bridge and TX/RX + optocouplers below it, two FFCs on the back
        Box("DALI board", MAIN_X0 + CARRIER + 0.1, IN_X, 33.2, 78.9, 27.8, 29.0, "board", group="dali"),
        Box("WAGO 2601-1102", -1.6, IN_X, 62.3, 79.4, 29.0, 41.75, through="top", group="dali"),
        Box("DALI parts front (bridge, TX/RX, optos)", MAIN_X0 + CARRIER + 0.5, IN_X - 0.2, 43.0, 61.8, 29.0, 33.0,
            group="dali"),
        Box("DALI parts back + FFCs (clear of the REM3 and XIAO)", -1.6, IN_X - 0.2, 43.0, 78.4, 23.8, 27.8,
            group="dali"),
        Box("DALI logic FFC (front, bottom)", MAIN_X0 + CARRIER + 0.5, IN_X - 0.2, 33.7, 38.2, 29.0, 30.2,
            group="dali"),
        on_carrier("DALI limiter + JP1 (carrier front, beside the WAGO)", 3.8, 62.3, 78.4, 38.0, 41.4),
        # Inputs shelf and plugs
        Box("inputs shelf", -IN_X, IN_X, 11.5, 12.7, 12.0, 41.9, "board", group="bottom"),
        Box("KNX plug red/black (front)", -6.23, 6.23, 0.0, 11.5, 30.4, 41.5, through="bottom", group="bottom"),
        Box("AUX plug yellow/white (rear)", -6.23, 6.23, 0.0, 11.5, 18.0, 29.1, through="bottom", group="bottom"),
        Box("inputs protection (shelf top)", face + 0.5, IN_X - 0.3, 12.7, 15.2, 12.5, 41.5, group="bottom"),
        # UI board
        Box("UI board", -IN_X, IN_X, 20.25, 60.75, 53.8, 55.4, "board", group="ui"),
        Box("OLED WEA012832F (portrait)", -6.0, 6.0, 24.0, 59.8, 55.4, 57.8, group="ui"),
        Box("PRG/RST button", -6.2, -2.3, 20.8, 23.8, 55.4, 57.4, group="ui"),
        Box("FUNC button", 2.3, 6.2, 20.8, 23.8, 55.4, 57.4, group="ui"),
        Box("UI FFC connector (UI back)", -4.3, 4.3, 44.0, 48.4, 52.6, 53.8, group="ui"),
    ]
    return boxes


def envelope_problems(box):
    hidden = Box("hidden", -IN_X, IN_X, WALL + CLEAR, HIDDEN_H - WALL - CLEAR, WALL, HIDDEN_D - WALL - CLEAR)
    front = Box("front", -IN_X, IN_X, FRONT_Y0 + WALL + CLEAR, FRONT_Y1 - WALL - CLEAR, HIDDEN_D - WALL - CLEAR,
                TOTAL_D - WALL)
    region = front if box.z0 >= hidden.z1 else hidden
    problems = []
    lo_y = 0.0 if box.through == "bottom" else region.y0
    hi_y = HIDDEN_H if box.through == "top" else region.y1
    hi_z = TOTAL_D + 2.0 if box.through == "front" else (front.z1 if box.z1 > hidden.z1 else hidden.z1)
    if box.z1 > hidden.z1 and not (front.y0 <= box.y0 and box.y1 <= front.y1):
        problems.append(f"reaches z {box.z1:.1f} outside the front protrusion (y {front.y0:.1f}-{front.y1:.1f})")
    for axis, lo, hi, a, b in (("x", region.x0, region.x1, box.x0, box.x1), ("y", lo_y, hi_y, box.y0, box.y1),
                               ("z", region.z0, hi_z, box.z0, box.z1)):
        if a < lo - 1e-6 or b > hi + 1e-6:
            problems.append(f"{axis} {a:.2f}..{b:.2f} outside {lo:.2f}..{hi:.2f}")
    return problems


def collisions(boxes):
    clashes = []
    for a, b in combinations(boxes, 2):
        same_mount = {a.kind, b.kind} == {"board", "part"} and a.group == b.group
        if a.overlaps(b) and not same_mount:
            clashes.append((a.name, b.name))
    return clashes


COLOURS = {"main": "#1565c0", "knx": "#6a1b9a", "bottom": "#c62828", "dali": "#ef6c00", "ui": "#2e7d32"}


def outline(ax, h_axis, v_axis):
    if (h_axis, v_axis) == ("x", "y"):
        ax.add_patch(Rectangle((-WIDTH / 2, 0), WIDTH, HIDDEN_H, fill=False, ec="k", lw=0.8))
    elif (h_axis, v_axis) == ("z", "y"):
        ax.add_patch(Rectangle((0, 0), HIDDEN_D, HIDDEN_H, fill=False, ec="k", lw=0.8))
        ax.add_patch(Rectangle((HIDDEN_D, FRONT_Y0), FRONT_D, FRONT_H, fill=False, ec="k", lw=0.8))
    else:
        ax.add_patch(Rectangle((-WIDTH / 2, 0), WIDTH, HIDDEN_D, fill=False, ec="k", lw=0.8))
        ax.add_patch(Rectangle((-WIDTH / 2, HIDDEN_D), WIDTH, FRONT_D, fill=False, ec="k", lw=0.8))


def view(boxes, h_axis, v_axis, title, path):
    fig, ax = plt.subplots(figsize=(7, 9))
    outline(ax, h_axis, v_axis)
    for b in boxes:
        h0, h1 = getattr(b, h_axis + "0"), getattr(b, h_axis + "1")
        v0, v1 = getattr(b, v_axis + "0"), getattr(b, v_axis + "1")
        colour = COLOURS[b.group]
        ax.add_patch(Rectangle((h0, v0), h1 - h0, v1 - v0, fc=colour, alpha=0.18 if b.kind == "part" else 0.45,
                               ec=colour, lw=0.6))
        if b.kind == "part" and (h1 - h0) * (v1 - v0) > 30:
            ax.text((h0 + h1) / 2, (v0 + v1) / 2, b.name, fontsize=4.5, ha="center", va="center")
    ax.set_aspect("equal")
    ax.autoscale_view()
    ax.set_xlabel(f"{h_axis} [mm]")
    ax.set_ylabel(f"{v_axis} [mm]")
    ax.set_title(title, fontsize=9)
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)


def main():
    boxes = layout()
    failed = False
    for b in boxes:
        for problem in envelope_problems(b):
            print(f"ENVELOPE  {b.name}: {problem}")
            failed = True
    for a, b in collisions(boxes):
        print(f"COLLISION {a} <-> {b}")
        failed = True
    budget = 2 * IN_X - CARRIER
    print(f"width for parts on the carrier's front face: {budget:.1f} mm (single-sided placement)")
    for b in boxes:
        if b.group == "main" and b.kind == "part":
            height = b.x1 - b.x0 - CARRIER
            print(f"  {b.name:32s} {height:5.1f} mm, margin {budget - height:4.1f}")
    OUT.mkdir(parents=True, exist_ok=True)
    view(boxes, "x", "y", "V3 front view (looking at the front face)", OUT / "packaging_front.svg")
    view(boxes, "z", "y", "V3 side view (rail on the left)", OUT / "packaging_side.svg")
    view(boxes, "x", "z", "V3 top view (rail at the bottom)", OUT / "packaging_top.svg")
    print("FIT" if not failed else "DOES NOT FIT")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
