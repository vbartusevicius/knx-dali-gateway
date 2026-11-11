"""The XIAO's USB-C must be reachable for service with main out of the base:
its USB end faces the nearest board edge, and nothing on the front between
that end and the edge stands taller than a USB-C plug's underside."""

import pcbnew
import pytest
from board_variant import VARIANT, layout_path, layout_plan

PLUG_HALF_WIDTH = 12.35 / 2   # USB-C plug overmould, max width
PLUG_UNDERSIDE = 4.9          # above the main board's front face, for the XIAO on 5 mm sockets


@pytest.mark.skipif(VARIANT != "v3", reason="v3 carries the XIAO on main")
def test_usb_c_end_has_a_free_plug_path_to_the_board_edge():
    board = pcbnew.LoadBoard(str(layout_path("main")))
    xiao = board.FindFootprintByReference("U8")
    pads = {p.GetNumber(): p.GetPosition() for p in xiao.Pads()}
    usb = (pads["1"] + pads["14"]) / 2
    centre = xiao.GetPosition()
    edge = board.GetBoardEdgesBoundingBox()
    towards = 1 if usb.x > centre.x else -1
    edge_x = edge.GetRight() if towards > 0 else edge.GetLeft()
    assert abs(edge_x - usb.x) < abs(edge_x - centre.x), "USB end must face the board edge"
    heights = layout_plan("main").get("heights", {})
    lo, hi = sorted((usb.x, edge_x))
    blocking = []
    for fp in board.Footprints():
        if fp.GetReference() == "U8" or fp.IsFlipped():
            continue
        bb = fp.GetBoundingBox(False, False)
        in_x = bb.GetRight() > lo and bb.GetLeft() < hi
        in_y = abs(pcbnew.ToMM(bb.GetCenter().y - usb.y)) < PLUG_HALF_WIDTH + pcbnew.ToMM(bb.GetHeight()) / 2
        height = heights.get(fp.GetReference(), heights.get("default", 0))
        if in_x and in_y and height > PLUG_UNDERSIDE:
            blocking.append((fp.GetReference(), height))
    assert not blocking
