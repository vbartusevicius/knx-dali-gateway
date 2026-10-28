"""Transfer maps and checklist for moving parts from a rev C v3 main board
(ordered from commit 59eea1f) onto a rev D main board (`eda` container).

    python3 hardware/scripts/rework_maps.py

Writes hardware/docs/rework-v3-rev-d/: old_front.png / old_back.png (what to
take off the rev C board), new_front.png / new_back.png (where everything goes
on rev D) and transfer.csv (one row per rev D part: move or buy, side, method).
A part moves when its reference, value and footprint are identical on both
boards; everything else is bought new.
"""

import csv
import subprocess
import tempfile
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pcbnew
from matplotlib import patches

REPO = Path("/work")
OLD_REV = "59eea1f"
LAYOUT = "hardware/layout/v3/main/main.kicad_pcb"
OUT = REPO / "hardware/docs/rework-v3-rev-d"

MOVE, BUY, LEAVE, HAND = "#2e9d4f", "#e8891a", "#c0392b", "#3a6fd1"
LEGEND = {
    "old": [(MOVE, "take off, moves to rev D"), (HAND, "hand-fit part, moves to rev D"),
            (LEAVE, "changed on rev D: leave on the old board")],
    "new": [(MOVE, "from the rev C board"), (HAND, "hand-fit part from the rev C board"),
            (BUY, "new part: buy")],
}
FINE_PITCH = ("QFN", "VQFN", "SC-70", "SOT-363", "TSOT")
HAND_FIT = {"U3", "U8", "J1", "J2"}


def load(rev):
    if rev is None:
        return pcbnew.LoadBoard(str(REPO / LAYOUT))
    text = subprocess.run(["git", "-C", str(REPO), "show", f"{rev}:{LAYOUT}"],
                          check=True, capture_output=True, text=True).stdout
    path = Path(tempfile.mkdtemp()) / "main.kicad_pcb"
    path.write_text(text)
    return pcbnew.LoadBoard(str(path))


def parts(board):
    out = {}
    for fp in board.GetFootprints():
        out[fp.GetReference()] = fp
    return out


def key(fp):
    return fp.GetValue(), fp.GetFPIDAsString().split(":")[-1]


def method(fp):
    name = fp.GetFPIDAsString().split(":")[-1]
    if not fp.GetAttributes() & pcbnew.FP_SMD:
        return "through-hole: desoldering pump or braid, both sides"
    if any(tag in name for tag in FINE_PITCH):
        return "hot air 300-330 C with flux; clean and re-tin pads; check bridges under magnification"
    if any(tag in name for tag in ("TO-252", "CP_Elec", "L_Sunlord", "L_APV", "Fuse_1812", "R_2512")):
        return "hot air, large nozzle (big thermal mass); lift straight up"
    if any(tag in name for tag in ("SO-6", "SOT-23", "D_SMA", "D_SOD", "Diode_Bridge", "Crystal")):
        return "hot air or two irons; tweezers"
    return "two irons or hot air; tweezers (or buy new: cents per part)"


def draw(board, colours, title, path, side, kind):
    bbox = board.GetBoardEdgesBoundingBox()
    x0, y0 = pcbnew.ToMM(bbox.GetX()), pcbnew.ToMM(bbox.GetY())
    w, h = pcbnew.ToMM(bbox.GetWidth()), pcbnew.ToMM(bbox.GetHeight())
    fig, ax = plt.subplots(figsize=(w / 6 + 2, h / 6 + 1.5), dpi=160)
    for drawing in board.GetDrawings():
        if drawing.GetLayer() != pcbnew.Edge_Cuts:
            continue
        if drawing.GetShape() == pcbnew.SHAPE_T_SEGMENT:
            s, e = drawing.GetStart(), drawing.GetEnd()
            ax.plot([pcbnew.ToMM(s.x), pcbnew.ToMM(e.x)], [pcbnew.ToMM(s.y), pcbnew.ToMM(e.y)], color="black", lw=1.2)
        elif drawing.GetShape() == pcbnew.SHAPE_T_POLY:
            pts = [(pcbnew.ToMM(p.x), pcbnew.ToMM(p.y)) for p in drawing.GetPolyShape().COutline(0).CPoints()]
            ax.add_patch(patches.Polygon(pts, closed=True, fill=False, color="black", lw=1.2))
    # Hand-fit parts (XIAO, REC3, pin strips) are large and sit over SMD parts:
    # outline only, label at their top edge, drawn first.
    ordered = sorted(parts(board).items(), key=lambda item: item[0] not in HAND_FIT)
    for ref, fp in ordered:
        if fp.IsFlipped() != (side == "back") or ref not in colours:
            continue
        box = fp.GetBoundingBox(False, False)
        bx, by = pcbnew.ToMM(box.GetX()), pcbnew.ToMM(box.GetY())
        bw, bh = pcbnew.ToMM(box.GetWidth()), pcbnew.ToMM(box.GetHeight())
        if ref in HAND_FIT:
            ax.add_patch(patches.Rectangle((bx, by), bw, bh, fill=False, edgecolor=colours[ref], lw=1.6, ls="--"))
            ax.text(bx + bw / 2, by + 1.2, ref, ha="center", va="top", fontsize=7, weight="bold", color=colours[ref])
            continue
        ax.add_patch(patches.Rectangle((bx, by), bw, bh, facecolor=colours[ref], edgecolor="black",
                                       lw=0.4, alpha=0.8))
        ax.text(bx + bw / 2, by + bh / 2, ref, ha="center", va="center", fontsize=5.5, weight="bold", color="white")
    ax.set_xlim(x0 - 1, x0 + w + 1)
    ax.set_ylim(y0 + h + 1, y0 - 1)
    if side == "back":
        ax.invert_xaxis()
    ax.set_aspect("equal")
    ax.axis("off")
    ax.legend(handles=[patches.Patch(color=c, label=label) for c, label in LEGEND[kind]],
              loc="upper center", bbox_to_anchor=(0.5, -0.01), fontsize=6, frameon=False)
    ax.set_title(f"{title}, {side} side{' (seen from the back, mirrored)' if side == 'back' else ''}", fontsize=8)
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)


def main():
    old, new = load(OLD_REV), load(None)
    old_parts, new_parts = parts(old), parts(new)
    moves = {ref for ref, fp in new_parts.items() if ref in old_parts and key(old_parts[ref]) == key(fp)}
    old_colours = {ref: (HAND if ref in HAND_FIT else MOVE) if ref in moves else LEAVE for ref in old_parts}
    new_colours = {ref: (HAND if ref in HAND_FIT else MOVE) if ref in moves else BUY for ref in new_parts}
    OUT.mkdir(parents=True, exist_ok=True)
    for side in ("front", "back"):
        draw(old, old_colours, "Rev C main board (ordered)", OUT / f"old_{side}.png", side, "old")
        draw(new, new_colours, "Rev D main board (new)", OUT / f"new_{side}.png", side, "new")
    with open(OUT / "transfer.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Ref", "Value", "Footprint", "Action", "Rev C side", "Rev D side", "Method"])
        for ref in sorted(new_parts, key=lambda r: (r.rstrip("0123456789"), int("".join(c for c in r if c.isdigit()) or 0))):
            fp = new_parts[ref]
            side = lambda part: "back" if part.IsFlipped() else "front"
            value, footprint = key(fp)
            if ref in moves:
                w.writerow([ref, value, footprint, "move", side(old_parts[ref]), side(fp), method(fp)])
            else:
                w.writerow([ref, value, footprint, "buy", "-", side(fp), method(fp)])
    print(f"{len(moves)} parts move, {len(new_parts) - len(moves)} to buy; maps in {OUT.relative_to(REPO)}")


if __name__ == "__main__":
    main()
