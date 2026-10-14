"""Fit drawing: case openings and supports over the real board parts.

    python3 mechanical/scripts/fit_drawing.py

Writes mechanical/build/<variant>/fit_front.svg (front face with every
opening over the part outlines of all boards) and fit_side.svg (side
section with board positions, part heights, the mated KNX plug and the support
posts). All geometry comes from enclosure_features, the same source the
CadQuery model uses, and from the layout height tables.
"""

from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import enclosure_features as ef
import matplotlib.pyplot as plt
import yaml
from matplotlib.patches import Circle, Rectangle

REPO = Path(__file__).resolve().parents[2]
SPEC = ef.SPEC
OUT = REPO / "mechanical/build" / ef.VARIANT
WALL = SPEC["enclosure"]["wall"]
MAIN_TOP_Z = SPEC["boards"]["main"].get("z", 21.2)
MAIN_THICK = SPEC["boards"]["main"]["thickness"]
UI_BOTTOM_Z = SPEC["front_ui"]["board_z"]
UI_THICK = SPEC["boards"]["ui"]["thickness"]
PLUG_HEIGHT = 11.5
LABELLED = {"main": {"J1", "J2", "J3", "J4", "J5", "J6", "JP1", "U8", "U7", "U5", "U4", "C4", "U3", "U2", "U1"},
            "ui": {"SW1", "SW2", "D1", "D2", "D3", "J1", "J2"},
            "interface": {"J1", "J2", "U1", "U2"},
            "knx": {"U1", "C4", "L1", "J1"},
            "dali": {"J1", "J2", "J3", "U1", "U2", "D1"},
            "inputs": {"J1", "J2", "J3", "F1"}}
COLOURS = {"main": "#2e7d32", "ui": "#1565c0", "interface": "#ef6c00",
           "knx": "#6a1b9a", "dali": "#ef6c00", "inputs": "#c62828"}
BOARDS = ef.SPEC["variants"][ef.VARIANT]["boards"]


def heights(board):
    plan = yaml.safe_load((REPO / f"hardware/circuits/variants/{ef.VARIANT}/layout_{board}.yaml").read_text())
    return plan["heights"]


to_enc = ef.board_to_enc


def enc_box(board, comp):
    b = comp["bbox"]
    x0, y0 = to_enc(board, b["x0"], b["y0"])
    x1, y1 = to_enc(board, b["x1"], b["y1"])
    return x0, y0, x1 - x0, y1 - y0


def rect(ax, x, y, w, h, **kw):
    ax.add_patch(Rectangle((x, y), w, h, **kw))


def front_view():
    fig, ax = plt.subplots(figsize=(7, 10))
    half = ef.WIDTH / 2
    rect(ax, -half, 0, ef.WIDTH, ef.HIDDEN_H, fill=False, lw=2, ec="black")
    rect(ax, -half, ef.FRONT_Y_START, ef.WIDTH, ef.FRONT_H, fill=True, fc="#f2f2f2", ec="black", lw=1.5)
    boxes = ef.part_boxes()
    for board in BOARDS:
        colour = COLOURS[board]
        for slab in ef.board_slabs(board):
            rect(ax, slab["x0"], slab["y0"], slab["x1"] - slab["x0"], slab["y1"] - slab["y0"],
                 fill=False, ec=colour, ls="--", lw=1)
        comps = ef.components(board)
        for b in [p for p in boxes if p["board"] == board and p["ref"] in comps]:
            ref = b["ref"]
            x, y, bw, bh = b["x0"], b["y0"], b["x1"] - b["x0"], b["x1"] - b["y0"]
            bh = b["y1"] - b["y0"]
            if board == "main" and ef.FRONT_Y_START < y + bh / 2 < ef.FRONT_Y_END and ref not in LABELLED[board]:
                continue
            back = comps[ref]["side"] == "back"
            rect(ax, x, y, bw, bh, fill=False, ec=colour, lw=0.6, ls=":" if back else "-")
            if ref in LABELLED[board]:
                ax.text(x + bw / 2, y + bh / 2, f"{board}:{ref}", fontsize=6, ha="center",
                        va="center", color=colour)
    for name, holes in ef.terminal_holes().items():
        size = 10.6 if name == "KNX" else 4.0
        for h in holes:
            rect(ax, h["x"] - size / 2, h["y"] - size / 2, size, size, fc="#ff7043", alpha=0.45, ec="#d84315")
    for h in ef.led_windows():
        rect(ax, h["x"] - 1.75, h["y"] - 1.75, 3.5, 3.5, fc="#ff7043", alpha=0.45, ec="#d84315")
    for h in ef.button_holes().values():
        ax.add_patch(Circle((h["x"], h["y"]), 1.75, fc="#ff7043", alpha=0.45, ec="#d84315"))
    win = ef.oled_window()
    rect(ax, win["cx"] - win["w"] / 2, win["cy"] - win["h"] / 2, win["w"], win["h"],
         fc="#ff7043", alpha=0.3, ec="#d84315")
    for p in ef.legs():
        rect(ax, p["x"] - 2.5, p["y"] - 2.5, 5, 5, fill=False, ec="#6a1b9a", lw=1.4, ls="-.")
    for p in ef.pegs() + ef.standoffs():
        ax.add_patch(Circle((p["x"], p["y"]), 1.5, fill=False, ec="#6a1b9a", lw=1.4))
    for s in ef.wire_entries():
        ax.plot([s["x0"], s["x1"]], [ef.HIDDEN_H, ef.HIDDEN_H], color="#ff7043", lw=5)
    ax.set_xlim(-half - 3, half + 3)
    ax.set_ylim(-3, ef.HIDDEN_H + 3)
    ax.set_aspect("equal")
    ax.set_title("Front view: openings (orange) over parts (green main, blue UI; dotted = back side);\n"
                 "legs (squares), pegs and standoffs (circles) purple; wire slots as orange bars", fontsize=8)
    ax.set_xlabel("x [mm]")
    ax.set_ylabel("y [mm]")
    ax.grid(alpha=0.2)
    return fig


def side_view():
    fig, ax = plt.subplots(figsize=(8, 10))
    lo, hi = ef.SHOULDER_Z["bottom"], ef.SHOULDER_Z["top"]
    profile = [(0, 0), (lo, 0), (lo, ef.FRONT_Y_START), (ef.TOTAL_D, ef.FRONT_Y_START),
               (ef.TOTAL_D, ef.FRONT_Y_END), (hi, ef.FRONT_Y_END), (hi, ef.HIDDEN_H),
               (0, ef.HIDDEN_H), (0, 0)]
    ax.plot(*zip(*profile), color="black", lw=2)
    inner = [(WALL, WALL), (lo - WALL, WALL), (lo - WALL, ef.FRONT_Y_START + WALL),
             (ef.TOTAL_D - WALL, ef.FRONT_Y_START + WALL), (ef.TOTAL_D - WALL, ef.FRONT_Y_END - WALL),
             (hi - WALL, ef.FRONT_Y_END - WALL), (hi - WALL, ef.HIDDEN_H - WALL),
             (WALL, ef.HIDDEN_H - WALL), (WALL, WALL)]
    ax.plot(*zip(*inner), color="grey", lw=0.8)
    boxes = ef.part_boxes()
    for board in BOARDS:
        colour = COLOURS[board]
        for slab in ef.board_slabs(board):
            rect(ax, slab["z0"], slab["y0"], slab["z1"] - slab["z0"], slab["y1"] - slab["y0"], fc=colour, ec=colour)
        comps = ef.components(board)
        for b in [p for p in boxes if p["board"] == board and p["ref"] in comps]:
            ref = b["ref"]
            z, y, bw, bh = b["z0"], b["y0"], b["z1"] - b["z0"], b["y1"] - b["y0"]
            plug = (board, ref) in ef.PLUG_REFS
            fc = "#c62828" if plug else colour
            rect(ax, z, y, bw, bh, fill=True, fc=fc, alpha=0.35 if plug else 0.18, ec=fc, lw=0.5)
            if ref in LABELLED[board] or bw >= 5 or plug:
                ax.text(z + bw / 2, y + bh / 2, ref, fontsize=5.5, ha="center", va="center", color=fc)
    for p in ef.legs():
        rect(ax, WALL, p["y"] - 2.5, MAIN_TOP_Z - MAIN_THICK - WALL, 5, fc="#6a1b9a", alpha=0.35, ec="#6a1b9a")
    for p in ef.pegs():
        top = ef.BASE_TOP_Z if ef.FRONT_Y_START < p["y"] < ef.FRONT_Y_END else ef.SHOULDER_Z[ef.shoulder_of(p["y"])] - WALL
        rect(ax, MAIN_TOP_Z, p["y"] - 1.5, top - MAIN_TOP_Z, 3, fc="#6a1b9a", alpha=0.35, ec="#6a1b9a")
    for p in ef.standoffs():
        rect(ax, UI_BOTTOM_Z + UI_THICK, p["y"] - 1.5, ef.TOTAL_D - WALL - UI_BOTTOM_Z - UI_THICK, 3,
             fc="#6a1b9a", alpha=0.35, ec="#6a1b9a")
    if not ef.screw_tabs():
        for yc in (14.8, 64.5):
            rect(ax, 39.2, yc - 1.4, lo - WALL - 39.2, 2.8, fc="#6a1b9a", alpha=0.45, ec="#6a1b9a")
    for name, holes in ef.terminal_holes().items():
        size = 10.6 if name == "KNX" else 4.0
        for h in holes:
            face = ef.SHOULDER_Z[ef.shoulder_of(h["y"])]
            ax.plot([face, face], [h["y"] - size / 2, h["y"] + size / 2], color="#ff7043", lw=4)
    for s in ef.wire_entries():
        ax.plot([s["z0"], s["z1"]], [ef.HIDDEN_H, ef.HIDDEN_H], color="#ff7043", lw=5)
    for h in list(ef.button_holes().values()) + ef.led_windows():
        ax.plot([ef.TOTAL_D, ef.TOTAL_D], [h["y"] - 1.75, h["y"] + 1.75], color="#ff7043", lw=4)
    ax.set_xlim(-3, ef.TOTAL_D + 3)
    ax.set_ylim(-3, ef.HIDDEN_H + 3)
    ax.set_aspect("equal")
    ax.set_title("Side section (rail at left, front at right): boards, part heights, KNX plug,\n"
                 "legs, pegs and standoffs (purple), openings (orange)", fontsize=8)
    ax.set_xlabel("z [mm] from rail")
    ax.set_ylabel("y [mm]")
    ax.grid(alpha=0.2)
    return fig


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for name, fig in (("fit_front", front_view()), ("fit_side", side_view())):
        fig.savefig(OUT / f"{name}.svg", bbox_inches="tight")
        print(f"{name} written")


if __name__ == "__main__":
    main()
