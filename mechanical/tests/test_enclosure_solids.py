"""Exact solid checks of the assembled case (base + cover) against the boards.

test_enclosure_probe.py samples points; these tests intersect real solids, so
thin interferences (a board 0.1 mm wider than its slot, a diode 0.4 mm into a
wall) fail too. Part bodies use the layout heights, which test_fit.py holds at
or above the datasheet maxima in hardware/product/package_heights.yaml; solder
tails use the same table.
"""

import math
import sys
from pathlib import Path

import cadquery as cq
import pytest
import yaml

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "mechanical/scripts"))
import build_enclosure as be
import enclosure_features as ef

SPEC = ef.SPEC
BOARDS = SPEC["variants"][ef.VARIANT]["boards"]
PACKAGES = yaml.safe_load((REPO / "hardware/product/package_heights.yaml").read_text())
TRIM_THT_TO = SPEC.get("assembly", {}).get("trim_tht_to")
TOLERANCE_MM3 = 1e-2
TAIL_DIAMETER = 1.6
CONTACT_GAP = 0.2
NUB_GAP_MAX = 0.4


@pytest.fixture(scope="module")
def case():
    return be.build_base().union(be.build_cover())


def overlap(a, b):
    try:
        return a.intersect(b).val().Volume()
    except (ValueError, IndexError):
        return 0.0


def solid(b):
    if b.get("shape") == "horizontal_capacitor":
        return be.capacitor_body(b)
    return be.box(b["x0"], b["x1"], b["y0"], b["y1"], b["z0"], b["z1"])


def collisions(case, boxes):
    return {b["ref"]: round(v, 3) for b in boxes for v in [overlap(solid(b), case)] if v > TOLERANCE_MM3}


@pytest.mark.parametrize("board", BOARDS)
def test_no_case_material_inside_a_board(case, board):
    hits = [round(overlap(solid(s), case), 3) for s in ef.board_slabs(board)]
    assert not [v for v in hits if v > TOLERANCE_MM3], hits


def test_case_misses_every_part_body(case):
    boxes = [b for b in ef.part_boxes("body") if not b.get("through_front")]
    assert not collisions(case, boxes)


def test_part_collision_check_catches_a_buried_part(case):
    wall = {"ref": "probe", "x0": -ef.WIDTH / 2 + 0.2, "x1": -ef.WIDTH / 2 + 1.0, "y0": 20, "y1": 22, "z0": 10, "z1": 12}
    assert collisions(case, [wall]) == {"probe": pytest.approx(3.2, abs=0.01)}


def tail_length(footprint, board):
    tail = PACKAGES.get(footprint, {}).get("tail_max")
    if tail is None:
        return None
    if TRIM_THT_TO is not None:
        return min(tail - SPEC["boards"][board]["thickness"], TRIM_THT_TO)
    return tail - SPEC["boards"][board]["thickness"]


def tail_boxes():
    boxes = []
    for board in BOARDS:
        thick = SPEC["boards"][board]["thickness"]
        for ref, c in ef.components(board).items():
            length = tail_length(c["footprint"].split(":")[-1], board)
            if not length:
                continue
            r = TAIL_DIAMETER / 2
            n0, n1 = (0.0, length) if c["side"] == "back" else (-thick - length, -thick)
            for px, py in c["tht_pads"]:
                boxes.append(dict(ef.span_box(ef.board_point(board, px - r, py - r, n0),
                                              ef.board_point(board, px + r, py + r, n1)),
                                  ref=f"{board}/{ref}@{px:.1f},{py:.1f}"))
    return boxes


def test_solder_tails_clear_the_case(case):
    """Through-hole leads stick out of the far side of the board; after the
    trim the variant's assembly list asks for, they must not touch the case."""
    assert not collisions(case, tail_boxes())


def faces_touching(case, board):
    """Case contacts within CONTACT_GAP of the board's back face, as (y, z)."""
    cfg = SPEC["boards"][board]
    back = cfg["x"] - cfg["thickness"]
    contacts = []
    for s in ef.board_slabs(board):
        probe = be.box(back - CONTACT_GAP, back, s["y0"], s["y1"], s["z0"], s["z1"])
        try:
            found = probe.intersect(case).val().Solids()
        except (ValueError, IndexError):
            found = []
        contacts += [(sol.Center().y, sol.Center().z) for sol in found if sol.Volume() > TOLERANCE_MM3]
    return contacts


@pytest.mark.skipif(SPEC["boards"]["main"].get("plane") != "perpendicular-to-rail",
                    reason="horizontal main boards are clamped between legs and pegs (test_enclosure_probe)")
def test_vertical_main_board_is_backed_by_the_case_over_its_area(case):
    """The plug pins push the board towards the cover; ribs on the cover must
    take that load near all four corners instead of parts or solder joints."""
    contacts = faces_touching(case, "main")
    slab = ef.board_slab("main")
    mid_y, mid_z = (slab["y0"] + slab["y1"]) / 2, (slab["z0"] + slab["z1"]) / 2
    quadrants = {(y > mid_y, z > mid_z) for y, z in contacts}
    assert len(quadrants) == 4, contacts


def button_tops():
    ui = ef.components("ui")
    top = SPEC["front_ui"]["board_top_z"]
    return {ref: (*ef.ui_to_enc(ui[ref]["x"], ui[ref]["y"]), top + ui[ref]["height"]) for ref in ("SW1", "SW2")}


def material_at(case, x, y, z):
    return case.val().isInside(cq.Vector(x, y, z))


@pytest.mark.parametrize("ref", ["SW1", "SW2"])
def test_each_button_can_be_pressed_from_the_front(case, ref):
    """Either the actuator itself reaches past the front face, or the cover has
    a plunger whose nub ends just above the actuator, carried by a C-shaped
    front tab: slotted around the pad and along the arm, rooted at the middle
    bridge and thinned so it flexes."""
    x, y, top = button_tops()[ref]
    if top > ef.TOTAL_D:
        return
    assert not material_at(case, x, y, top - 0.02), "plunger presses the button at rest"
    assert any(material_at(case, x, y, top + d / 10) for d in range(1, int(NUB_GAP_MAX * 10) + 1)), \
        "no plunger nub above the actuator"
    assert material_at(case, x, y, ef.TOTAL_D + 0.3), "no finger boss on the front"
    t = be.BUTTON_TAB
    pad_edge_x = x + math.copysign((t["pad"] + t["slot"]) / 2, x)
    arm_edge_y = (x + math.copysign(t["root_x"], x)) / 2, y + (t["arm"] + t["slot"]) / 2
    assert not material_at(case, pad_edge_x, y, ef.TOTAL_D - 0.3), "pad is not slotted on its outer side"
    assert not material_at(case, *arm_edge_y, ef.TOTAL_D - 0.3), "arm is not slotted along its edge"
    assert material_at(case, math.copysign(t["root_x"] / 2, x), y, ef.TOTAL_D - 0.3), "arm is not rooted"
    assert not material_at(case, arm_edge_y[0], y, ef.TOTAL_D - t["thickness"] - 0.1), "arm is not thinned to flex"
