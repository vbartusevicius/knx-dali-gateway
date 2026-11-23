"""Every track end must land solidly: on another track end, on another track's
centreline (T joint), inside a pad, inside a via or inside a filled zone of the
same net and layer. An end whose round cap only grazes a pad or track (KiCad
counts that as connected, a fab would make a hairline joint) fails:
layout.seat_track_ends() extends such ends into the pad / onto the track."""

import math
import sys
from pathlib import Path

import pcbnew
import pytest
from board_variant import BOARDS, layout_path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from track_joints import (
    clean_planar_tracks,
    loose_ends,
    planar_anomalies,
    remove_dead_ends,
)


@pytest.mark.parametrize("board", BOARDS)
def test_track_ends_land_on_pads_tracks_or_vias(board):
    bad = loose_ends(pcbnew.LoadBoard(str(layout_path(board))))
    report = [f"{t.GetNetname()} {t.GetLayerName()} end at ({pcbnew.ToMM(end.x):.3f}, {pcbnew.ToMM(end.y):.3f})"
              for t, end in bad]
    assert not report, f"{board}: {len(report)} track ends only graze their pad / track: " + "; ".join(report[:8])


@pytest.mark.parametrize("board", BOARDS)
def test_no_planar_track_anomalies(board):
    """No zero-length segments, collinear overlaps, unsplit crossings or same-net
    planar cycles on any copper layer."""
    issues = planar_anomalies(pcbnew.LoadBoard(str(layout_path(board))))
    assert not issues, f"{board}: {len(issues)} planar track anomalies: " + "; ".join(issues[:8])


def test_a_track_stopping_short_of_its_pad_is_caught():
    """The weather-station case: a track end 0.07 mm short of a pad edge, so only
    its round cap touches the pad."""
    board = pcbnew.BOARD()
    net = pcbnew.NETINFO_ITEM(board, "GND")
    board.Add(net)
    fp = pcbnew.FOOTPRINT(board)
    board.Add(fp)
    pad = pcbnew.PAD(fp)
    pad.SetShape(pcbnew.PAD_SHAPE_RECTANGLE)
    pad.SetAttribute(pcbnew.PAD_ATTRIB_SMD)
    layers = pcbnew.LSET()
    layers.AddLayer(pcbnew.F_Cu)
    pad.SetLayerSet(layers)
    pad.SetSize(pcbnew.VECTOR2I(pcbnew.FromMM(1.0), pcbnew.FromMM(1.0)))
    pad.SetPosition(pcbnew.VECTOR2I(0, 0))
    fp.Add(pad)
    pad.SetNet(net)

    def track(x_end_mm):
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(pcbnew.VECTOR2I(pcbnew.FromMM(-5), 0))
        t.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(x_end_mm), 0))
        t.SetWidth(pcbnew.FromMM(0.25))
        t.SetLayer(pcbnew.F_Cu)
        board.Add(t)
        t.SetNet(net)
        return t

    short = track(-0.5 - 0.07)          # pad edge at -0.5 mm: end outside, cap overlaps by 0.055 mm
    assert [end for _, end in loose_ends(board) if end.x == short.GetEnd().x]
    board.Remove(short)
    track(0.0)                           # into the pad centre: solid
    assert not [end for _, end in loose_ends(board) if math.hypot(end.x - pcbnew.FromMM(-5), end.y) > 1]


def test_dead_end_stub_is_removed():
    """A pad escape the router never joined dangles: remove_dead_ends drops it."""
    board = pcbnew.BOARD()
    stub = pcbnew.PCB_TRACK(board)
    stub.SetStart(pcbnew.VECTOR2I(0, 0))
    stub.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(1), 0))
    stub.SetWidth(pcbnew.FromMM(0.25))
    stub.SetLayer(pcbnew.B_Cu)
    board.Add(stub)
    assert remove_dead_ends(board) == 1
    assert not [t for t in board.GetTracks()]


def test_crossing_segments_and_serpentine_cycle_are_split_and_pruned():
    board = pcbnew.BOARD()
    net = pcbnew.NETINFO_ITEM(board, "SIG")
    board.Add(net)

    def add_seg(x0, y0, x1, y1, w=0.2, locked=False):
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(pcbnew.VECTOR2I(pcbnew.FromMM(x0), pcbnew.FromMM(y0)))
        t.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(x1), pcbnew.FromMM(y1)))
        t.SetWidth(pcbnew.FromMM(w))
        t.SetLayer(pcbnew.F_Cu)
        t.SetLocked(locked)
        board.Add(t)
        t.SetNet(net)

    # Zero-length segment + duplicate reversed overlap + triangle cycle + X crossing
    add_seg(0, 0, 0, 0)
    add_seg(0, 0, 4, 0, w=0.3)
    add_seg(4, 0, 0, 0, w=0.2)
    add_seg(4, 0, 2, 3, w=0.2)
    add_seg(2, 3, 0, 0, w=0.2)
    add_seg(1, -1, 1, 1, w=0.2)
    assert planar_anomalies(board)
    zero, splits, dedup, cycles = clean_planar_tracks(board)
    assert zero == 1
    assert splits >= 1
    assert dedup >= 1
    assert cycles >= 1
    assert not planar_anomalies(board)


def test_dsn_net_classes_include_clearance_margin_and_smd_escape_override():
    import layout

    sample = (
        "(pcb test\n  (network\n"
        "    (class kicad_default A B\n      (circuit\n        (use_via Via[0-3]_600:300_um)\n      )\n"
        "      (rule\n        (width 200)\n        (clearance 200)\n      )\n    )\n"
        "  )\n  (wiring\n  )\n)\n"
    )
    settings = {
        "classes": [
            {"name": "Default", "track_width": 0.2, "clearance": 0.2},
            {"name": "DALI", "track_width": 0.3, "clearance": 0.4},
        ],
        "netclass_patterns": [{"pattern": "B", "netclass": "DALI"}],
    }
    rewritten = layout.with_net_classes(sample, ["A", "B"], settings)
    assert "(clearance 203)" in rewritten
    assert "(clearance 403)" in rewritten
    assert "(clearance 203 (type wire_smd))" in rewritten
    assert "(clearance 203 (type smd_smd))" in rewritten
