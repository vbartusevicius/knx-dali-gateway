"""Legend artwork (outlined text, + / - signs, the DALI bracket) placed on
the case's front faces from the same opening positions the CadQuery cover
uses, kept off the openings and the other labels. legend_color_model.py cuts
it into cover_embossed.stl; on v3 the connector names are engraved into the
base's right side (side_labels). No separate SVG/PDF artwork is written
(user, 2026-11-11).
"""

from dataclasses import dataclass

import build_enclosure as be
import enclosure_features as ef
from matplotlib.path import Path as MplPath
from matplotlib.textpath import TextPath
from matplotlib.transforms import Affine2D

# Smallest legend a powder print (MJF / SLS) keeps legible when recessed
# INLAY_DEPTH deep: 3 mm letters, whose DejaVu Sans Bold strokes are about 0.5 mm.
MIN_TEXT_H = 3.0
MIN_STROKE = 0.4
LABEL_H = 3.0
TITLE_H = 3.0
GAP = 0.8
EDGE = 1.0
LABEL_SPACING = 1.0   # at 3 mm, closer labels read as one word
OPENING_SPACING = 0.3
STROKE_SPACING = 0.3   # bracket lines to text
LED_KEEPOUT = 0.6 if ef.VARIANT in ("v3", "v4") else 0.8   # labels stay this far outside an LED window
FONT = {"family": "DejaVu Sans", "weight": "bold"}


SUPPLY_LABEL = "20-32V"
ROW_PITCH = 1.3
BRACKET_LINE = 0.5
BRACKET_CLEARANCE = 1.0
BRACKET_TITLE_GAP = 0.8
TITLE_CLEARANCE = 1.4
LINE_SPACING = 1.3
SIGN_REFERENCE = "+"
SIGNS = ("+", "\u2212")
SIGN_BAR = 0.6


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
        if self.text in SIGNS:
            return self.sign_path()
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

    def sign_path(self):
        """+ and - as solid bars (the font's are thinner than MIN_STROKE)."""
        half, bar = 0.35 * self.height, max(SIGN_BAR, 0.18 * self.height) / 2
        bars = [(self.x - half, self.y - bar, self.x + half, self.y + bar)]
        if self.text == "+":
            bars.append((self.x - bar, self.y - half, self.x + bar, self.y + half))
        return stroke_path(bars)

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
        if ef.VARIANT in ("v3", "v4"):
            continue
        if name == "KNX":
            x0, x1, y1 = be.knx_recess()
            items.append(("rect", x0, 0.0, x1, y1))
        else:
            r = be.SCREW_HOLE / 2
            items += [("circle", h["x"] - r, h["y"] - r, h["x"] + r, h["y"] + r) for h in holes]
    for ref, h in zip(("SW1", "SW2"), ef.button_holes().values()):
        if be.button_top_z(ref) < ef.TOTAL_D:
            (cx, cy, d), (x0, x1, y0, y1) = be.button_tab_outline(h, be.BUTTON_TAB["slot"])
            items += [("circle", cx - d / 2, cy - d / 2, cx + d / 2, cy + d / 2), ("rect", x0, y0, x1, y1)]
            continue
        r = be.BUTTON_HOLE / 2
        items.append(("circle", h["x"] - r, h["y"] - r, h["x"] + r, h["y"] + r))
    for h in ef.led_windows():
        r = be.LED_WINDOW / 2 + LED_KEEPOUT
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
        if inside(box, face) and not any(overlaps(box, b, OPENING_SPACING) for b in openings_) \
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
    if ef.VARIANT in ("v3", "v4") and main is None:
        return [], []
    else:
        main = main or ef.components("main")
        radius = be.SCREW_HOLE / 2
        labels, names = [], []
        for ref, key, name in (("J2", "aux_dc", SUPPLY_LABEL), ("J3", "dali", "BUS")):
            screws = [ef.main_to_enc(px, py) for px, py, _ in sorted(main[ref]["pads"], key=lambda p: int(p[2]))]
            hole_y = screws[0][1]
            pin_y = hole_y - radius - GAP - LABEL_H / 2
            pins = [lb.replace("AUX", "").replace("-", "\u2212") or lb for lb in terminals[key]["labels"]]
            if key != "dali":   # DALI is not polarised: the pair name says it all
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
    strokes = dali_terminals()[1]
    return [stroke_path(strokes)] if strokes else []


# The 18 mm V3 face has no room for words at MIN_TEXT_H, and the flat buttons
# sit right under the OLED window, so the front carries no legend: the finger
# bumps tell the buttons apart, plain on SCROLL (left) and with a recessed X
# on RESET (right) (build_enclosure.BUTTON_MARK). On both variants the LED row (STATUS, KNX,
# DALI from left to right) is unmarked; the release READMEs explain both. The
# V3 connector names sit on the right side face next to their connectors
# (side_labels), engraved into the base.
SIDE_GAP = 1.5


def v3_labels():
    return []


def side_to_enc(u, v):
    """Right side face (x = WIDTH / 2), seen from the right with the device
    upright: u runs from the front towards the rail (-z), v up (+y)."""
    return -u, v


def side_labels():
    """V3 connector names on the right side face: KNX and AUX above their
    plug pockets, DALI level with the DALI terminal near the top. Label
    coordinates are (u, v) = (-z, y)."""
    if ef.VARIANT not in ("v3", "v4"):
        return []
    above_pockets = be.plug_block().val().BoundingBox().ymax + SIDE_GAP + LABEL_H / 2
    labels = [Label(name, -(p["z0"] + p["z1"]) / 2, above_pockets, LABEL_H)
              for name, p in (("KNX", be.plug_pocket("J1")), ("AUX", be.plug_pocket("J2")))]
    j3 = next(b for b in ef.part_boxes("body") if (b["board"], b["ref"]) == ("main", "J3"))
    labels.append(Label("DALI", -(j3["z0"] + j3["z1"]) / 2, (j3["y0"] + j3["y1"]) / 2, LABEL_H))
    return labels


def labels():
    if ef.VARIANT in ("v3", "v4"):
        return v3_labels()
    terminals = ef.SPEC["terminals"]
    main = ef.components("main")
    placed, _ = dali_terminals(terminals, main)
    x0, x1, y1 = be.knx_recess()
    # no +/- over the KNX plug: its coding key lets it in only one way round
    place("KNX", (x0, y1 - 2, x1, y1), TITLE_H, placed, prefer=("above", "left", "right"))
    buttons = ef.button_holes()
    for name, text in (("PROG", "SCROLL"), ("RESET", "RESET")):
        place(text, hole_box(buttons[name], be.BUTTON_HOLE), LABEL_H, placed,
              prefer=("above", "below", "left", "right"))
    return placed
