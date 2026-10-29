"""Track ends that only graze their pad or track (KiCad connects them, the fab
makes a hairline joint): find them and seat them.

    loose_ends(board)        -> [(track, end point)]
    seat_track_ends(board)   -> number of segments added (extends grazing ends into the
                                pad / via centre or onto the other track's centreline)

hardware/tests/test_track_joints.py checks every stored board.
"""

import math

import pcbnew

TOL = pcbnew.FromMM(0.005)


def _nearest(p, t):
    a, b = t.GetStart(), t.GetEnd()
    dx, dy = b.x - a.x, b.y - a.y
    length2 = dx * dx + dy * dy
    u = 0.0 if length2 == 0 else max(0.0, min(1.0, ((p.x - a.x) * dx + (p.y - a.y) * dy) / length2))
    q = pcbnew.VECTOR2I(int(a.x + u * dx), int(a.y + u * dy))
    return q, math.hypot(p.x - q.x, p.y - q.y)


def _items(board):
    tracks = [t for t in board.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T]
    vias = [v for v in board.GetTracks() if v.Type() == pcbnew.PCB_VIA_T]
    return tracks, vias, list(board.GetPads())


def _solid(board, t, end, tracks, vias, pads):
    net, layer = t.GetNetCode(), t.GetLayer()
    if any(o is not t and o.GetNetCode() == net and o.GetLayer() == layer and _nearest(end, o)[1] <= TOL
           for o in tracks):
        return True
    if any(v.GetNetCode() == net and math.hypot(end.x - v.GetPosition().x, end.y - v.GetPosition().y)
           <= v.GetWidth(layer) / 2 for v in vias):
        return True
    if any(p.GetNetCode() == net and p.IsOnLayer(layer) and p.HitTest(end) for p in pads):
        return True
    return any(z.GetNetCode() == net and z.IsOnLayer(layer) and z.HitTestFilledArea(layer, end)
               for z in board.Zones())


def loose_ends(board):
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())   # zone fills as DRC / the fab see them
    tracks, vias, pads = _items(board)
    return [(t, end) for t in tracks for end in (t.GetStart(), t.GetEnd())
            if not _solid(board, t, end, tracks, vias, pads)]


def seat_track_ends(board):
    added = 0
    tracks, vias, pads = _items(board)
    for t, end in loose_ends(board):
        net, layer, w = t.GetNetCode(), t.GetLayer(), t.GetWidth()
        mine = [p for p in pads if p.GetNetCode() == net and p.IsOnLayer(layer)]
        target = next((p.GetPosition() for p in mine if p.HitTest(end, w // 2 + TOL)), None)
        if target is None:
            same = [o for o in tracks if o is not t and o.GetNetCode() == net and o.GetLayer() == layer]
            hits = [(q, d) for o in same for q, d in [_nearest(end, o)] if d <= (w + o.GetWidth()) / 2 + TOL]
            target = min(hits, key=lambda h: h[1])[0] if hits else None
        if target is None:
            target = next((v.GetPosition() for v in vias if v.GetNetCode() == net and
                           math.hypot(end.x - v.GetPosition().x, end.y - v.GetPosition().y)
                           <= v.GetWidth(layer) / 2 + w / 2 + TOL), None)
        if target is None:
            continue
        seg = pcbnew.PCB_TRACK(board)
        seg.SetStart(end)
        seg.SetEnd(target)
        seg.SetWidth(w)
        seg.SetLayer(layer)
        board.Add(seg)
        seg.SetNetCode(net)
        added += 1
    return added
