"""Probe the generated case solids as assembled (base and cover together).

- every opening gives a clear line of sight from its part to the outside
- legs, pegs, standoffs and snap hooks actually touch their board
- no case material inside either board's volume
- the main board can drop into the base from the front
"""

import sys
from pathlib import Path

import cadquery as cq
import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "mechanical/scripts"))
import build_enclosure as be
import enclosure_features as ef


@pytest.fixture(scope="module")
def solids():
    return be.build_base().val(), be.build_cover().val()


def solid_at(solids, x, y, z):
    return any(s.isInside(cq.Vector(x, y, z)) for s in solids)


def clear_line(solids, x, y, z0, z1, steps=12):
    return not any(solid_at(solids, x, y, z0 + (z1 - z0) * i / steps) for i in range(steps + 1))


def test_front_openings_clear(solids):
    holes = list(ef.button_holes().values())
    win = ef.oled_window()
    holes.append({"x": win["cx"], "y": win["cy"]})
    blocked = [h for h in holes if not clear_line(solids, h["x"], h["y"], be.FRONT_INNER_Z - 0.2, be.TOTAL_D + 1)]
    assert not blocked


def test_led_windows_are_thin_skins_over_open_pockets(solids):
    skin = be.STACK.led_skin_mm
    for w in ef.led_windows():
        assert solid_at(solids, w["x"], w["y"], be.TOTAL_D - skin / 2)
        assert not solid_at(solids, w["x"], w["y"], be.TOTAL_D - skin - 0.1)
        assert clear_line(solids, w["x"], w["y"], be.STACK.led_top_z + 0.05, be.TOTAL_D - skin - 0.1)


def test_each_led_sits_in_a_closed_baffle(solids):
    import math
    radius = be.LED_WINDOW / 2 + be.BAFFLE_WALL / 2
    z = (be.STACK.baffle_bottom_z + be.FRONT_INNER_Z) / 2
    for w in ef.led_windows():
        for k in range(12):
            a = 2 * math.pi * k / 12
            assert solid_at(solids, w["x"] + radius * math.cos(a), w["y"] + radius * math.sin(a), z)


def test_shoulder_openings_clear(solids):
    holes = [h for name in ("KNX", "DALI", "AUX") for h in ef.terminal_holes()[name]]
    blocked = [h for h in holes if not ((clear_line_y(solids, 2.0, 71.0, ef.HIDDEN_H + 1, h["z"])
                                         if h["y"] > ef.HIDDEN_H / 2
                                         else clear_line_y(solids, 2.0, -1, 10.0, h["z"])) if "z" in h
                                        else clear_line(solids, h["x"], h["y"], be.MAIN_Z + 10.5,
                                                        ef.SHOULDER_Z[ef.shoulder_of(h["y"])] + 1))]
    assert not blocked


def test_terminal_faces_open_in_the_top_wall(solids):
    for s in ef.wire_entries():
        for fx in (0.15, 0.5, 0.85):
            for fz in (0.15, 0.5, 0.85):
                x, z = s["x0"] + (s["x1"] - s["x0"]) * fx, s["z0"] + (s["z1"] - s["z0"]) * fz
                assert not solid_at(solids, x, ef.HIDDEN_H - be.WALL / 2, z), s["ref"]


def test_jp1_stays_covered(solids):
    jp1 = ef.components("main")["JP1"]
    x, y, z = ef.board_point("main", jp1["x"], jp1["y"])
    assert not clear_line(solids, x, y, z if ef.VARIANT == "v3" else be.MAIN_Z, be.TOTAL_D + 1, steps=200)


def test_front_wall_is_solid_between_openings(solids):
    assert solid_at(solids, 0, ef.FRONT_Y_END - be.WALL - 0.5, be.TOTAL_D - be.WALL / 2)


def test_blocked_line_is_detected(solids):
    assert not clear_line(solids, 0, ef.FRONT_Y_END - be.WALL - 0.5, be.UI_BOARD_TOP_Z + 4, be.TOTAL_D + 1)


def test_legs_touch_the_main_board(solids):
    bottom = be.MAIN_BOTTOM_Z
    for leg in ef.legs():
        assert solid_at(solids, leg["x"], leg["y"], bottom - 0.05)


def test_pegs_touch_the_main_board(solids):
    for peg in ef.pegs():
        assert solid_at(solids, peg["x"], peg["y"], be.MAIN_Z + 0.05)


def test_standoffs_and_snap_hooks_hold_the_ui_board(solids):
    for s in ef.standoffs():
        assert solid_at(solids, s["x"], s["y"], be.UI_BOARD_TOP_Z + 0.05)
    edge = be.UI_W / 2 - 0.2
    for y in be.SNAP["y"]:
        assert solid_at(solids, edge, y, be.UI_BOARD_Z - 0.05)
        assert solid_at(solids, -edge, y, be.UI_BOARD_Z - 0.05)


def test_ui_snap_tongues_are_free_to_spring_out(solids):
    wall_x = (be.INNER_W + be.WIDTH) / 4
    z_free = be.UI_BOARD_Z - be.SNAP["free_end_below_board"]
    for y in be.SNAP["y"]:
        for side in (1, -1):
            x = side * wall_x
            assert solid_at(solids, x, y, (z_free + be.FRONT_INNER_Z) / 2)
            assert not solid_at(solids, x, y + be.SNAP["width"] / 2 + be.SNAP["slot"] / 2, z_free + 2)
            assert not solid_at(solids, x, y, z_free - be.SNAP["slot"] / 2)


def test_ui_board_slides_in_past_the_hook_ramps(solids):
    """At the hooks' lowest point the ramp leaves the board edge free; the
    tongue only has to bend by the overlap."""
    edge = be.UI_W / 2 - 0.05
    z_low = be.UI_BOARD_Z - be.SNAP["hook_height"] + 0.05
    for y in be.SNAP["y"]:
        assert not solid_at(solids, edge, y, z_low)


def board_volume_hits(solids, x0, x1, y0, y1, z0, z1, n=9):
    return [(x, y) for i in range(n) for j in range(n)
            for x in [x0 + (x1 - x0) * (i + 0.5) / n] for y in [y0 + (y1 - y0) * (j + 0.5) / n]
            if solid_at(solids, x, y, (z0 + z1) / 2)]


def test_no_material_inside_the_boards(solids):
    for board in ef.SPEC["variants"][ef.VARIANT]["boards"]:
        for s in ef.board_slabs(board):
            assert not board_volume_hits(solids, s["x0"], s["x1"], s["y0"], s["y1"], s["z0"], s["z1"]), board


def test_main_board_drops_into_the_base(solids):
    base = solids[:1]
    for s in ef.board_slabs("main"):
        assert not board_volume_hits(base, s["x0"], s["x1"], s["y0"], s["y1"], s["z1"], ef.HIDDEN_D, n=15)


def box_points(b, n=3):
    return [(b["x0"] + (b["x1"] - b["x0"]) * (i + 0.5) / n, b["y0"] + (b["y1"] - b["y0"]) * (j + 0.5) / n,
             b["z0"] + (b["z1"] - b["z0"]) * (k + 0.5) / n) for i in range(n) for j in range(n) for k in range(n)]


def part_hits(solids, boxes):
    hits = []
    for b in boxes:
        if b["through_front"]:
            points = [((b["x0"] + b["x1"]) / 2, (b["y0"] + b["y1"]) / 2, b["z0"] + (b["z1"] - b["z0"]) * k / 4)
                      for k in range(5)]
        else:
            points = box_points(b)
        if any(solid_at(solids, *p) for p in points):
            hits.append(b["ref"])
    return hits


def test_case_misses_every_part(solids):
    assert not part_hits(solids, ef.part_boxes())


def test_part_collision_is_detected(solids):
    plug = next(b for b in ef.part_boxes() if (b["board"], b["ref"]) in ef.PLUG_REFS and b["ref"] == "J1")
    shifted = dict(plug, x0=plug["x0"] - 2, x1=plug["x1"] - 2) if ef.VARIANT == "v3" else         dict(plug, x0=plug["x0"] + 4, x1=plug["x1"] + 4)
    assert part_hits(solids, [shifted]) == ["J1"]


def test_rail_channel_matches_ts35(solids):
    low, high = be.rail_edges()
    assert abs((high - low) - 35.4) < 0.01
    lip = be.RAIL["lip"]
    for y in (low + 0.5, low + 3.5, high - 0.5, high - 3.5):
        for x in (-20, 0, 20):
            assert not solid_at(solids, x, y, -lip / 2), (x, y)
    assert not solid_at(solids, 0, be.RAIL["centre"], -be.BACK_DEPTH / 2)


def test_back_is_at_most_5mm_deep(solids):
    x = ef.WIDTH / 2 - 2
    assert not solid_at(solids, x, 5, -be.BACK_DEPTH - 0.2)
    assert solid_at(solids, x, 5, -be.BACK_DEPTH + 0.2)


def test_hook_and_latch_reach_behind_the_rail_lips(solids):
    low, high = be.rail_edges()
    z0, z1 = be.lip_z()
    assert solid_at(solids, 0, high - 0.75, (z0 + z1) / 2)
    assert solid_at(solids, 0, low + 0.5, z1 - 0.3)


def test_latch_lip_has_a_lead_in(solids):
    low, _ = be.rail_edges()
    z0, _ = be.lip_z()
    assert not solid_at(solids, 0, low + be.HOOK_REACH - 0.2, z0 + 0.2)


def test_latch_slides_on_its_flexures(solids):
    l = be.LATCH
    w, g, b = l["half_width"], l["gap"], l["beam"]
    z = -be.BACK_DEPTH / 2
    assert not solid_at(solids, w + g / 2, 16, z)
    assert not solid_at(solids, w + g / 2, -2, z)
    y_beam = l["beam_ys"][0]
    x_beam = (w + l["beam_reach"]) / 2
    assert solid_at(solids, x_beam, y_beam + b / 2, z)
    assert not solid_at(solids, x_beam, (l["beam_ys"][0] + l["beam_ys"][1]) / 2, z)
    assert not solid_at(solids, x_beam, y_beam - l["travel"] / 2, z)


def test_latch_tab_sticks_out_below_the_case_with_a_front_facing_hole(solids):
    l = be.LATCH
    hole_w, _hole_h = l["hole"]
    z = -be.BACK_DEPTH / 2
    assert solid_at(solids, hole_w / 2 + 1.0, l["hole_y"], z)
    assert solid_at(solids, 0, l["tab_bottom"] + 0.5, z)
    assert not solid_at(solids, 0, l["tab_bottom"] - 0.5, z)
    for z in (-be.BACK_DEPTH + 0.2, -be.BACK_DEPTH / 2, -0.6):
        assert not solid_at(solids, 0, l["hole_y"], z)
    assert not solid_at(solids, 0, l["hole_y"], 0.5)


def clear_line_y(solids, x, y0, y1, z, steps=10):
    return not any(solid_at(solids, x, y0 + (y1 - y0) * i / steps, z) for i in range(steps + 1))


def test_each_printed_part_is_one_piece(solids):
    for part in solids:
        assert len(part.Solids()) == 1


def test_knx_recess_is_open_to_front_and_bottom(solids):
    x0, x1, y1 = be.knx_recess()
    x = (x0 + x1) / 2
    if ef.VARIANT == "v3":
        for h in ef.terminal_holes()["KNX"] + ef.terminal_holes()["AUX"]:
            assert not solid_at(solids, 2.0, be.WALL / 2, h["z"])
            assert not solid_at(solids, be.WIDTH / 2 - be.WALL / 2, 6.0, h["z"])
        return
    assert not solid_at(solids, x, be.WALL / 2, ef.BASE_TOP_Z - 1)
    assert not solid_at(solids, x, y1 - 1, ef.SHOULDER_Z["bottom"] - be.WALL / 2)


def test_case_screws_enter_from_the_wire_sides(solids):
    base, cover = solids
    z = be.CASE_SCREW["z"]
    for tab in ef.screw_tabs():
        outer, wall_in, _end = be.screw_axis_y(tab)
        step = 1 if tab["side"] == "bottom" else -1
        assert not solid_at(solids, tab["x"], outer + step * be.WALL / 2, z)
        assert not solid_at(solids, tab["x"], wall_in + step * 3, z)
        assert cover.isInside(cq.Vector(tab["x"] + be.CASE_SCREW["pilot"] / 2 + 0.8, wall_in + step * 3, z))
        assert base.isInside(cq.Vector(tab["x"] + be.CASE_SCREW["clearance"] / 2 + 0.5, outer + step * be.WALL / 2, z))
