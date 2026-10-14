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
    SHOULDER_Z,
    SPEC,
    TOTAL_D,
    VARIANT,
    WIDTH,
    board_point,
    board_slab,
    button_holes,
    components,
    horizontal_capacitor,
    led_windows,
    legs,
    oled_module,
    oled_window,
    pegs,
    screw_tabs,
    shoulder_of,
    standoffs,
    terminal_holes,
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
KNX_OPENING = 10.6
SCREW_HOLE = 3.2 if VARIANT == "v3" else 4.0
CASE_SCREW = {"pilot": 2.2, "clearance": 2.8, "depth": 8.5, "z": BASE_TOP_Z - 2.0}
SCREW_TAB = {"width": 4.0 if VARIANT == "v3" else 6.0, "depth": 8.5,
             "z0": 39.85 if VARIANT == "v3" else MAIN_Z + 2.0 + SPEC["checks"]["component_air_gap"]}
LIP = {"thickness": 1.2, "depth": 2.0}
SNAP = {"overlap": 0.5, "width": 6.0, "slot": 0.5, "hook_height": 1.8, "free_end_below_board": 1.8,
        "y": (FRONT_Y_START + 12, FRONT_Y_END - 12)}

RAIL = {"centre": HIDDEN_H / 2, "slot": 35.4, "lip": 1.0, "clearance": 0.1}
BACK_DEPTH = 5.0
HOOK_REACH = 1.5
LATCH = {"half_width": min(5.0, WIDTH / 2 - 4.5), "gap": 0.4, "tab_bottom": -6.0, "beam": 0.8,
         "beam_ys": (9.0, 11.4), "beam_reach": min(16.0, WIDTH / 2 - 1.5), "hole": (4.0, 2.0),
         "hole_y": -4.0, "travel": 2.0}


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
    top; its lower end is a tab sticking out below the case with a hole facing
    the front. Two S-shaped meander flexure springs (one on each side of the
    bar) let the bar slide down and spring it back."""
    low, _ = rail_edges()
    back = -BACK_DEPTH
    w, g, b = LATCH["half_width"], LATCH["gap"], LATCH["beam"]
    reach = LATCH["beam_reach"]
    y0, y1 = LATCH["beam_ys"]
    y2 = y1 + (y1 - y0)
    pocket = box(-w - g, w + g, LATCH["tab_bottom"] - 1, low + 1, back - 1, 0).union(
        box(-reach - g, reach + g, y0 - LATCH["travel"], y2 + b + g, back - 1, 0))
    bar_z = (back, -g)
    bar = box(-w, w, LATCH["tab_bottom"], low - g, *bar_z)
    lip = box(-w, w, low - g, low + HOOK_REACH, *lip_z())
    lip = lip.edges("|X and <Z and >Y").chamfer(1.0)
    latch = bar.union(lip)
    for s in (-1, 1):
        # 3-leg S-meander spring: bar at y0 -> outer turn -> middle leg at y1 ->
        # inner turn -> wall at y2
        leg1 = box(*sorted((s * w, s * (reach - g))), y0, y0 + b, *bar_z)
        turn1 = box(*sorted((s * (reach - g - b), s * (reach - g))), y0, y1 + b, *bar_z)
        leg2 = box(*sorted((s * (w + g), s * (reach - g))), y1, y1 + b, *bar_z)
        turn2 = box(*sorted((s * (w + g), s * (w + g + b))), y1, y2 + b, *bar_z)
        leg3 = box(*sorted((s * (w + g), s * (reach + g + 0.5))), y2, y2 + b, *bar_z)
        for seg in (leg1, turn1, leg2, turn2, leg3):
            latch = latch.union(seg)
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


def shoulder_snap_bodies():
    """On V3 (where J1 and J3 occupy the front of the top and bottom shoulders),
    two snap-click tongues on the left side (x = -6.9..-4.5, clear of J1 and J3)
    click into keeper pockets on the inner faces of the top and bottom base walls."""
    if screw_tabs():
        return []
    bodies = []
    for y_wall, d in ((WALL, 1), (HIDDEN_H - WALL, -1)):
        y0, y1 = sorted((y_wall, y_wall + d * 1.1))
        yb0, yb1 = sorted((y_wall - d * 0.5, y_wall + d * 1.1))
        tongue = box(-6.9, -4.5, y0, y1, 39.0, SHOULDER_Z["top"] - WALL)
        barb = box(-6.9, -4.5, yb0, yb1, 39.0, 40.2)
        bodies.append(tongue.union(barb))
    return bodies


def shoulder_snap_keepers():
    """Matching keeper recesses on the inner faces of the top and bottom base walls."""
    if screw_tabs():
        return []
    return [box(-7.0, -4.4, WALL - 0.6, WALL + 0.1, 38.9, 40.4),
            box(-7.0, -4.4, HIDDEN_H - WALL - 0.1, HIDDEN_H - WALL + 0.6, 38.9, 40.4)]


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
    """The KNX plug sits in a recess open to the front and to the bottom face,
    so the bus cable can come in from below and the plug lifts straight out."""
    knx = terminal_holes()["KNX"][0]
    half = KNX_OPENING / 2
    return knx["x"] - half, knx["x"] + half, knx["y"] + half


def knx_pin_centres(board="main", ref="J1"):
    return [board_point(board, px, py) for px, py, _ in components(board)[ref]["pads"]]


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


def build_base():
    shell = box(-WIDTH / 2, WIDTH / 2, 0, HIDDEN_H, 0, BASE_TOP_Z).cut(
        box(-INNER_W / 2, INNER_W / 2, WALL, HIDDEN_H - WALL, WALL, BASE_TOP_Z + 1))
    base = shell.union(rail_blocks())
    pocket, latch = latch_parts()
    base = base.cut(pocket).union(latch)
    for slot in wire_entries():
        base = base.cut(box(slot["x0"], slot["x1"], HIDDEN_H - WALL - 1, HIDDEN_H + 1, slot["z0"], slot["z1"]))
    x0, x1, _y1 = knx_recess()
    if VARIANT == "v3":
        # J1 (KNX red/black, front) and J2 (AUX yellow/white, rear) sit on main
        # (x = -5.7) with their pins pointing +x to the right (turned 90 deg to
        # the right) and their 4+4 wire holes facing straight down (-y). A solid
        # inner wall at x = -2.8..-1.8 seals the PCB and the 2.8 mm PST 1,0
        # plastic strips; only the 4 metal pins poke through 2.2 mm holes, and
        # each pocket has a 30-deg half-butterfly dovetail on its -z (+) wall
        # 3.3 mm below the pocket roof (not in the corner).
        # Bottom pockets for J2 (AUX, rear) and J1 (KNX, front), and top pocket
        # for J3 (DALI, top-front): solid inner wall at x = -1.3..-0.3 covers the
        # 2.8 mm PST 1,0 plastic strips on main (x = -4.2); only the metal pins
        # poke through 2.2 mm holes, and each pocket has a 30-deg half-butterfly
        # dovetail keyway on its + wall.
        base = base.union(box(-1.3, WIDTH / 2, 0.0, 12.2, 14.8, 39.8))
        for ref, z_lo, z_hi in (("J2", 16.3, 26.8), ("J1", 29.3, 39.8)):
            base = base.cut(box(-0.3, WIDTH / 2 + 1, -1, 11.2, z_lo, z_hi))
            base = base.cut(dovetail_zy(z_lo, 8.0, -0.3, WIDTH / 2 + 1, sign=-1))
            for _, py, pz in knx_pin_centres("main", ref):
                base = base.cut(cq.Workplane("YZ", origin=(-1.6, 0, 0)).center(py, pz)
                                .circle(1.1).extrude(1.6))

    else:
        base = base.cut(box(x0 - 1.25, min(x1 + 1.85, INNER_W / 2), -1, WALL + 1, MAIN_Z + 2.8, BASE_TOP_Z + 1))
    for body in leg_bodies() + capacitor_cradles():
        base = base.union(walls_clip(body))
    for keeper in shoulder_snap_keepers():
        base = base.cut(keeper)
    for tab in screw_tabs():
        outer, wall_in, _ = screw_axis_y(tab)
        overshoot = 1 if tab["side"] == "top" else -1
        base = base.cut(screw_cylinder(tab, CASE_SCREW["clearance"], outer + overshoot, wall_in - overshoot))
    return base


def peg_bodies():
    """Pegs hang from the shoulder plates onto the main board's front."""
    return [cylinder(p["x"], p["y"], MAIN_Z, SHOULDER_Z[shoulder_of(p["y"])] - WALL, PEG_DIAMETER)
            for p in pegs()]


def standoff_bodies():
    z1 = FRONT_INNER_Z + STACK.pocket_depth + 0.1
    bodies = [cylinder(p["x"], p["y"], UI_BOARD_TOP_Z, z1, STANDOFF_DIAMETER) for p in standoffs()]
    if VARIANT == "v3":
        # Two vertical support rails behind the left/right edges of the CFAL12832D-B
        # OLED panel, stopping 1.5 mm behind the panel back for 1-2 layers of 1 mm
        # double-sided foam tape while leaving the centre clear for the flex tail.
        z_tape = STACK.oled_glass_z - STACK.oled["panel"] - STACK.oled["module_board"] - 1.5 if hasattr(STACK, "oled") else FRONT_INNER_Z - 1.5
        for sx in (-4.6, 4.6):
            bodies.append(box(sx - 0.8, sx + 0.8, 30.0, 56.0, z_tape, z1))
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


def lip_bodies():
    """Locating lips on the inside of the base side walls, interrupted where a
    stacked board's edge passes through their height."""
    x_out = INNER_W / 2 - 0.15
    z0 = BASE_TOP_Z - LIP["depth"]
    sides = (1,) if VARIANT == "v3" else (1, -1)
    lips = [box(*sorted((s * x_out, s * (x_out - LIP["thickness"]))), WALL + 1, HIDDEN_H - WALL - 1,
                z0, BASE_TOP_Z) for s in sides]
    for slab in (board_slab(b) for b in SPEC["variants"][VARIANT]["boards"] if b not in ("main", "ui")):
        if slab["z0"] < BASE_TOP_Z and slab["z1"] > z0:
            notch = box(-WIDTH / 2, WIDTH / 2, slab["y0"] - 0.5, slab["y1"] + 0.5, z0 - 1, BASE_TOP_Z + 1)
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
    for body in lip_bodies() + peg_bodies() + standoff_bodies() + screw_tab_bodies() + baffle_bodies():
        cover = cover.union(body)
    for slot, tongue in snap_geometry():
        cover = cover.cut(slot).union(tongue)
    if VARIANT != "v3":
        # Crafted inverted-L pocket on the cover: reaches down to z = MAIN_Z + 2.8
        # with a 1.0 mm floor over the PST 1,0 plastic strip, two 2.2 mm holes for
        # the protruding KNX pins, 1.2 mm side/top walls protecting the board, and
        # a coding notch on the right (+) wall for the red half's key tab.
        x0, x1, y1 = knx_recess()
        pins = knx_pin_centres("main", "J1")
        pins[0][1]
        x_hi = min(x1 + 1.8, INNER_W / 2 - 0.2)
        pocket = box(x0 - 1.2, x_hi, 0.0, y1 + 1.2, MAIN_Z + 2.8, SHOULDER_Z["bottom"])
        cover = cover.union(pocket)
        cover = cover.cut(box(x0, x1, -1, y1, MAIN_Z + 3.8, SHOULDER_Z["bottom"] + 1))
        cover = cover.cut(dovetail_xy(x1, y1 - 3.3, MAIN_Z + 3.8, SHOULDER_Z["bottom"] + 1))
        for px, py_p, _ in pins:
            cover = cover.cut(cylinder(px, py_p, MAIN_Z + 2.0, MAIN_Z + 4.5, 2.2))
    if VARIANT != "v3":
        for name in ("DALI", "AUX"):
            for hole in terminal_holes()[name]:
                cover = cover.cut(cylinder(hole["x"], hole["y"], BASE_TOP_Z - 1, SHOULDER_Z["top"] + 1, SCREW_HOLE))
    front = (FRONT_INNER_Z - 1, TOTAL_D + 1)
    for w in led_windows():
        cover = cover.cut(cylinder(w["x"], w["y"], FRONT_INNER_Z - 1, TOTAL_D - STACK.led_skin_mm, LED_WINDOW))
    for hole in button_holes().values():
        cover = cover.cut(cylinder(hole["x"], hole["y"], *front, BUTTON_HOLE))
    win, module = oled_window(), oled_module()
    cover = cover.cut(box(win["cx"] - win["w"] / 2, win["cx"] + win["w"] / 2,
                          win["cy"] - win["h"] / 2, win["cy"] + win["h"] / 2, *front))
    margin = 0.5
    pocket = box(module["cx"] - module["w"] / 2 - margin, module["cx"] + module["w"] / 2 + margin,
                 module["cy"] - module["h"] / 2 - margin, module["cy"] + module["h"] / 2 + margin,
                 FRONT_INNER_Z - 1, FRONT_INNER_Z + STACK.pocket_depth)
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
