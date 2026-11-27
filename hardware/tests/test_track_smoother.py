"""Unit and board tests for the DRC-safe octilinear track smoother."""

import math
import sys
from pathlib import Path

import pcbnew
import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "hardware/scripts"))
import track_smoother as ts


def _make_board(w=30.0, h=30.0, edge_clr=0.3):
    board = pcbnew.BOARD()
    board.GetDesignSettings().m_CopperEdgeClearance = pcbnew.FromMM(edge_clr)
    pts = [(0.0, 0.0), (w, 0.0), (w, h), (0.0, h)]
    for a, b in zip(pts, pts[1:] + pts[:1]):
        seg = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
        seg.SetLayer(pcbnew.Edge_Cuts)
        seg.SetWidth(pcbnew.FromMM(0.1))
        seg.SetStart(pcbnew.VECTOR2I_MM(*a))
        seg.SetEnd(pcbnew.VECTOR2I_MM(*b))
        board.Add(seg)
    return board


def _add_net(board, name):
    net = pcbnew.NETINFO_ITEM(board, name)
    board.Add(net)
    return board.FindNet(name)


def _add_track(board, net, x0, y0, x1, y1, w=0.2, layer=pcbnew.F_Cu, locked=False):
    t = pcbnew.PCB_TRACK(board)
    t.SetStart(pcbnew.VECTOR2I_MM(x0, y0))
    t.SetEnd(pcbnew.VECTOR2I_MM(x1, y1))
    t.SetWidth(pcbnew.FromMM(w))
    t.SetLayer(layer)
    t.SetLocked(locked)
    board.Add(t)
    t.SetNet(net)
    return t


def _add_pad(board, ref, net, x, y, size=1.0, shape=pcbnew.PAD_SHAPE_CIRCLE, drill=0.0, npth=False):
    fp = pcbnew.FOOTPRINT(board)
    fp.SetReference(ref)
    fp.SetPosition(pcbnew.VECTOR2I_MM(x, y))
    pad = pcbnew.PAD(fp)
    pad.SetNumber("1")
    pad.SetShape(shape)
    pad.SetSize(pcbnew.VECTOR2I_MM(size, size))
    if npth:
        pad.SetAttribute(pcbnew.PAD_ATTRIB_NPTH)
        pad.SetDrillSize(pcbnew.VECTOR2I_MM(drill or size, drill or size))
        pad.SetLayerSet(pad.UnplatedHoleMask())
    elif drill > 0:
        pad.SetAttribute(pcbnew.PAD_ATTRIB_PTH)
        pad.SetDrillSize(pcbnew.VECTOR2I_MM(drill, drill))
        pad.SetLayerSet(pad.PTHMask())
    else:
        pad.SetAttribute(pcbnew.PAD_ATTRIB_SMD)
        pad.SetLayerSet(pad.SMDMask())
    fp.Add(pad)
    board.Add(fp)
    if net is not None:
        pad.SetNet(net)
    return pad


def _add_keepout(board, x0, y0, x1, y1, layer=pcbnew.F_Cu):
    z = pcbnew.ZONE(board)
    z.SetIsRuleArea(True)
    z.SetDoNotAllowTracks(True)
    z.SetLayer(layer)
    ol = z.Outline()
    ol.NewOutline()
    for px, py in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
        ol.Append(pcbnew.FromMM(px), pcbnew.FromMM(py))
    board.Add(z)
    return z


def _tracks_of(board, net_name):
    return [t for t in board.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T and t.GetNetname() == net_name]


def _total_len_mm(tracks):
    return sum(pcbnew.ToMM(t.GetLength()) for t in tracks)


def test_collapses_micro_staircase_and_v_detour_with_endpoint_snap():
    """Micro-staircase + V-detour with 0.010 mm endpoint drift collapses to one straight segment."""
    board = _make_board()
    sig = _add_net(board, "SIG1")
    _add_pad(board, "U1", sig, 5.0, 10.0, size=0.8)
    _add_pad(board, "U2", sig, 15.0, 10.0, size=0.8)
    # Staircase / V-detour with a 0.010 mm (10 um < 15 um snap) drift between segments 2 & 3
    _add_track(board, sig, 5.0, 10.0, 7.0, 10.0)
    _add_track(board, sig, 7.0, 10.0, 9.0, 12.0)
    _add_track(board, sig, 9.008, 12.005, 11.0, 12.0)
    _add_track(board, sig, 11.0, 12.0, 13.0, 10.0)
    _add_track(board, sig, 13.0, 10.0, 15.0, 10.0)

    chains, before, after, saved = ts.smooth_tracks(board)
    trs = _tracks_of(board, "SIG1")
    assert chains == 1
    assert before == 5
    assert after == 1
    assert len(trs) == 1
    assert saved > 1.5
    assert math.isclose(_total_len_mm(trs), 10.0, abs_tol=0.02)


def test_preserves_degree2_chain_across_mid_chain_width_neckdown():
    """Width necking near a pad must NOT split the chain; uses min(chain_widths)."""
    board = _make_board()
    sig = _add_net(board, "NECK")
    _add_pad(board, "P1", sig, 5.0, 10.0, size=0.6)
    _add_pad(board, "P2", sig, 12.0, 13.0, size=0.6)
    # 0.12 mm necked stub into 0.20 mm staircase jogs
    _add_track(board, sig, 5.0, 10.0, 6.0, 10.0, w=0.12)
    _add_track(board, sig, 6.0, 10.0, 6.0, 12.0, w=0.20)
    _add_track(board, sig, 6.0, 12.0, 9.0, 12.0, w=0.20)
    _add_track(board, sig, 9.0, 12.0, 9.0, 13.0, w=0.20)
    _add_track(board, sig, 9.0, 13.0, 12.0, 13.0, w=0.20)

    chains, before, after, saved = ts.smooth_tracks(board)
    trs = _tracks_of(board, "NECK")
    assert chains == 1
    assert before == 5
    assert after == 2   # clean 45 deg + H dogleg
    assert saved > 1.0
    assert all(t.GetWidth() == pcbnew.FromMM(0.12) for t in trs)


def test_circular_pad_uses_exact_radius_not_inflated_bbox():
    """A 45-deg shortcut clearing a circular THT pad by exact radius (0.24 mm > 0.212 mm)
    but intersecting its sqrt(2)-inflated bounding box must be ACCEPTED."""
    board = _make_board()
    sig = _add_net(board, "SIG_DIAG")
    other = _add_net(board, "OTHER_PIN")
    _add_pad(board, "P1", sig, 5.0, 5.0, size=0.8)
    _add_pad(board, "P2", sig, 15.0, 15.0, size=0.8)
    # Circular pad (r = 1.0 mm) centered at (11.0, 9.0):
    # Diagonal y=x has exact center-to-line distance |11-9|/sqrt(2) = 1.414 mm -> gap = 1.414 - 1.0 - 0.10 = 0.314 mm
    # (> 0.212 mm clearance!), while its square bbox [10..12, 8..10] touches (10, 10) right ON the line (gap = 0)!
    _add_pad(board, "J9", other, 11.0, 9.0, size=2.0, shape=pcbnew.PAD_SHAPE_CIRCLE, drill=0.8)

    # Detour around the top-left of the pad
    _add_track(board, sig, 5.0, 5.0, 5.0, 15.0, w=0.2)
    _add_track(board, sig, 5.0, 15.0, 15.0, 15.0, w=0.2)

    chains, before, after, saved = ts.smooth_tracks(board)
    assert chains == 1
    assert before == 2
    assert after == 1
    assert saved > 5.0


def test_rejects_shortcut_through_keepout_and_npth_hole_and_other_net():
    """Shortcuts crossing rule-area keepouts, unnetted mounting holes, or isolation gaps are rejected."""
    board = _make_board()
    sig = _add_net(board, "SIG_KO")
    _add_pad(board, "P1", sig, 4.0, 10.0, size=0.6)
    _add_pad(board, "P2", sig, 16.0, 10.0, size=0.6)
    _add_keepout(board, 8.5, 9.0, 11.5, 11.0)
    # V-detour around the keepout
    _add_track(board, sig, 4.0, 10.0, 7.0, 13.0)
    _add_track(board, sig, 7.0, 13.0, 13.0, 13.0)
    _add_track(board, sig, 13.0, 13.0, 16.0, 10.0)

    chains, _, _, _ = ts.smooth_tracks(board)
    assert chains == 0
    assert len(_tracks_of(board, "SIG_KO")) == 3

    # Now test unnetted mounting hole blocking a straight shortcut
    board2 = _make_board()
    sig2 = _add_net(board2, "SIG_HOLE")
    _add_pad(board2, "P1", sig2, 4.0, 10.0, size=0.6)
    _add_pad(board2, "P2", sig2, 16.0, 10.0, size=0.6)
    _add_pad(board2, "H1", None, 10.0, 10.0, size=2.0, drill=2.0, npth=True)
    _add_track(board2, sig2, 4.0, 10.0, 7.0, 13.0)
    _add_track(board2, sig2, 7.0, 13.0, 13.0, 13.0)
    _add_track(board2, sig2, 13.0, 13.0, 16.0, 10.0)
    chains2, _, _, _ = ts.smooth_tracks(board2)
    assert chains2 == 0


def test_skips_plane_nets_and_caps_tuned_net_reduction():
    """Plane nets (GND) are never smoothed; tuned nets cap per-shortcut length reduction."""
    board = _make_board()
    gnd = _add_net(board, "GND")
    _add_pad(board, "P1", gnd, 5.0, 10.0, size=0.8)
    _add_pad(board, "P2", gnd, 15.0, 10.0, size=0.8)
    _add_track(board, gnd, 5.0, 10.0, 10.0, 14.0)
    _add_track(board, gnd, 10.0, 14.0, 15.0, 10.0)

    tuned = _add_net(board, "DIFF_P")
    _add_pad(board, "P3", tuned, 5.0, 20.0, size=0.8)
    _add_pad(board, "P4", tuned, 15.0, 20.0, size=0.8)
    # Large 4 mm meander (reduction ~2.8 mm > 1.0 mm cap -> preserved!)
    _add_track(board, tuned, 5.0, 20.0, 8.0, 20.0)
    _add_track(board, tuned, 8.0, 20.0, 8.0, 22.0)
    _add_track(board, tuned, 8.0, 22.0, 12.0, 22.0)
    _add_track(board, tuned, 12.0, 22.0, 12.0, 20.0)
    _add_track(board, tuned, 12.0, 20.0, 15.0, 20.0)

    chains, _, _, _ = ts.smooth_tracks(board, tuned_nets={"DIFF_P"}, max_tuned_reduction_mm=1.0)
    assert len(_tracks_of(board, "GND")) == 2
    assert _total_len_mm(_tracks_of(board, "DIFF_P")) >= 13.9
    assert chains == 0


def test_rejects_same_net_self_crossing():
    """A shortcut that would cross another branch of the same net must be rejected."""
    board = _make_board()
    sig = _add_net(board, "SELF_X")
    _add_pad(board, "P1", sig, 5.0, 10.0, size=0.6)
    _add_pad(board, "P2", sig, 15.0, 10.0, size=0.6)
    _add_pad(board, "P3", sig, 10.0, 6.0, size=0.6)
    _add_pad(board, "P4", sig, 10.0, 11.0, size=0.6)
    # Unrelated same-net vertical trace right across y=10 at x=10
    _add_track(board, sig, 10.0, 6.0, 10.0, 11.0)
    # Detour going over y=13 so it doesn't cross (10, 6)-(10, 11)
    _add_track(board, sig, 5.0, 10.0, 8.0, 13.0)
    _add_track(board, sig, 8.0, 13.0, 12.0, 13.0)
    _add_track(board, sig, 12.0, 13.0, 15.0, 10.0)

    chains, _, _, _ = ts.smooth_tracks(board)
    assert chains == 0


@pytest.mark.parametrize("pair", [((0, 0), (1_000_000, 0)), ((0, 0), (1_000_000, 1_000_000))])
def test_octilinear_candidates_straight_when_aligned(pair):
    cands = ts.octilinear_candidates(*pair)
    assert cands == [[pair]]


def test_octilinear_candidates_two_doglegs_when_off_axis():
    cands = ts.octilinear_candidates((0, 0), (3_000_000, 1_000_000))
    assert len(cands) == 2
    for segs in cands:
        assert len(segs) == 2
        for (ax, ay), (bx, by) in segs:
            dx, dy = abs(bx - ax), abs(by - ay)
            assert dx == 0 or dy == 0 or dx == dy
