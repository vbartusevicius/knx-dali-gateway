"""Board-to-enclosure fit, using the parts on the stored layouts.

Every part height is checked against the surface in front of or behind it,
every case opening must lie on the face it belongs to without overlapping
another, and the support posts must miss the parts on the back of the main
board. Each check has a negative case that must fail.
"""

import sys
from pathlib import Path

import pytest
import yaml

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "mechanical/scripts"))
import enclosure_features as ef

SPEC = ef.SPEC
WALL = SPEC["enclosure"]["wall"]
MIN_GAP = SPEC["checks"]["component_to_cover_min_gap"]
AIR = SPEC["checks"]["component_air_gap"]
REACH_OPENINGS = {"J1", "J2", "J3"}
MAIN_TOP_Z = SPEC["boards"]["main"]["z"]
MAIN_THICK = SPEC["boards"]["main"]["thickness"]
UI_BOTTOM_Z = SPEC["front_ui"]["board_z"]
UI_TOP_Z = SPEC["front_ui"]["board_top_z"]
FRONT_INNER_Z = ef.TOTAL_D - WALL
THROUGH_FRONT = ef.THROUGH_FRONT


def heights(board):
    plan = yaml.safe_load((REPO / f"hardware/circuits/variants/{ef.VARIANT}/layout_{board}.yaml").read_text())
    return plan["heights"]


def part_height(board, ref):
    h = heights(board)
    return h.get(ref, h["default"])


def main_part_limits(comp, height):
    """Return (top_z, limit_z) for a main-board part."""
    if comp["side"] == "back":
        return WALL, MAIN_TOP_Z - MAIN_THICK - height
    _, enc_y = ef.main_to_enc(*ef.bbox_centre(comp))
    under_ui = ef.FRONT_Y_START < enc_y < ef.FRONT_Y_END
    limit = UI_BOTTOM_Z - part_height("ui", "J1") if under_ui else ef.SHOULDER_Z[ef.shoulder_of(enc_y)] - WALL
    return MAIN_TOP_Z + height, limit


def main_height_violations(comps, height_of):
    bad = []
    for ref, comp in comps.items():
        if ref in REACH_OPENINGS:
            continue
        gap = AIR
        top, limit = main_part_limits(comp, height_of(ref))
        if comp["side"] == "back" and limit < top + gap - 1e-6:
            bad.append((ref, round(limit, 2), top))
        if comp["side"] == "front" and top > limit - gap + 1e-6:
            bad.append((ref, round(top, 2), limit))
    return bad


def test_main_parts_have_air_to_floor_shoulders_and_ui_board():
    assert not main_height_violations(ef.components("main"), lambda r: part_height("main", r))


def test_main_height_check_catches_a_tall_part():
    comps = ef.components("main")
    assert main_height_violations(comps, lambda r: 30.0 if r == "C4" else part_height("main", r))


def test_ui_front_parts_stay_behind_front_wall():
    bad = [(ref, UI_TOP_Z + part_height("ui", ref)) for ref, c in ef.components("ui").items()
           if c["side"] == "front" and ref not in THROUGH_FRONT
           and UI_TOP_Z + part_height("ui", ref) > FRONT_INNER_Z - MIN_GAP + 1e-6]
    assert not bad


FLUSH_TOLERANCE = 0.25


def test_connector_faces_flush_with_the_top_and_bottom_faces():
    """Terminals end 0.2 mm short of the top face: their pads need 0.3 mm to
    the board edge."""
    faces = ef.connector_faces()
    assert abs(faces["J2"] - ef.HIDDEN_H) <= FLUSH_TOLERANCE
    assert abs(faces["J3"] - ef.HIDDEN_H) <= FLUSH_TOLERANCE
    assert abs(faces["J1"]) <= FLUSH_TOLERANCE


def test_knx_plug_top_below_the_shoulder_face():
    plug_top = MAIN_TOP_Z + ef.PLUG_HEIGHT
    assert 0 <= ef.SHOULDER_Z["bottom"] - plug_top <= 1.5


def openings():
    t = ef.terminal_holes()
    knx = [dict(h, y=(h["y"] + 5.3) / 2, w=10.6, h_=h["y"] + 5.3, face="bottom") for h in t["KNX"]]
    screws = [dict(h, w=4.0, h_=4.0, face="top") for h in t["DALI"] + t["AUX"]]
    front = [dict(h, w=3.5, h_=3.5, face="front") for h in ef.led_windows()]
    front += [dict(h, w=3.5, h_=3.5, face="front") for h in ef.button_holes().values()]
    win = ef.oled_window()
    front.append({"x": win["cx"], "y": win["cy"], "w": win["w"], "h_": win["h"], "face": "front"})
    return knx + screws + front


FACES = {
    "bottom": (0.0, ef.SHOULDER_H),
    "top": (ef.HIDDEN_H - ef.SHOULDER_H, ef.HIDDEN_H),
    "front": (ef.FRONT_Y_START + WALL, ef.FRONT_Y_END - WALL),
}


def opening_violations(items):
    bad = []
    half_w = ef.WIDTH / 2 - WALL
    for o in items:
        y0, y1 = FACES[o["face"]]
        if not (abs(o["x"]) + o["w"] / 2 <= half_w and y0 <= o["y"] - o["h_"] / 2
                and o["y"] + o["h_"] / 2 <= y1):
            bad.append(("off-face", o))
    for i, a in enumerate(items):
        for b in items[i + 1:]:
            if a["face"] == b["face"] and abs(a["x"] - b["x"]) < (a["w"] + b["w"]) / 2 \
                    and abs(a["y"] - b["y"]) < (a["h_"] + b["h_"]) / 2:
                bad.append(("overlap", a, b))
    return bad


def test_openings_sit_on_their_faces_without_overlap():
    assert not opening_violations(openings())


def test_opening_check_catches_a_knx_opening_outside_the_shoulder():
    items = openings()
    items[0] = dict(items[0], y=ef.SHOULDER_H + 3)
    assert opening_violations(items)


def contact_collisions(points, comps, side, half, to_enc):
    """Case contact points must miss the parts on the side they touch and the
    soldered pin ends of through-hole parts on the other side."""
    hits = []
    for p in points:
        for ref, c in comps.items():
            b = c["bbox"]
            (x0, y0), (x1, y1) = to_enc(b["x0"], b["y0"]), to_enc(b["x1"], b["y1"])
            if c["side"] == side and x0 < p["x"] + half and p["x"] - half < x1 \
                    and y0 < p["y"] + half and p["y"] - half < y1:
                hits.append(ref)
            pins = [to_enc(x, y) for x, y in c["tht_pads"]]
            if c["side"] != side and any(abs(x - p["x"]) < half + PIN_RADIUS and abs(y - p["y"]) < half + PIN_RADIUS
                                         for x, y in pins):
                hits.append(f"{ref} pins")
    return hits


PIN_RADIUS = 1.6
CONTACTS = [("legs", "main", "back", 2.5), ("pegs", "main", "front", 1.5), ("standoffs", "ui", "front", 1.5)]


@pytest.mark.parametrize("kind,board,side,half", CONTACTS)
def test_case_contacts_miss_parts_and_pins(kind, board, side, half):
    to_enc = ef.main_to_enc if board == "main" else ef.ui_to_enc
    assert not contact_collisions(getattr(ef, kind)(), ef.components(board), side, half, to_enc)


def test_contact_check_catches_a_leg_under_a_terminal_pin():
    comps = ef.components("main")
    x, y = ef.main_to_enc(*comps["J2"]["tht_pads"][0])
    assert contact_collisions([{"x": x, "y": y}], comps, "back", 2.5, ef.main_to_enc) == ["J2 pins"]


def test_main_board_supported_in_all_four_quadrants():
    pts = ef.legs()
    quadrants = {(p["x"] > 0, p["y"] > ef.HIDDEN_H / 2) for p in pts}
    assert len(quadrants) == 4


@pytest.mark.parametrize("ref,hole", [("SW1", "PROG"), ("SW2", "RESET")])
def test_button_holes_follow_the_buttons(ref, hole):
    sw = ef.components("ui")[ref]
    x, y = ef.ui_to_enc(sw["x"], sw["y"])
    h = ef.button_holes()[hole]
    assert abs(h["x"] - x) <= 0.01 and abs(h["y"] - y) <= 0.01


THROUGH_WALL = {"main": {"J1", "J2", "J3"}, "ui": set()}


def wall_crossings(board, comps):
    """Parts other than the connectors, which may pass through their case
    openings, must stay inside the walls."""
    half = ef.WIDTH / 2 - WALL
    low, high = (WALL, ef.HIDDEN_H - WALL) if board == "main" else \
        (ef.FRONT_Y_START + WALL, ef.FRONT_Y_END - WALL)
    bad = []
    for ref, c in comps.items():
        if ref in THROUGH_WALL[board]:
            continue
        x, y = zip(*[ef.main_to_enc(c["bbox"][f"x{i}"], c["bbox"][f"y{i}"]) if board == "main"
                     else ef.ui_to_enc(c["bbox"][f"x{i}"], c["bbox"][f"y{i}"]) for i in (0, 1)])
        if min(x) < -half or max(x) > half or min(y) < low or max(y) > high:
            bad.append(ref)
    return bad


@pytest.mark.parametrize("board", ["main", "ui"])
def test_part_bodies_stay_inside_the_case_walls(board):
    assert not wall_crossings(board, ef.components(board))


def test_wall_check_catches_a_part_overhanging_the_top_edge():
    comps = ef.components("main")
    jp1 = dict(comps["JP1"], bbox=dict(comps["JP1"]["bbox"], y1=ef.MAIN_H + 1))
    assert wall_crossings("main", dict(comps, JP1=jp1)) == ["JP1"]


def slot_misses(slots, comps):
    misses = []
    for ref, slot in zip(("J2", "J3"), slots):
        pads_x = [ef.main_to_enc(x, y)[0] for x, y in ef.pad_points(comps[ref])]
        if not (slot["x0"] <= min(pads_x) and max(pads_x) <= slot["x1"]
                and slot["z0"] >= MAIN_TOP_Z and slot["z1"] <= ef.BASE_TOP_Z):
            misses.append(ref)
    return misses


def test_wire_slots_face_the_terminal_wire_openings():
    assert not slot_misses(ef.wire_entries(), ef.components("main"))


def test_wire_slot_check_catches_a_slot_beside_the_terminal():
    slots = [dict(s, x0=s["x0"] + 12, x1=s["x1"] + 12) for s in ef.wire_entries()]
    assert slot_misses(slots, ef.components("main"))


CABLE_LENGTH = 101.6
CABLE_BEND_ALLOWANCE = 20.0


def connector_gap():
    j4 = ef.components("main")["J4"]
    j1 = ef.components("ui")["J1"]
    (mx, my), (ux, uy) = ef.main_to_enc(j4["x"], j4["y"]), ef.ui_to_enc(j1["x"], j1["y"])
    dz = (SPEC["front_ui"]["board_z"]) - MAIN_TOP_Z
    return ((mx - ux) ** 2 + (my - uy) ** 2 + dz ** 2) ** 0.5


def test_ui_cable_reaches_with_slack():
    assert connector_gap() + CABLE_BEND_ALLOWANCE <= CABLE_LENGTH


def test_pegs_sit_under_a_shoulder_plate():
    """Under the front protrusion there is no plate a peg could hang from."""
    assert not [p for p in ef.pegs() if ef.FRONT_Y_START - 1.5 < p["y"] < ef.FRONT_Y_END + 1.5]
