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
from track_joints import loose_ends


@pytest.mark.parametrize("board", BOARDS)
def test_track_ends_land_on_pads_tracks_or_vias(board):
    bad = loose_ends(pcbnew.LoadBoard(str(layout_path(board))))
    report = [f"{t.GetNetname()} {t.GetLayerName()} end at ({pcbnew.ToMM(end.x):.3f}, {pcbnew.ToMM(end.y):.3f})"
              for t, end in bad]
    assert not report, f"{board}: {len(report)} track ends only graze their pad / track: " + "; ".join(report[:8])


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
