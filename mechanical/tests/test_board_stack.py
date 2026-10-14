"""Stacked boards clear each other's parts, and board-to-board links have a
free path that crosses no third board."""

import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "mechanical/scripts"))
import enclosure_features as ef
from ui_stackup import OLED

BOARDS = ef.SPEC["variants"][ef.VARIANT]["boards"]
LINKS = [link for link in ef.SPEC["boards"].get("interconnects", []) if "refs" in link]
CLEARANCE = 0.5


def overlaps(a, b, margin=0.0):
    return all(a[f"{k}0"] < b[f"{k}1"] + margin and b[f"{k}0"] < a[f"{k}1"] + margin for k in "xyz")


def test_boards_do_not_overlap():
    slabs = [s for b in BOARDS for s in ef.board_slabs(b)]
    assert not [(a["board"], b["board"]) for i, a in enumerate(slabs) for b in slabs[i + 1:]
                if a["board"] != b["board"] and overlaps(a, b)]


def test_parts_clear_other_boards():
    hits = [(p["board"], p["ref"], board) for p in ef.part_boxes() for board in BOARDS
            for slab in ef.board_slabs(board)
            if board != p["board"] and overlaps(p, slab, CLEARANCE)]
    assert not hits


def test_parts_of_different_boards_do_not_collide():
    parts = ef.part_boxes()
    hits = [(a["board"], a["ref"], b["board"], b["ref"]) for i, a in enumerate(parts) for b in parts[i + 1:]
            if a["board"] != b["board"] and overlaps(a, b)]
    assert not hits


B2B_LINKS = [link for link in LINKS if link["type"] == "board-to-board"]
PIN_TOLERANCE = 0.05


def pin_positions(board, ref):
    comp = ef.components(board)[ref]
    return {num: ef.board_to_enc(board, x, y) for x, y, num in comp["pads"]}


@pytest.mark.parametrize("link", B2B_LINKS, ids=lambda link: "-".join(link["refs"].values()))
def test_board_to_board_pins_line_up(link):
    """Header pin n sits exactly over socket contact n."""
    (a_board, a_ref), (b_board, b_ref) = link["refs"].items()
    a, b = pin_positions(a_board, a_ref), pin_positions(b_board, b_ref)
    assert set(a) == set(b)
    off = {n: (round(a[n][0] - b[n][0], 3), round(a[n][1] - b[n][1], 3)) for n in a
           if abs(a[n][0] - b[n][0]) > PIN_TOLERANCE or abs(a[n][1] - b[n][1]) > PIN_TOLERANCE}
    assert not off


@pytest.mark.parametrize("link", B2B_LINKS, ids=lambda link: "-".join(link["refs"].values()))
def test_board_to_board_stack_seats(link):
    """The carried board sits on the header plastic, which sits on the socket."""
    boxes = {(p["board"], p["ref"]): p for p in ef.part_boxes()}
    socket, header = (boxes[item] for item in link["refs"].items())
    assert abs(header["z0"] - socket["z1"]) <= 0.1


def test_parts_under_the_oled_clear_the_module():
    module = ef.oled_module()
    x0, x1 = module["cx"] - module["w"] / 2, module["cx"] + module["w"] / 2
    y0, y1 = module["cy"] - module["h"] / 2, module["cy"] + module["h"] / 2
    limit = OLED["header_gap"] - 0.3
    tall = [(p["ref"], p["z1"] - p["z0"]) for p in ef.part_boxes()
            if p["board"] == "ui" and p["ref"] not in ("J2",) and not p["through_front"]
            and p["x0"] < x1 and x0 < p["x1"] and p["y0"] < y1 and y0 < p["y1"]
            and p["z0"] >= ef.SPEC["front_ui"]["board_top_z"] - 0.01 and p["z1"] - p["z0"] > limit]
    assert not tall


@pytest.mark.parametrize("link", LINKS, ids=lambda link: "-".join(link["refs"]))
def test_link_path_crosses_no_other_board(link):
    corridor = ef.interconnect_corridor(link)
    blocking = [b for b in BOARDS for slab in ef.board_slabs(b) if b not in link["refs"] and overlaps(corridor, slab)]
    assert not blocking
