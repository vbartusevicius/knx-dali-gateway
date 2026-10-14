"""Front legend artwork for printing onto the cover (UV/pad print) or as an
adhesive label, at 1:1 in the case's front view.

    python3 mechanical/scripts/front_legend.py

Writes mechanical/build/<variant>/front_legend.svg/.pdf (1:1). The front view
shows three flat faces: the bottom shoulder (KNX plug), the front protrusion
(display, buttons, LEDs) and the top shoulder (DALI and AUX screws). Every
label is placed from the same opening positions the CadQuery cover uses and
kept off the openings and the other labels. Text is converted to outlines.
Group "print" is the artwork; group "guides" (face edges, openings, cyan) is
for positioning only and must not be printed.
"""

from dataclasses import dataclass
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import build_enclosure as be
import enclosure_features as ef
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, PathPatch, Rectangle
from matplotlib.path import Path as MplPath
from matplotlib.textpath import TextPath
from matplotlib.transforms import Affine2D

REPO = Path(__file__).resolve().parents[2]
OUT = REPO / "mechanical/build" / ef.VARIANT
LABEL_H = 1.8
LED_LABEL_H = (1.4, 1.2, 1.0, 0.85)
TITLE_H = 2.2
GAP = 0.8
EDGE = 1.0
LABEL_SPACING = 0.3
LED_LABEL_SPACING = 1.5
INK = "#000000"
GUIDE = "#00acc1"
LED_RING = 0.25 if ef.VARIANT == "v3" else 0.8
RING_LINE = 0.25 if ef.VARIANT == "v3" else 0.4
FONT = {"family": "DejaVu Sans", "weight": "bold"}


SUPPLY_LABEL = "PSU 20-32V"
ROW_PITCH = 1.5
BRACKET_LINE = 0.3
BRACKET_CLEARANCE = 1.2
BRACKET_TITLE_GAP = 0.8
TITLE_CLEARANCE = 1.4
LINE_SPACING = 1.3
SIGN_REFERENCE = "+"


@dataclass
class Label:
    """Outlined text centred on (x, y); '\\n' stacks centred lines. Single
    characters such as + and - are centred on the '+' glyph so signs side by
    side share one axis instead of each being centred on its own ink."""
    text: str
    x: float
    y: float
    height: float

    def path(self):
        lines = self.text.split("\n")
        parts = []
        for i, line in enumerate(lines):
            raw = TextPath((0, 0), line, size=self.height, prop=FONT)
            ext = raw.get_extents()
            vertical = TextPath((0, 0), SIGN_REFERENCE, size=self.height, prop=FONT).get_extents() \
                if len(line) == 1 else ext
            dy = (len(lines) - 1) / 2 * self.height * LINE_SPACING - i * self.height * LINE_SPACING
            parts.append(raw.transformed(Affine2D().translate(self.x - ext.x0 - ext.width / 2,
                                                              self.y + dy - vertical.y0 - vertical.height / 2)))
        return MplPath.make_compound_path(*parts)

    def box(self):
        ext = self.path().get_extents()
        return ext.x0, ext.y0, ext.x1, ext.y1


def faces():
    half = ef.WIDTH / 2
    return {"bottom": (-half, 0.0, half, ef.FRONT_Y_START),
            "front": (-half, ef.FRONT_Y_START, half, ef.FRONT_Y_END),
            "top": (-half, ef.FRONT_Y_END, half, ef.HIDDEN_H)}


def openings():
    """Every opening in the front view as a box, with its kind for drawing."""
    items = []
    for name, holes in ef.terminal_holes().items():
        if name in ("KNX", "AUX") and ef.VARIANT == "v3":
            continue
        if name == "KNX":
            x0, x1, y1 = be.knx_recess()
            items.append(("rect", x0, 0.0, x1, y1))
        else:
            r = be.SCREW_HOLE / 2
            items += [("circle", h["x"] - r, h["y"] - r, h["x"] + r, h["y"] + r) for h in holes]
    for h in ef.button_holes().values():
        r = be.BUTTON_HOLE / 2
        items.append(("circle", h["x"] - r, h["y"] - r, h["x"] + r, h["y"] + r))
    for h in ef.led_windows():
        r = be.LED_WINDOW / 2 + LED_RING
        items.append(("led", h["x"] - r, h["y"] - r, h["x"] + r, h["y"] + r))
    win = ef.oled_window()
    items.append(("rect", win["cx"] - win["w"] / 2, win["cy"] - win["h"] / 2,
                  win["cx"] + win["w"] / 2, win["cy"] + win["h"] / 2))
    return items


def overlaps(a, b, margin=0.0):
    return a[0] < b[2] + margin and b[0] < a[2] + margin and a[1] < b[3] + margin and b[1] < a[3] + margin


def inside(box, face, margin=EDGE):
    return face[0] + margin <= box[0] and box[2] <= face[2] - margin and \
        face[1] + margin <= box[1] and box[3] <= face[3] - margin


def face_of(y):
    return next(name for name, f in faces().items() if f[1] <= y <= f[3])


def place(text, anchor, height, placed, prefer=("below", "above", "left", "right"), spacing=LABEL_SPACING):
    """First position next to the anchor box that stays on its face, off every
    opening and at least `spacing` away from the labels placed so far."""
    x0, y0, x1, y1 = anchor
    face = faces()[face_of((y0 + y1) / 2)]
    openings_ = [o[1:] for o in openings()]
    probe = Label(text if len(text) > 1 else SIGN_REFERENCE, 0, 0, height)
    w, h = probe.box()[2] - probe.box()[0], probe.box()[3] - probe.box()[1]
    centres = {"below": ((x0 + x1) / 2, y0 - GAP - h / 2), "above": ((x0 + x1) / 2, y1 + GAP + h / 2),
               "left": (x0 - GAP - w / 2, (y0 + y1) / 2), "right": (x1 + GAP + w / 2, (y0 + y1) / 2)}
    for side in prefer:
        label = Label(text, *centres[side], height)
        box = label.box()
        if inside(box, face) and not any(overlaps(box, b, LABEL_SPACING) for b in openings_) \
                and not any(overlaps(box, lb.box(), spacing) for lb in placed):
            placed.append(label)
            return label
    raise ValueError(f"no room for label {text!r} on the {face_of((y0 + y1) / 2)} face")


def hole_box(h, diameter):
    r = diameter / 2
    return h["x"] - r, h["y"] - r, h["x"] + r, h["y"] + r


def union(boxes):
    return min(b[0] for b in boxes), min(b[1] for b in boxes), max(b[2] for b in boxes), max(b[3] for b in boxes)


def dali_terminals(terminals=None, main=None):
    """Top shoulder in fixed rows: pin names under the screws, the pair names
    (DALI bus supply input, bus) centred under each pair, and one bracket
    around both pairs with DALI set into its bar, so the supply reads as the
    DALI bus supply, not the device's power. Returns the labels and the
    bracket's strokes as boxes."""
    terminals = terminals or ef.SPEC["terminals"]
    if ef.VARIANT == "v3" and main is None:
        radius = be.SCREW_HOLE / 2
        screws = [(h["x"], h["y"]) for h in ef.terminal_holes()["DALI"]]
        pin_y = screws[0][1] - radius - GAP - LABEL_H / 2
        labels = [Label("DA", x, pin_y, LABEL_H) for x, _ in screws]
        names = [Label("BUS", sum(x for x, _ in screws) / 2, pin_y - LABEL_H * ROW_PITCH, LABEL_H)]
    else:
        main = main or ef.components("main")
        radius = be.SCREW_HOLE / 2
        labels, names = [], []
        for ref, key, name in (("J2", "aux_dc", SUPPLY_LABEL), ("J3", "dali", "BUS")):
            screws = [ef.main_to_enc(px, py) for px, py, _ in sorted(main[ref]["pads"], key=lambda p: int(p[2]))]
            hole_y = screws[0][1]
            pin_y = hole_y - radius - GAP - LABEL_H / 2
            pins = [lb.replace("AUX", "").replace("-", "\u2212") or lb for lb in terminals[key]["labels"]]
            labels += [Label(pin, x, pin_y, LABEL_H) for (x, _), pin in zip(screws, pins)]
            names.append(Label(name, sum(x for x, _ in screws) / 2, pin_y - LABEL_H * ROW_PITCH, LABEL_H))
    labels += names
    text = union([lb.box() for lb in labels])
    left, right = text[0] - BRACKET_CLEARANCE, text[2] + BRACKET_CLEARANCE
    bar = text[1] - BRACKET_CLEARANCE
    title = Label("DALI", (left + right) / 2, bar, LABEL_H)
    above = [lb.box() for lb in labels if lb.box()[0] < title.box()[2] and title.box()[0] < lb.box()[2]]
    if above:
        bar = min(bar, min(b[1] for b in above) - TITLE_CLEARANCE - (title.box()[3] - title.box()[1]) / 2)
        title = Label("DALI", (left + right) / 2, bar, LABEL_H)
    leg_top = union([n.box() for n in names])[3]
    gap0, gap1 = title.box()[0] - BRACKET_TITLE_GAP, title.box()[2] + BRACKET_TITLE_GAP
    half = BRACKET_LINE / 2
    strokes = [(left - half, bar - half, gap0, bar + half), (gap1, bar - half, right + half, bar + half),
               (left - half, bar - half, left + half, leg_top), (right - half, bar - half, right + half, leg_top)]
    return [*labels, title], strokes


def stroke_path(boxes):
    return MplPath.make_compound_path(*(MplPath.unit_rectangle().transformed(
        Affine2D().scale(x1 - x0, y1 - y0).translate(x0, y0)) for x0, y0, x1, y1 in boxes))


def marks():
    """Printed shapes other than text."""
    return [stroke_path(dali_terminals()[1])]


def v3_labels():
    placed, _ = dali_terminals()
    for x, text in ((-2.875, "\u2212"), (2.875, "+")):
        placed.append(Label(text, x, 3.0, TITLE_H))
    placed.append(Label("KNX", 0.0, 6.8, TITLE_H))
    placed.append(Label(SUPPLY_LABEL, 0.0, 11.0, LABEL_H))
    placed.append(Label("PRG/RST", -4.4, 60.0, 1.4))
    placed.append(Label("FUNC", 4.4, 60.0, 1.4))
    for text, x in (("STATUS", -5.2), ("KNX", 0.4), ("DALI", 5.6)):
        placed.append(Label(text, x, 57.3, 0.85))
    return placed


def labels():
    if ef.VARIANT == "v3":
        return v3_labels()
    terminals = ef.SPEC["terminals"]
    main = ef.components("main")
    placed, _ = dali_terminals(terminals, main)
    x0, x1, y1 = be.knx_recess()
    knx_pins = sorted(main["J1"]["pads"], key=lambda p: int(p[2]))
    for (px, _, _), text in zip(knx_pins, ("+", "\u2212")):
        x = ef.main_to_enc(px, 0)[0]
        place(text, (x - 1, y1 - 2, x + 1, y1), TITLE_H, placed, prefer=("above",))
    place("KNX", (x0, y1, x1, max(lb.box()[3] for lb in placed[-2:])), TITLE_H, placed,
          prefer=("above", "left", "right"))
    buttons = ef.button_holes()
    for name, text in (("PROG", "PRG/RST"), ("RESET", "FUNC")):
        place(text, hole_box(buttons[name], be.BUTTON_HOLE), LABEL_H, placed,
              prefer=("above", "below", "left", "right"))
    leds = {led["ref"]: led["id"].upper() for led in ef.SPEC["front_ui"]["leds"]}
    windows = [(leds[ref], hole_box(w, be.LED_WINDOW + 2 * LED_RING)) for ref, w in zip(("D1", "D2", "D3"), ef.led_windows())]
    for height in LED_LABEL_H:
        for side in ("below", "above"):
            row = list(placed)
            try:
                for text, box in windows:
                    place(text, box, height, row, prefer=(side,), spacing=LED_LABEL_SPACING)
            except ValueError:
                continue
            return row
    raise ValueError("no room for one row of LED labels")


def draw(ax, placed):
    for name, (x0, y0, x1, y1) in faces().items():
        ax.add_patch(Rectangle((x0, y0), x1 - x0, y1 - y0, fill=False, ec=GUIDE, lw=0.3, gid=f"guides-face-{name}"))
    for kind, x0, y0, x1, y1 in openings():
        if kind == "rect":
            ax.add_patch(Rectangle((x0, y0), x1 - x0, y1 - y0, fill=False, ec=GUIDE, lw=0.3, gid="guides-opening"))
        else:
            r = be.LED_WINDOW / 2 if kind == "led" else (x1 - x0) / 2
            ax.add_patch(Circle(((x0 + x1) / 2, (y0 + y1) / 2), r, fill=False, ec=GUIDE, lw=0.3,
                                gid="guides-opening"))
    for window in ef.led_windows():
        ax.add_patch(Circle((window["x"], window["y"]), be.LED_WINDOW / 2 + LED_RING - RING_LINE / 2, fill=False,
                            ec=INK, lw=RING_LINE * 72 / 25.4, gid="print-led-ring"))
    for label in placed:
        ax.add_patch(PathPatch(label.path(), fc=INK, ec="none", gid="print-text"))
    for mark in marks():
        ax.add_patch(PathPatch(mark, fc=INK, ec="none", gid="print-bracket"))


def render(placed):
    margin = 6.0
    width, height = ef.WIDTH + 2 * margin, ef.HIDDEN_H + 2 * margin + 8
    fig = plt.figure(figsize=(width / 25.4, height / 25.4))
    ax = fig.add_axes((0, 0, 1, 1))
    ax.set_xlim(-ef.WIDTH / 2 - margin, ef.WIDTH / 2 + margin)
    ax.set_ylim(-margin, ef.HIDDEN_H + margin + 8)
    ax.set_aspect("equal")
    ax.axis("off")
    draw(ax, placed)
    ax.plot([-ef.WIDTH / 2, -ef.WIDTH / 2 + 10], [-margin / 2] * 2, color=GUIDE, lw=0.5, gid="guides-scale")
    ax.text(-ef.WIDTH / 2 + 11, -margin / 2, "10 mm (1:1); cyan = guides, not printed", fontsize=4,
            va="center", color=GUIDE, gid="guides-note")
    ax.text(0, ef.HIDDEN_H + margin / 2 + 4, f"KNX-DALI gateway {ef.VARIANT} front legend, front view",
            fontsize=5, ha="center", color=GUIDE, gid="guides-title")
    OUT.mkdir(parents=True, exist_ok=True)
    plt.rcParams["svg.fonttype"] = "path"
    for suffix in ("svg", "pdf"):
        fig.savefig(OUT / f"front_legend.{suffix}")
    plt.close(fig)


def main():
    render(labels())
    print(f"front_legend written to {OUT.relative_to(REPO)}")


if __name__ == "__main__":
    main()
