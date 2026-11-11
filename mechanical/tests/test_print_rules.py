"""Defects found on the first powder-printed V3 case (photos 2026-11-09):
a cover lip held only by the snap tongues, a DIN latch fused to the base
with a spring too stiff to release, KNX/AUX pockets too tight with the plug
sockets off the pins, the board resting on C4, a cover that pivoted, and a
side wall that dented between its supports. Each test keeps one of them
from coming back.
"""

import sys
from itertools import pairwise
from pathlib import Path

import cadquery as cq
import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "mechanical/scripts"))
import build_enclosure as be
import enclosure_features as ef

V3 = ef.VARIANT in ("v3", "v4")
needs_v3 = pytest.mark.skipif(not V3, reason="vertical-main side-opening case only")


@pytest.fixture(scope="module")
def solids():
    return be.build_base().val(), be.build_cover().val()


def inside(shape, x, y, z):
    return shape.isInside(cq.Vector(x, y, z))


def test_latch_spring_stays_within_the_material_strain():
    assert be.latch_strain(be.LATCH["legs"]) <= be.LATCH_MAX_STRAIN


def test_strain_check_catches_the_first_latch():
    """3 legs of 0.8 mm over 2.4 mm free length, 2 mm travel: ~28 % strain."""
    first = dict(be.LATCH, beam=0.8, travel=2.0, half_width=4.5, gap=0.4, reach=7.5)
    assert be.latch_strain(3, first) > 0.2


def test_latch_gaps_do_not_fuse_in_a_powder_print(solids):
    base, _ = solids
    l = be.LATCH
    assert l["gap"] >= be.MIN_PRINT_GAP
    z = -be.BACK_DEPTH / 2
    for dx in (0.1, l["gap"] - 0.1):
        assert not inside(base, l["half_width"] + dx, be.rail_edges()[0] - 3.0, z)
    tab_x = (l["half_width"] + l["gap"] + l["tab_half_width"]) / 2
    assert not inside(base, tab_x, -l["gap"] / 2, z), "tab must stay MIN_PRINT_GAP below the case"


@needs_v3
def test_cover_lips_are_bonded_to_the_side_wall_along_their_length(solids):
    """The first cover's lip hung 0.15 mm inside the wall and held on only at
    the two snap tongues."""
    _, cover = solids
    lip_x = be.INNER_W / 2 - 0.15 - be.LIP["thickness"] / 2
    z_lip, z_bond = ef.BASE_TOP_Z - be.LIP["depth"] / 2, ef.BASE_TOP_Z + be.LIP_BOND["height"] / 2
    ys = [y / 2 for y in range(int(2 * be.WALL) + 2, int(2 * (ef.HIDDEN_H - be.WALL)) - 2)]
    lip_ys = [y for y in ys if inside(cover, lip_x, y, z_lip)]
    assert lip_ys, "no lip"
    loose = [y for y in lip_ys if not (inside(cover, lip_x, y, z_bond) and
                                       inside(cover, be.INNER_W / 2 + 0.3, y, z_bond))]
    assert not loose, f"lip not tied into the wall at y = {loose[:6]}"


@needs_v3
def test_side_snap_tongues_are_free_to_flex(solids):
    _, cover = solids
    half = be.SIDE_SNAPS["half"]
    for y in be.SIDE_SNAPS["right_y"]:
        for side in (-1, 1):
            for z in (39.6, 41.0, ef.BASE_TOP_Z - 0.2):
                assert not inside(cover, be.INNER_W / 2 - 0.5, y + side * (half + 0.3), z), (y, z)


@needs_v3
def test_cover_clicks_on_at_both_ends_of_both_sides():
    assert len(be.SIDE_SNAPS["right_y"]) >= 2 and len(be.SIDE_SNAPS["left_z"]) >= 2
    right = sorted(be.SIDE_SNAPS["right_y"])
    assert right[0] < ef.HIDDEN_H / 3 and right[-1] > 2 * ef.HIDDEN_H / 3


PLUG_REFS = sorted(ref for _, ref in ef.PLUG_REFS)


@pytest.mark.parametrize("ref", PLUG_REFS)
def test_plug_has_clearance_all_round_in_its_pocket(solids, ref):
    """The plug (measured WAGO 243-211, sockets on the pins) misses the case by
    nearly PLUG_CLEARANCE on every side but the face it seats on. The first
    pockets (v2 and v3) were 0.1-0.4 mm wider than the plug in total and
    2.8 mm off the sockets."""
    plug, c = ef.knx_plug(ref), be.PLUG_CLEARANCE - 0.05
    if V3:
        probe = be.box(plug["x0"] + 0.05, plug["x1"], plug["y0"] - c, plug["y1"] + c, plug["z0"] - c, plug["z1"] + c)
    else:
        probe = be.box(plug["x0"] - c, plug["x1"] + c, plug["y0"] - c, plug["y1"] + c, plug["z0"] + 0.05, plug["z1"])
    for part in solids:
        assert part.intersect(probe.val()).Volume() < 1e-3, ref


@pytest.mark.parametrize("ref", PLUG_REFS)
def test_plug_key_slot_runs_the_length_of_the_pocket(solids, ref):
    plug, c, depth = ef.knx_plug(ref), be.PLUG_CLEARANCE, be.PLUG_KEY["depth"]
    y = plug["y1"] - 4.0
    if V3:
        z = plug["z0"] - c - depth / 2
        points = [(plug["x0"] + 0.5, y, z), (ef.WIDTH / 2 - 0.5, y, z)]
    else:
        x = plug["x1"] + c + depth / 2
        points = [(x, y, plug["z0"] + 1.0), (x, y, plug["z1"] - 0.5)]
    for point in points:
        assert not any(inside(s, *point) for s in solids), (ref, point)


def test_plug_stays_behind_the_shoulder_face():
    """v2's first model (7.6 mm on a 1 mm floor) let the real 11.5 mm plug
    stand 1.5 mm proud of the bottom shoulder."""
    for _, ref in ef.PLUG_REFS:
        if not V3:
            assert ef.knx_plug(ref)["z1"] <= ef.SHOULDER_Z["bottom"], ref


@needs_v3
def test_board_front_rests_on_the_case_in_every_quadrant(solids):
    """The first print had no support in front of main: C4 carried the board."""
    base, _ = solids
    slab = ef.board_slab("main")
    x = slab["x1"] + be.BOARD_SUPPORT_GAP + 0.05
    y_mid, z_mid = (slab["y0"] + slab["y1"]) / 2, (slab["z0"] + slab["z1"]) / 2
    quadrants = set()
    for y in [slab["y0"] + 0.25 * i for i in range(int((slab["y1"] - slab["y0"]) / 0.25))]:
        for z in [slab["z0"] + 0.5 * i for i in range(int((slab["z1"] - slab["z0"]) / 0.5))]:
            if inside(base, x, y, z):
                quadrants.add((y > y_mid, z > z_mid))
    assert len(quadrants) == 4, quadrants


def test_supports_hold_the_board_before_any_front_part_touches_the_case():
    """Supports stop the board BOARD_SUPPORT_GAP from its front face; every
    front part keeps PART_AIR to the case (test_case_misses_every_part_body)."""
    assert be.BOARD_SUPPORT_GAP < ef.PART_AIR


@needs_v3
def test_cover_side_wall_is_backed_at_least_every_40_mm():
    """The first cover's left wall dented between its end ribs."""
    rows = sorted({round(y, 1) for _, y, _ in ef.main_ribs()})
    assert max(b - a for a, b in pairwise(rows)) <= 40.0, rows


@needs_v3
def test_dali_terminal_face_with_its_push_buttons_is_open(solids):
    """Both wire entries and both release buttons sit in the terminal's top
    face; the top-wall opening exposes all of it."""
    j3 = next(b for b in ef.part_boxes("body") if (b["board"], b["ref"]) == ("main", "J3"))
    slot = ef.wire_entries()[0]
    assert slot["x0"] <= j3["x0"] and j3["x1"] <= slot["x1"] and slot["z0"] <= j3["z0"] and j3["z1"] <= slot["z1"]
    for x in (j3["x0"] + 0.3, (j3["x0"] + j3["x1"]) / 2, j3["x1"] - 0.3):
        for z in (j3["z0"] + 0.3, (j3["z0"] + j3["z1"]) / 2, j3["z1"] - 0.3):
            assert not any(inside(s, x, ef.HIDDEN_H - be.WALL / 2, z) for s in solids), (x, z)


def test_reset_bump_carries_a_recessed_x_and_scroll_stays_plain():
    """SW1 (left, SCROLL) is a plain bump, SW2 (right, RESET) has an X cut
    into it: bars wide enough to print, shallower than the bump, longer than
    it so they run out through the rim, and solid bump left between the arms."""
    assert be.BUTTON_MARK == {"SW1": "dot", "SW2": "cross"}
    c, t = be.BUTTON_CROSS, be.BUTTON_TAB
    assert c["bar"] >= 0.4 + 0.1 and c["depth"] < t["boss_height"]
    top = 10.0
    groove = be.button_cross(0, 0, top).val()
    assert groove.BoundingBox().xlen > t["boss"]
    assert groove.isInside(cq.Vector(0, 0, top - c["depth"] / 2))
    assert not groove.isInside(cq.Vector(0, 0, top - c["depth"] - 0.05))
    arm = t["boss"] / 2 * 0.6
    assert not groove.isInside(cq.Vector(arm, 0, top - c["depth"] / 2))


def test_led_baffles_print_and_clear_the_led():
    """Light tubes: walls thick enough for a powder print, neighbours apart, and
    the tube starts above the WS2812B-MINI-V3J's top (1.57 mm) instead of on it."""
    assert be.BAFFLE_WALL >= 0.6
    outer = be.LED_WINDOW + 2 * be.BAFFLE_WALL
    xs = sorted(w["x"] for w in ef.led_windows())
    assert min(b - a for a, b in pairwise(xs)) - outer >= 0.6
    assert be.STACK.baffle_bottom_z >= be.STACK.board_top_z + ef.SPEC["front_ui"]["led_top"] + 0.15
