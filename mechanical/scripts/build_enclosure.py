"""CadQuery enclosure for the POC gateway (3 DIN modules), two printed parts.

base   rail side, z -5..shoulder: a TS35 channel in the 5 mm deep back
       (EN 60715: 35 +-0.3 between lips, 1 mm lips) with a fixed hook under
       the top lip and a sprung sliding latch under the bottom lip, released
       by levering a flat screwdriver in the slot at the bottom edge; four
       walls open towards the front; legs carry the main board.
cover  front side: shoulder plates (KNX opening, terminal screw holes) and the
       front protrusion (OLED window, button holes, LED windows: a thin skin
       over each LED inside a baffle tube); pegs clamp the
       main board onto the legs; two screw tabs take the case screws; the UI
       board snaps in from behind past four sprung hooks in the protrusion's
       side walls and rests against four standoffs.

Assembly: drop the main board into the base, plug the UI cable, snap the UI
board into the cover, close, two 2.5 x 10 thread-forming screws through the
top and bottom walls (the wire sides) into the cover's screw tabs.
Coordinates: x centred, y up from the bottom, z from the rail's front face.
"""

import math
import sys
from pathlib import Path

import cadquery as cq

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "mechanical/scripts"))
from enclosure_features import (
    BASE_TOP_Z,
    FRONT_Y_END,
    FRONT_Y_START,
    HIDDEN_H,
    MIN_SKIN,
    PART_AIR,
    SHOULDER_Z,
    SPEC,
    TOTAL_D,
    VARIANT,
    WIDTH,
    board_point,
    board_slab,
    button_holes,
    components,
    front_posts,
    horizontal_capacitor,
    knx_plug,
    led_windows,
    legs,
    main_ribs,
    oled_glass,
    oled_module,
    oled_panel,
    oled_window,
    part_boxes,
    pegs,
    screw_tabs,
    shoulder_of,
    standoffs,
    terminal_holes,
    vertical_main,
    wire_entries,
)
from ui_stackup import compute_ui_stackup

WALL = SPEC["enclosure"]["wall"]
MAIN_Z = SPEC["boards"]["main"].get("z", 21.2)
MAIN_THICK = SPEC["boards"]["main"]["thickness"]
MAIN_BOTTOM_Z = MAIN_Z - MAIN_THICK
STACK = compute_ui_stackup()
UI_BOARD_Z = STACK.board_z
UI_BOARD_TOP_Z = STACK.board_top_z
UI_W = SPEC["boards"]["ui"]["max_outline"]["width"]

INNER_W = WIDTH - 2 * WALL
INNER_H = HIDDEN_H - 2 * WALL
FRONT_INNER_Z = TOTAL_D - WALL

LEG_SIZE = 5.0
PEG_DIAMETER = 3.0
STANDOFF_DIAMETER = 3.0
LED_WINDOW = 2.2 if VARIANT == "v3" else 3.0
BAFFLE_WALL = 0.4 if VARIANT == "v3" else 0.8
BUTTON_HOLE = 2.6 if VARIANT == "v3" else 4.0
SCREW_HOLE = 3.2 if VARIANT == "v3" else 4.0
CASE_SCREW = {"pilot": 2.2, "clearance": 2.8, "depth": 8.5, "z": BASE_TOP_Z - 2.0}
SCREW_TAB = {"width": 4.0 if VARIANT == "v3" else 5.0, "depth": 8.5,
             "z0": 39.85 if VARIANT == "v3" else MAIN_Z + 2.0 + SPEC["checks"]["component_air_gap"]}
LIP = {"thickness": 1.2, "depth": 2.0}
SNAP = {"overlap": 0.5, "width": 6.0, "slot": 0.5, "hook_height": 1.8, "free_end_below_board": 1.8,
        "y": (FRONT_Y_START + 12, FRONT_Y_END - 12)}

BOARD_EDGE_CLEARANCE = 0.2
RIB = {"half_y": 0.4, "half_z": 1.5, "gap": 0.1}
BUTTON_TAB = {"pad": 2.6, "arm": 2.0, "root_x": 0.9, "slot": 0.45, "thickness": 0.7, "nub": 1.4,
              "nub_gap": 0.1, "boss": 2.0, "boss_height": 0.6, "boss_round": 0.25}

RAIL = {"centre": HIDDEN_H / 2, "slot": 35.4, "lip": 1.0, "clearance": 0.1}
BACK_DEPTH = 5.0
HOOK_REACH = 1.5
MIN_PRINT_GAP = 0.6       # MJF/SLS: narrower gaps between separate features fuse
LATCH_MAX_STRAIN = 0.025  # PA12 / PETG flexure, repeated use
LATCH = {"half_width": 1.8 if WIDTH < 30 else 5.0, "tab_half_width": 3.0 if WIDTH < 30 else 5.0, "gap": 0.7,
         "tab_bottom": -6.0, "beam": 0.6, "y0": 3.0, "skin": 1.0, "hole": (3.0, 1.6), "hole_y": -4.0,
         "travel": HOOK_REACH + 0.3}
LATCH["reach"] = min(16.0, WIDTH / 2 - LATCH["skin"] - LATCH["gap"])


def latch_leg_length(latch=LATCH):
    """Free length of one spring leg between its two turns."""
    return latch["reach"] - (latch["half_width"] + latch["gap"]) - 2 * latch["beam"]


def latch_strain(legs, latch=LATCH):
    """Peak bending strain of a fixed-guided leg when the bar travels its full
    release travel, shared equally by `legs` legs in series: 3 t d / L^2."""
    return 3 * latch["beam"] * (latch["travel"] / legs) / latch_leg_length(latch) ** 2


LATCH["legs"] = next(n for n in range(1, 40, 2) if latch_strain(n) <= LATCH_MAX_STRAIN)


def box(x0, x1, y0, y1, z0, z1):
    return cq.Workplane("XY").box(x1 - x0, y1 - y0, z1 - z0, centered=False).translate((x0, y0, z0))


def cylinder(x, y, z0, z1, diameter):
    return cq.Workplane("XY").circle(diameter / 2).extrude(z1 - z0).translate((x, y, z0))


def capacitor_body(bounds):
    cx = (bounds["x0"] + bounds["x1"]) / 2
    cz = (bounds["z0"] + bounds["z1"]) / 2
    radius = (bounds["x1"] - bounds["x0"]) / 2
    return (cq.Workplane("XZ").center(cx, cz).circle(radius)
            .extrude(bounds["y1"] - bounds["y0"]).translate((0, bounds["y1"], 0)))


def capacitor_cradles():
    cap = horizontal_capacitor()
    if cap is None:
        return []
    cx, cz = (cap["x0"] + cap["x1"]) / 2, (cap["z0"] + cap["z1"]) / 2
    radius = (cap["x1"] - cap["x0"]) / 2
    result = []
    for y in (cap["y0"] + 4, cap["y1"] - 4):
        body = box(cap["x0"] - 1.5, cap["x1"] + 1.5, y - 1, y + 1, WALL - 0.1, cz + 0.5)
        cavity = cq.Workplane("XZ").center(cx, cz).circle(radius).extrude(4).translate((0, y + 2, 0))
        result.append(body.cut(cavity))
    return result


def rail_edges():
    return RAIL["centre"] - RAIL["slot"] / 2, RAIL["centre"] + RAIL["slot"] / 2


def lip_z():
    """z band behind a rail lip, where hooks reach."""
    top = -(RAIL["lip"] + RAIL["clearance"])
    return top - 1.5, top


def rail_blocks():
    """Back blocks above and below the rail channel, with the fixed hook
    under the rail's top lip; hook the device on here first."""
    low, high = rail_edges()
    back = -BACK_DEPTH
    blocks = box(-WIDTH / 2, WIDTH / 2, 0, low, back, 0).union(box(-WIDTH / 2, WIDTH / 2, high, HIDDEN_H, back, 0))
    reach = min(15.0, WIDTH / 2 - 1.0)
    hook = box(-reach, reach, high - HOOK_REACH, high, *lip_z())
    return blocks.union(hook)


def latch_parts():
    """Sliding latch under the rail's bottom lip. A bar carries the lip at its
    top; its lower end is a wider tab below the case with a hole facing the
    front for a flat screwdriver. On each side of the bar a serpentine spring
    of LATCH["legs"] legs in series (enough that no leg bends past
    LATCH_MAX_STRAIN over the release travel) springs it back. Every gap is at
    least MIN_PRINT_GAP so powder prints do not fuse the latch to the case."""
    low, _ = rail_edges()
    back = -BACK_DEPTH
    w, g, b, n = LATCH["half_width"], LATCH["gap"], LATCH["beam"], LATCH["legs"]
    reach, pitch, y0 = LATCH["reach"], b + g, LATCH["y0"]
    y_last = y0 + (n - 1) * pitch
    pocket = box(-w - g, w + g, LATCH["tab_bottom"] - 1, low + 1, back - 1, 0).union(
        box(-reach - g, reach + g, y0 - LATCH["travel"] - g, y_last + b + g, back - 1, 0))
    bar_z = (back, -g)
    tw = LATCH["tab_half_width"]
    latch = box(-w, w, LATCH["tab_bottom"], low - g, *bar_z).union(
        box(-tw, tw, LATCH["tab_bottom"], -g, *bar_z))
    lip = box(-w, w, low - g, low + HOOK_REACH, *lip_z())
    latch = latch.union(lip.edges("|X and <Z and >Y").chamfer(1.0))
    x_in = w + g
    for s in (-1, 1):
        for i in range(n):
            y = y0 + i * pitch
            start = w if i == 0 else x_in
            end = reach + g + 0.5 if i == n - 1 else reach
            latch = latch.union(box(*sorted((s * start, s * end)), y, y + b, *bar_z))
            if i < n - 1:
                x_turn = (reach - b, reach) if i % 2 == 0 else (x_in, x_in + b)
                latch = latch.union(box(*sorted((s * x_turn[0], s * x_turn[1])), y, y + pitch + b, *bar_z))
    hole_w, hole_h = LATCH["hole"]
    hole = box(-hole_w / 2, hole_w / 2, LATCH["hole_y"] - hole_h / 2, LATCH["hole_y"] + hole_h / 2, back - 1, 1)
    return pocket, latch.cut(hole)


def leg_bodies():
    h = LEG_SIZE / 2
    return [box(p["x"] - h, p["x"] + h, p["y"] - h, p["y"] + h, WALL, MAIN_BOTTOM_Z) for p in legs()]


def screw_axis_y(tab):
    """Outer face, inner face of the wall and inner end of the tab, along y."""
    if tab["side"] == "top":
        return HIDDEN_H, HIDDEN_H - WALL, HIDDEN_H - WALL - SCREW_TAB["depth"]
    return 0.0, WALL, WALL + SCREW_TAB["depth"]


def plate_inner(tab):
    return SHOULDER_Z[tab["side"]] - WALL


SIDE_SNAPS = {"right_y": (16.8, 63.3), "left_z": ((15.0, 21.0), (33.0, 39.0)), "half": 1.4}


def shoulder_snap_bodies():
    """On V3 the base is open on the left side (-x) so the vertical main board
    slides in from the left; the cover carries the left side wall and clicks
    onto the base with six snap hooks: two on the right side wall (clear of
    the plug block and the DALI terminal) and two pairs on the left edge
    of the top and bottom walls, so the cover cannot pivot about one edge."""
    if screw_tabs():
        return []
    bodies = []
    half = SIDE_SNAPS["half"]
    for y_c in SIDE_SNAPS["right_y"]:
        tongue = box(INNER_W / 2 - 1.1, INNER_W / 2, y_c - half, y_c + half, 39.2, BASE_TOP_Z + 0.4)
        barb = box(INNER_W / 2 - 1.1, INNER_W / 2 + 0.5, y_c - half, y_c + half, 39.2, 40.4)
        bodies.append(tongue.union(barb))
    for y_wall, d in ((WALL, 1), (HIDDEN_H - WALL, -1)):
        y0, y1 = sorted((y_wall, y_wall + d * 1.1))
        yb0, yb1 = sorted((y_wall - d * 0.5, y_wall + d * 1.1))
        for z0, z1 in SIDE_SNAPS["left_z"]:
            tongue = box(-WIDTH / 2, -INNER_W / 2 + 1.2, y0, y1, z0, z1)
            barb = box(-INNER_W / 2, -INNER_W / 2 + 1.2, yb0, yb1, z0, z1)
            bodies.append(tongue.union(barb))
    return bodies


def shoulder_snap_keepers():
    """Matching keeper recesses in the V3 base: right side wall and the left
    rims of the top and bottom walls."""
    if screw_tabs():
        return []
    half = SIDE_SNAPS["half"] + 0.2
    keepers = [box(INNER_W / 2 - 0.1, INNER_W / 2 + 0.6, y_c - half, y_c + half, 39.0, 40.6)
               for y_c in SIDE_SNAPS["right_y"]]
    for z0, z1 in SIDE_SNAPS["left_z"]:
        keepers += [box(-INNER_W / 2 - 0.1, -INNER_W / 2 + 1.3, WALL - 0.6, WALL + 0.1, z0 - 0.2, z1 + 0.2),
                    box(-INNER_W / 2 - 0.1, -INNER_W / 2 + 1.3, HIDDEN_H - WALL - 0.1, HIDDEN_H - WALL + 0.6,
                        z0 - 0.2, z1 + 0.2)]
    return keepers


def screw_tab_bodies():
    """Tabs hanging from the shoulder plates behind the top and bottom walls,
    ending above the board's low parts with air to spare; they take the case
    screws."""
    bodies = list(shoulder_snap_bodies())
    for tab in screw_tabs():
        _, wall_in, end = screw_axis_y(tab)
        half = SCREW_TAB["width"] / 2
        body = box(tab["x"] - half, tab["x"] + half, *sorted((wall_in, end)), SCREW_TAB["z0"], plate_inner(tab))
        body = body.cut(screw_cylinder(tab, CASE_SCREW["pilot"], wall_in, end))
        if tab["side"] == "top" and VARIANT != "v3":
            for hole in terminal_holes()["DALI"]:
                body = body.cut(cylinder(hole["x"], hole["y"], SCREW_TAB["z0"] - 1, SHOULDER_Z["top"] + 1, SCREW_HOLE))
        bodies.append(body)
    return bodies


def screw_cylinder(tab, diameter, y0, y1):
    lo, hi = sorted((y0, y1))
    return (cq.Workplane("XY").circle(diameter / 2).extrude(hi - lo)
            .rotate((0, 0, 0), (1, 0, 0), -90).translate((tab["x"], lo, CASE_SCREW["z"])))


def knx_recess():
    """x0, x1 and top y of the KNX plug's pocket: the measured plug
    (enclosure_features.KNX_PLUG, sockets on the pins) plus PLUG_CLEARANCE.
    On v2 it is open to the front and to the bottom face, so the bus cable
    comes in from below and the plug lifts straight out."""
    p, c = knx_plug("J1"), PLUG_CLEARANCE
    return p["x0"] - c, p["x1"] + c, p["y1"] + c


KNX_WALL = 1.2


def knx_pocket_walls():
    """v2 cover pocket walls: x extent (key slot on +x) and bottom z, 0.3 mm
    above the PST strip so they never touch the board."""
    x0, x1, _ = knx_recess()
    return (x0 - KNX_WALL, min(x1 + PLUG_KEY["depth"] + KNX_WALL, INNER_W / 2 - 0.2),
            knx_plug("J1")["z0"] + PLUG_CLEARANCE)


def knx_pin_centres(board="main", ref="J1"):
    return [board_point(board, px, py) for px, py, _ in components(board)[ref]["pads"]]


PLUG_CLEARANCE = 0.3
PLUG_KEY = {"depth": 1.4, "from_back": (1.0, 7.0)}
BOARD_SUPPORT_GAP = 0.1
PLUG_REFS_V3 = ("J2", "J1")


def plug_pocket(ref):
    """The mated plug plus PLUG_CLEARANCE all round, open to the right side
    (+x, where the plug goes in) and to the bottom (-y, wire entries)."""
    p, c = knx_plug(ref), PLUG_CLEARANCE
    return {"x0": p["x0"], "y1": p["y1"] + c, "z0": p["z0"] - c, "z1": p["z1"] + c}


def strip_box(ref):
    """The PST strip's plastic body on the board front, with clearance."""
    b, c = components("main")[ref]["bbox"], PLUG_CLEARANCE
    (x0, y0, za), (_, y1, zb) = board_point("main", b["x0"], b["y0"]), board_point("main", b["x1"], b["y1"])
    return x0 - 0.01, knx_plug(ref)["x0"] + c, min(y0, y1) - c, max(y0, y1) + c, min(za, zb) - c, max(za, zb) + c


def plug_openings():
    """Each plug's pocket through the base (open to the right side and the
    bottom) and its key slot on the -z wall."""
    cuts = []
    for ref in PLUG_REFS_V3:
        p, back = plug_pocket(ref), knx_plug(ref)["y1"]
        cuts.append(box(p["x0"], WIDTH / 2 + 1, -1, p["y1"], p["z0"], p["z1"]))
        cuts.append(box(p["x0"], WIDTH / 2 + 1, back - PLUG_KEY["from_back"][1], back - PLUG_KEY["from_back"][0],
                        p["z0"] - PLUG_KEY["depth"], p["z0"] + 0.01))
    return cuts


def plug_block():
    """Block in the bottom-right corner of the base holding both plug pockets.
    Its left face stops 0.1 mm short of the main board's front, so the board
    rests on it instead of on its parts; the PST strips sit in recesses and
    every other front part gets PART_AIR. Each pocket has a slot on its -z wall
    for the plug's butterfly key (its exact y position is not measured, so the
    slot covers 1..7 mm below the plug's back)."""
    pockets = [plug_pocket(r) for r in PLUG_REFS_V3]
    x_board = SPEC["boards"]["main"]["x"]
    block = box(x_board + BOARD_SUPPORT_GAP, WIDTH / 2, 0.0, max(p["y1"] for p in pockets) + 1.2,
                WALL - 0.01, max(p["z1"] for p in pockets) + 1.0)
    for opening in plug_openings():
        block = block.cut(opening)
    for ref in PLUG_REFS_V3:
        block = block.cut(box(*strip_box(ref)))
    for b in part_boxes():
        comp = components("main").get(b["ref"]) if b["board"] == "main" else None
        if comp and comp["side"] == "front" and b["ref"] not in PLUG_REFS_V3:
            block = block.cut(box(b["x0"] - PART_AIR - 1, b["x1"] + PART_AIR, b["y0"] - PART_AIR, b["y1"] + PART_AIR,
                                  b["z0"] - PART_AIR, b["z1"] + PART_AIR))
    return block


def dovetail_xy(x_wall, y_mid, z0, z1, neck=2.6, flare=3.5, depth=1.1):
    """30-deg 'half-butterfly' dovetail keyway on the +x wall of a front-facing
    KNX pocket (WAGO 243-211 drawing: 2.5 mm neck widening at 30 deg to 3.4 mm,
    3.0 mm below the top of the 10 mm side wall — not in the corner)."""
    pts = [(x_wall - 0.1, y_mid - neck / 2), (x_wall + depth, y_mid - flare / 2),
           (x_wall + depth, y_mid + flare / 2), (x_wall - 0.1, y_mid + neck / 2)]
    return cq.Workplane("XY", origin=(0, 0, z0)).polyline(pts).close().extrude(z1 - z0)


def dovetail_zy(z_wall, y_mid, x0, x1, neck=2.6, flare=3.5, depth=1.1, sign=-1):
    """30-deg 'half-butterfly' dovetail keyway on a z_wall (sign=-1 for -z wall,
    sign=+1 for +z wall) of a right-facing V3 pocket, extruded along +x."""
    pts = [(z_wall - sign * 0.1, y_mid - neck / 2), (z_wall + sign * depth, y_mid - flare / 2),
           (z_wall + sign * depth, y_mid + flare / 2), (z_wall - sign * 0.1, y_mid + neck / 2)]
    return (cq.Workplane("YZ", origin=(x0, 0, 0)).polyline([(y, z) for z, y in pts]).close()
            .extrude(x1 - x0))


def walls_clip(solid):
    """Keep only what lies inside the outer envelope."""
    return solid.intersect(box(-WIDTH / 2, WIDTH / 2, 0, HIDDEN_H, -BACK_DEPTH, BASE_TOP_Z))


def board_edge_reliefs():
    """Clearance in the base's bottom and top walls along the path of a
    vertical main board, which slides in from the open left side."""
    if not vertical_main():
        return []
    slab, c = board_slab("main"), BOARD_EDGE_CLEARANCE
    bands = [(slab["y0"] - c, WALL + 0.01)] if slab["y0"] - c < WALL else []
    bands += [(HIDDEN_H - WALL - 0.01, slab["y1"] + c)] if slab["y1"] + c > HIDDEN_H - WALL else []
    return [box(-WIDTH / 2 - 1, slab["x1"] + PART_AIR, y0, y1, slab["z0"] - PART_AIR, slab["z1"] + PART_AIR)
            for y0, y1 in bands]


def main_back_face_x():
    return SPEC["boards"]["main"]["x"] - MAIN_THICK


POST_HALF = 1.0


def post_bodies():
    """Base posts from the right wall to 0.1 mm short of the vertical main
    board's front face (layout case.front_posts)."""
    if not vertical_main():
        return []
    x0 = SPEC["boards"]["main"]["x"] + BOARD_SUPPORT_GAP
    return [box(x0, INNER_W / 2 + 0.01, y - POST_HALF, y + POST_HALF, z - POST_HALF, z + POST_HALF)
            for _, y, z in front_posts()]


def rib_bodies():
    """Ribs on the cover's left wall that back the vertical main board, so the
    plug insertion force goes into the case instead of parts and solder joints."""
    if not vertical_main():
        return []
    return [box(-INNER_W / 2 - 0.01, main_back_face_x() - RIB["gap"], y - RIB["half_y"], y + RIB["half_y"],
                z - RIB["half_z"], z + RIB["half_z"]) for _, y, z in main_ribs()]


def back_part_reliefs():
    """Channels in the cover's left wall over back-side parts of the vertical
    main board that reach closer than PART_AIR to the wall; open towards the
    front because the cover slides on along -z."""
    if not vertical_main():
        return []
    wall_in = -INNER_W / 2
    reliefs = []
    for b in part_boxes():
        comp = components(b["board"]).get(b["ref"]) if b["board"] == "main" else None
        if comp is None or comp["side"] != "back" or b["x0"] >= wall_in + PART_AIR:
            continue
        x0 = b["x0"] - PART_AIR
        if x0 < -WIDTH / 2 + MIN_SKIN:
            raise ValueError(f"main {b['ref']} would leave less than {MIN_SKIN} mm of the left wall")
        reliefs.append(box(x0, wall_in + 0.01, b["y0"] - PART_AIR, b["y1"] + PART_AIR,
                           b["z0"] - PART_AIR, BASE_TOP_Z + 0.01))
    return reliefs


def tall_front_parts():
    """Front-side parts of the vertical main board that reach closer than
    PART_AIR to the right wall (the TMR converter, 11.2 +/- 0.5 mm), with
    the outer x of the recess they need."""
    if not vertical_main():
        return []
    wall_in = INNER_W / 2
    found = []
    for b in part_boxes():
        comp = components(b["board"]).get(b["ref"]) if b["board"] == "main" else None
        if comp is None or comp["side"] != "front" or b["x1"] <= wall_in - PART_AIR or b["ref"] in PLUG_REFS_V3:
            continue
        x1 = b["x1"] + PART_AIR
        if x1 > WIDTH / 2 - MIN_SKIN:
            raise ValueError(f"main {b['ref']} would leave less than {MIN_SKIN} mm of the right wall")
        found.append((b, x1))
    return found


def front_part_reliefs():
    """Recesses in the base's right wall over tall front-side parts; the board
    slides in along +x, so a local recess suffices."""
    return [box(INNER_W / 2 - 0.01, x1, b["y0"] - PART_AIR, b["y1"] + PART_AIR,
                b["z0"] - PART_AIR, min(b["z1"] + PART_AIR, BASE_TOP_Z))
            for b, x1 in tall_front_parts() if b["z0"] < BASE_TOP_Z]


def protrusion_part_reliefs():
    """Channels in the cover's right protrusion wall over tall front-side parts
    above the base; open towards the base because the cover slides on along -z."""
    return [box(INNER_W / 2 - 0.01, x1, b["y0"] - PART_AIR, b["y1"] + PART_AIR,
                BASE_TOP_Z - 0.01, b["z1"] + PART_AIR)
            for b, x1 in tall_front_parts() if b["z1"] > BASE_TOP_Z]


def terminal_reliefs():
    """Clearance in the cover's shoulder plate over a vertical-board terminal
    whose body reaches the plate (the DALI terminal overhangs the board edge)."""
    if not vertical_main():
        return []
    reliefs = []
    for s in wire_entries():
        body = next(b for b in part_boxes("body") if (b["board"], b["ref"]) == ("main", s["ref"]))
        if body["z1"] + PART_AIR > BASE_TOP_Z:
            reliefs.append(box(s["x0"], s["x1"], body["y0"] - PART_AIR, HIDDEN_H + 1, BASE_TOP_Z - 0.01,
                               body["z1"] + PART_AIR))
    return reliefs


def button_top_z(ref):
    return UI_BOARD_TOP_Z + components("ui")[ref]["height"]


def button_tab_outline(hole, grow=0.0):
    """Front outline of a C-shaped button tab (or of its slot with grow =
    slot): a round pad over the button and an arm towards the middle of the
    front face, where both tabs root on one bridge. The left tab reads as a C,
    the right one as a mirrored C. Returns (pad circle (x, y, d), arm box
    (x0, x1, y0, y1))."""
    t = BUTTON_TAB
    root = math.copysign(t["root_x"], hole["x"])
    return ((hole["x"], hole["y"], t["pad"] + 2 * grow),
            (*sorted((hole["x"], root)), hole["y"] - t["arm"] / 2 - grow, hole["y"] + t["arm"] / 2 + grow))


def button_tab(ref, hole):
    """Spring tab in the front wall over a button that does not reach the
    front face: the C-shaped slot frees a pad and arm, both thinned to
    BUTTON_TAB thickness, so the pad flexes on the arm rooted at the middle
    bridge; a rounded bump on the front takes the finger and a plunger behind
    the pad ends just above the actuator. Returns (cuts, bodies)."""
    t = BUTTON_TAB

    def solid(grow, z0, z1):
        (cx, cy, d), (x0, x1, y0, y1) = button_tab_outline(hole, grow)
        return cylinder(cx, cy, z0, z1, d).union(box(x0, x1, y0, y1, z0, z1))

    through = (FRONT_INNER_Z - 1, TOTAL_D + 1)
    tab_back = TOTAL_D - t["thickness"]
    slot = solid(t["slot"], *through).cut(solid(0.0, *through))
    thin = solid(0.0, FRONT_INNER_Z - 1, tab_back)
    bump = cylinder(hole["x"], hole["y"], TOTAL_D - 0.01, TOTAL_D + t["boss_height"], t["boss"]) \
        .faces(">Z").edges().fillet(t["boss_round"])
    plunger = cylinder(hole["x"], hole["y"], button_top_z(ref) + t["nub_gap"], tab_back + 0.01, t["nub"])
    return [slot, thin], [plunger, bump]


def build_base():
    shell = box(-WIDTH / 2, WIDTH / 2, 0, HIDDEN_H, 0, BASE_TOP_Z).cut(
        box(-INNER_W / 2, INNER_W / 2, WALL, HIDDEN_H - WALL, WALL, BASE_TOP_Z + 1))
    base = shell.union(rail_blocks())
    pocket, latch = latch_parts()
    base = base.cut(pocket).union(latch)
    for slot in wire_entries():
        base = base.cut(box(slot["x0"], slot["x1"], HIDDEN_H - WALL - 1, HIDDEN_H + 1, slot["z0"], slot["z1"]))
    if VARIANT == "v3":
        # J1 (KNX red/black, front) and J2 (AUX yellow/white, rear): pins point
        # +x, wire entries face down (-y); each plug sits on its PST strip in a
        # pocket open to the right side and the bottom (plug_block). The left
        # side is open so main slides in along +x until its front face meets
        # the block; the cover closes the left wall.
        base = base.cut(box(-WIDTH / 2 - 1, -INNER_W / 2 + 0.01, WALL, HIDDEN_H - WALL, WALL, BASE_TOP_Z + 1))
        base = base.union(plug_block())
        for opening in plug_openings():
            base = base.cut(opening)
        # Flat-screwdriver pry recesses at the left-side top/bottom snap seams
        base = base.cut(box(-INNER_W / 2 - 0.1, -INNER_W / 2 + 1.4, -0.1, 0.8, 16.5, 19.5))
        base = base.cut(box(-INNER_W / 2 - 0.1, -INNER_W / 2 + 1.4, HIDDEN_H - 0.8, HIDDEN_H + 0.1, 16.5, 19.5))
        # Three passive convection micro-louvers on top and bottom walls
        for vx in (-2.2, 0.0, 2.2):
            base = base.cut(box(vx - 0.45, vx + 0.45, -0.1, WALL + 0.1, 7.5, 12.5))
            base = base.cut(box(vx - 0.45, vx + 0.45, HIDDEN_H - WALL - 0.1, HIDDEN_H + 0.1, 7.5, 12.5))

    else:
        x0, x1, _ = knx_pocket_walls()
        base = base.cut(box(x0 - 0.05, min(x1 + 0.05, INNER_W / 2), -1, WALL + 1, MAIN_Z + 2.8, BASE_TOP_Z + 1))
    for body in leg_bodies() + capacitor_cradles() + post_bodies():
        base = base.union(walls_clip(body))
    for cut in shoulder_snap_keepers() + board_edge_reliefs() + front_part_reliefs():
        base = base.cut(cut)
    for tab in screw_tabs():
        outer, wall_in, _ = screw_axis_y(tab)
        overshoot = 1 if tab["side"] == "top" else -1
        base = base.cut(screw_cylinder(tab, CASE_SCREW["clearance"], outer + overshoot, wall_in - overshoot))
    for cut in side_legend_cuts():
        base = base.cut(cut)
    return base


def side_legend_cuts():
    """V3 connector names (front_legend.side_labels) engraved INLAY_DEPTH into
    the base's right side face."""
    import front_legend
    import legend_color_model
    return [legend_color_model.inlay(label.path(), 0.0).rotate((0, 0, 0), (0, 1, 0), 90).translate((WIDTH / 2, 0, 0))
            for label in front_legend.side_labels()]


def peg_bodies():
    """Pegs hang from the shoulder plates onto the main board's front."""
    return [cylinder(p["x"], p["y"], MAIN_Z, SHOULDER_Z[shoulder_of(p["y"])] - WALL, PEG_DIAMETER)
            for p in pegs()]


def standoff_bodies():
    """Standoffs from the front wall onto the UI board: cylinders, or blocks
    where the layout gives a size (beside the OLED panel, which a 3 mm
    cylinder would overlap)."""
    z1 = FRONT_INNER_Z + STACK.pocket_depth + 0.1
    bodies = [box(p["x"] - p["w"] / 2, p["x"] + p["w"] / 2, p["y"] - p["h"] / 2, p["y"] + p["h"] / 2,
                  UI_BOARD_TOP_Z, z1) if "w" in p else cylinder(p["x"], p["y"], UI_BOARD_TOP_Z, z1, STANDOFF_DIAMETER)
              for p in standoffs()]
    if VARIANT == "v3":
        # Locating ledges just below the OLED panel's bottom edge (enc y 31.75)
        for sx in (-5.8, 5.8):
            bodies.append(box(sx - 1.1, sx + 1.1, 30.85, 31.65, FRONT_INNER_Z - 1.5, z1))
    return bodies


def baffle_bodies():
    """A tube around each LED from just above the UI board to the front wall,
    so its light reaches only its own thin window."""
    outer = LED_WINDOW + 2 * BAFFLE_WALL
    return [cylinder(w["x"], w["y"], STACK.baffle_bottom_z, FRONT_INNER_Z + 0.01, outer)
            .cut(cylinder(w["x"], w["y"], STACK.baffle_bottom_z - 1, FRONT_INNER_Z + 1, LED_WINDOW))
            for w in led_windows()]


def snap_geometry():
    """For each UI snap: the U-slot cut into a protrusion side wall and the
    tongue left inside it. The tongue is rooted near the front wall; its free
    end, behind the UI board, carries a hook with a ramp on the side the board
    comes from, so the board pushes the tongues outward, passes, and the
    hooks spring back behind its edges."""
    z_free = UI_BOARD_Z - SNAP["free_end_below_board"]
    z_root = FRONT_INNER_Z - 0.4
    half, s = SNAP["width"] / 2, SNAP["slot"]
    x_hook = UI_W / 2 - SNAP["overlap"]
    parts = []
    for y in SNAP["y"]:
        for side in (1, -1):
            wall = sorted((side * INNER_W / 2, side * WIDTH / 2))
            slot = box(wall[0] - 0.01, wall[1] + 0.01, y - half - s, y + half + s, z_free - s, z_root)
            tongue = box(*wall, y - half, y + half, z_free, z_root + 0.01)
            hook = box(*sorted((side * x_hook, side * INNER_W / 2)), y - half, y + half,
                       UI_BOARD_Z - SNAP["hook_height"], UI_BOARD_Z)
            ramp_edge = "|Y and <Z and " + ("<X" if side > 0 else ">X")
            parts.append((slot, tongue.union(hook.edges(ramp_edge).chamfer(SNAP["overlap"] + 0.3))))
    return parts


def snap_bodies():
    return [tongue for _, tongue in snap_geometry()]


LIP_BOND = {"over_wall": 0.6, "height": 1.2}
SNAP_FREE = 0.6


def lip_bodies():
    """Locating lips on the inside of the base side walls. Each lip hangs
    0.15 mm inside the base wall, so a LIP_BOND bridge on top ties it into the
    cover's side wall along its whole length. Lips are interrupted where a
    stacked board's edge or a part passes, around the side snap tongues (so
    those stay free to flex) and over the V3 plug block."""
    x_out = INNER_W / 2 - 0.15
    z0 = BASE_TOP_Z - LIP["depth"]
    top = BASE_TOP_Z + LIP_BOND["height"]
    sides = (1,) if VARIANT == "v3" else (1, -1)
    lips = []
    for s in sides:
        lip = box(*sorted((s * x_out, s * (x_out - LIP["thickness"]))), WALL + 1, HIDDEN_H - WALL - 1, z0, BASE_TOP_Z)
        bond = box(*sorted((s * (x_out - LIP["thickness"]), s * (INNER_W / 2 + LIP_BOND["over_wall"]))),
                   WALL + 1, HIDDEN_H - WALL - 1, BASE_TOP_Z - 0.01, top)
        lips.append(lip.union(bond))
    notches = [box(-WIDTH / 2, WIDTH / 2, slab["y0"] - 0.5, slab["y1"] + 0.5, z0 - 1, top + 1)
               for slab in (board_slab(b) for b in SPEC["variants"][VARIANT]["boards"] if b not in ("main", "ui"))
               if slab["z0"] < BASE_TOP_Z and slab["z1"] > z0]
    reach = x_out - LIP["thickness"] - PART_AIR
    notches += [box(b["x0"] - PART_AIR, b["x1"] + PART_AIR, b["y0"] - PART_AIR, b["y1"] + PART_AIR, z0 - 1, top + 1)
                for b in part_boxes("body")
                if b["board"] == "main" and b["z1"] > z0 and (b["x1"] > reach or b["x0"] < -reach)]
    if not screw_tabs():
        free = SIDE_SNAPS["half"] + SNAP_FREE
        notches += [box(-WIDTH / 2, WIDTH / 2, y - free, y + free, z0 - 1, top + 1) for y in SIDE_SNAPS["right_y"]]
    if VARIANT == "v3":
        bb = plug_block().val().BoundingBox()
        notches.append(box(-WIDTH / 2, WIDTH / 2, -1, bb.ymax + PART_AIR, z0 - 1, top + 1))
    for notch in notches:
        lips = [lip.cut(notch) for lip in lips]
    return lips


def shoulder_plate(side):
    """Plate and skirt of one shoulder, from the base's top edge up to that
    shoulder's face; open towards the protrusion."""
    face = SHOULDER_Z[side]
    y0, y1 = (0, FRONT_Y_START) if side == "bottom" else (FRONT_Y_END, HIDDEN_H)
    inner_y = (WALL, FRONT_Y_START + 1) if side == "bottom" else (FRONT_Y_END - 1, HIDDEN_H - WALL)
    shell = box(-WIDTH / 2, WIDTH / 2, y0, y1, BASE_TOP_Z, face)
    return shell.cut(box(-INNER_W / 2, INNER_W / 2, *inner_y, BASE_TOP_Z - 1, face - WALL))


def build_cover():
    protrusion = box(-WIDTH / 2, WIDTH / 2, FRONT_Y_START, FRONT_Y_END, BASE_TOP_Z, TOTAL_D).cut(
        box(-INNER_W / 2, INNER_W / 2, FRONT_Y_START + WALL, FRONT_Y_END - WALL, BASE_TOP_Z - 1, FRONT_INNER_Z))
    cover = protrusion.union(shoulder_plate("bottom")).union(shoulder_plate("top"))
    if VARIANT == "v3":
        cover = cover.union(box(-WIDTH / 2, -INNER_W / 2, WALL - 0.01, HIDDEN_H - WALL + 0.01, WALL, BASE_TOP_Z + 0.01))
    for relief in back_part_reliefs() + terminal_reliefs() + protrusion_part_reliefs():
        cover = cover.cut(relief)
    for body in lip_bodies() + peg_bodies() + standoff_bodies() + screw_tab_bodies() + baffle_bodies() + rib_bodies():
        cover = cover.union(body)
    for slot, tongue in snap_geometry():
        cover = cover.cut(slot).union(tongue)
    if VARIANT != "v3":
        # Pocket hanging from the bottom shoulder around the KNX plug, which
        # sits on its PST strip (no floor in between); open to the front and
        # the bottom, with a slot for the butterfly key on the +x wall.
        x0, x1, y1 = knx_recess()
        w_x0, w_x1, z_wall = knx_pocket_walls()
        back = knx_plug("J1")["y1"]
        cover = cover.union(box(w_x0, w_x1, 0.0, y1 + KNX_WALL, z_wall, SHOULDER_Z["bottom"]))
        cover = cover.cut(box(x0, x1, -1, y1, z_wall - 1, SHOULDER_Z["bottom"] + 1))
        cover = cover.cut(box(x1 - 0.01, x1 + PLUG_KEY["depth"], back - PLUG_KEY["from_back"][1],
                              back - PLUG_KEY["from_back"][0], z_wall - 1, SHOULDER_Z["bottom"] + 1))
    if VARIANT != "v3":
        for name in ("DALI", "AUX"):
            for hole in terminal_holes()[name]:
                cover = cover.cut(cylinder(hole["x"], hole["y"], BASE_TOP_Z - 1, SHOULDER_Z["top"] + 1, SCREW_HOLE))
    front = (FRONT_INNER_Z - 1, TOTAL_D + 1)
    for w in led_windows():
        cover = cover.cut(cylinder(w["x"], w["y"], FRONT_INNER_Z - 1, TOTAL_D - STACK.led_skin_mm, LED_WINDOW))
    for ref, hole in zip(("SW1", "SW2"), button_holes().values()):
        if button_top_z(ref) < TOTAL_D:
            cuts, bodies = button_tab(ref, hole)
            for cut in cuts:
                cover = cover.cut(cut)
            for body in bodies:
                cover = cover.union(body)
        else:
            cover = cover.cut(cylinder(hole["x"], hole["y"], *front, BUTTON_HOLE))
    win, panel, module = oled_window(), oled_panel(), oled_module()
    cover = cover.cut(box(win["cx"] - win["w"] / 2, win["cx"] + win["w"] / 2,
                          win["cy"] - win["h"] / 2, win["cy"] + win["h"] / 2, *front))
    margin = 0.5
    pocket = box(module["cx"] - module["w"] / 2 - margin, module["cx"] + module["w"] / 2 + margin,
                 module["cy"] - module["h"] / 2 - margin, module["cy"] + module["h"] / 2 + margin,
                 FRONT_INNER_Z - 1, FRONT_INNER_Z + STACK.pocket_depth)
    if panel != oled_glass():
        g_margin = 0.25
        pocket = pocket.union(box(panel["cx"] - panel["w"] / 2 - g_margin, panel["cx"] + panel["w"] / 2 + g_margin,
                                  panel["cy"] - panel["h"] / 2 - g_margin, panel["cy"] + panel["h"] / 2 + g_margin,
                                  FRONT_INNER_Z - 1, TOTAL_D - 0.6))
    for body in standoff_bodies():
        pocket = pocket.cut(body)
    return cover.cut(pocket)


def export_parts(out_dir):
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, part in (("base", build_base()), ("cover", build_cover())):
        cq.exporters.export(part, str(out_dir / f"{name}.step"), cq.exporters.ExportTypes.STEP)
        cq.exporters.export(part, str(out_dir / f"{name}.stl"), cq.exporters.ExportTypes.STL)
        print(f"{name}: {name}.step + {name}.stl")


if __name__ == "__main__":
    export_parts(sys.argv[1] if len(sys.argv) > 1 else REPO / "mechanical/build" / SPEC["active_variant"])
