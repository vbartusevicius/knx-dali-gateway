"""Track ends, planar overlaps/crossings and same-net loops: find and clean them.

    loose_ends(board)          -> [(track, end point)]
    planar_anomalies(board)    -> list of zero-length, overlapping/crossing or cyclic track descriptions
    clean_planar_tracks(board) -> (zero_removed, splits_made, deduped, cycles_pruned)
    seat_track_ends(board)     -> number of segments added (extends grazing ends into the
                                  pad / via centre or onto the other track's centreline)
    remove_dead_ends(board)    -> number of dangling stubs removed (unused escapes, leftovers)

hardware/tests/test_track_joints.py checks every stored board.
"""

import itertools
import math

import pcbnew
from shapely.geometry import LineString

TOL = pcbnew.FromMM(0.005)
SNAP = 500  # 0.5 um in nm: vertex identity for planar splitting, deduplication and cycle detection

# Removed PCB_TRACK proxies must outlive the process (KiCad 10 SWIG safety).
_DETACHED = []


def _dist(a, b):
    return math.hypot(a.x - b.x, a.y - b.y)


def _pt_key(p):
    return round(p.x / SNAP), round(p.y / SNAP)


def _nearest(p, t):
    a, b = t.GetStart(), t.GetEnd()
    dx, dy = b.x - a.x, b.y - a.y
    length2 = dx * dx + dy * dy
    u = 0.0 if length2 == 0 else max(0.0, min(1.0, ((p.x - a.x) * dx + (p.y - a.y) * dy) / length2))
    q = pcbnew.VECTOR2I(round(a.x + u * dx), round(a.y + u * dy))
    return q, _dist(p, q)


def _nearest_param(p, a, b):
    dx, dy = b.x - a.x, b.y - a.y
    length2 = dx * dx + dy * dy
    u = 0.0 if length2 == 0 else max(0.0, min(1.0, ((p.x - a.x) * dx + (p.y - a.y) * dy) / length2))
    q = pcbnew.VECTOR2I(round(a.x + u * dx), round(a.y + u * dy))
    return q, u, _dist(p, q)


def _uf_find(parent, x):
    while parent.get(x, x) != x:
        parent[x] = parent.get(parent[x], parent[x])
        x = parent[x]
    return x


def _uf_union(parent, a, b):
    ra, rb = _uf_find(parent, a), _uf_find(parent, b)
    if ra == rb:
        return False
    parent[rb] = ra
    return True


def _items(board):
    tracks = [t for t in board.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T]
    vias = [v for v in board.GetTracks() if v.Type() == pcbnew.PCB_VIA_T]
    return tracks, vias, list(board.GetPads())


def _detach(board, track):
    _DETACHED.append(track)
    board.Remove(track)


def _solid(board, t, end, tracks, vias, pads):
    net, layer = t.GetNetCode(), t.GetLayer()
    uid = t.m_Uuid.AsString()
    if any(o.m_Uuid.AsString() != uid and o.GetNetCode() == net and o.GetLayer() == layer
           and _nearest(end, o)[1] <= TOL for o in tracks):
        return True
    if any(v.GetNetCode() == net and _dist(end, v.GetPosition()) <= v.GetWidth(layer) / 2 for v in vias):
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
        other = t.GetEnd() if end == t.GetStart() else t.GetStart()
        mine = [p for p in pads if p.GetNetCode() == net and p.IsOnLayer(layer)]
        hit_pad = next((p for p in mine if p.HitTest(end, w // 2 + TOL)
                        and _dist(p.GetPosition(), other) > TOL), None)
        target = None
        if hit_pad is not None:
            pc = hit_pad.GetPosition()
            if min(hit_pad.GetSizeX(), hit_pad.GetSizeY()) >= pcbnew.FromMM(1.5):
                d = _dist(end, pc)
                step = min(d, pcbnew.FromMM(0.25))
                cand = pcbnew.VECTOR2I(round(end.x + (pc.x - end.x) * step / d),
                                       round(end.y + (pc.y - end.y) * step / d)) if d > 0 else pc
                target = cand if hit_pad.HitTest(cand) else pc
            else:
                target = pc
        if target is None:
            uid = t.m_Uuid.AsString()   # pcbnew returns new wrappers per call: identity never matches
            same = [o for o in tracks if o.m_Uuid.AsString() != uid and o.GetNetCode() == net and o.GetLayer() == layer]
            hits = [(q, d) for o in same for q, d in [_nearest(end, o)]
                    if d <= (w + o.GetWidth()) / 2 + TOL and _dist(q, other) > TOL and _dist(q, end) > SNAP]
            target = min(hits, key=lambda h: h[1])[0] if hits else None
        if target is None:
            target = next((v.GetPosition() for v in vias if v.GetNetCode() == net and
                           _dist(end, v.GetPosition()) <= v.GetWidth(layer) / 2 + w / 2 + TOL
                           and _dist(v.GetPosition(), other) > TOL), None)
        if target is None or _dist(end, target) <= SNAP:
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


def remove_dead_ends(board):
    """Remove tracks with an end that touches no same-net copper at all, such
    as pad escapes the router did not use or pruning leftovers (repeated until
    none remain). Returns how many were removed."""
    removed = 0
    while True:
        tracks, vias, pads = _items(board)
        dead = []
        for t, end in loose_ends(board):
            net, layer, w = t.GetNetCode(), t.GetLayer(), t.GetWidth()
            other = t.GetEnd() if end == t.GetStart() else t.GetStart()
            uid = t.m_Uuid.AsString()
            touches = any(o.m_Uuid.AsString() != uid and o.GetNetCode() == net and o.GetLayer() == layer
                          and (nd := _nearest(end, o))[1] <= (w + o.GetWidth()) / 2 + TOL
                          and _dist(nd[0], other) > TOL for o in tracks) or \
                any(v.GetNetCode() == net and _dist(end, v.GetPosition()) <= v.GetWidth(layer) / 2 + w / 2 + TOL
                    and _dist(v.GetPosition(), other) > TOL for v in vias) or \
                any(p.GetNetCode() == net and p.IsOnLayer(layer) and p.HitTest(end, w // 2 + TOL)
                    and _dist(p.GetPosition(), other) > TOL for p in pads)
            if not touches:
                dead.append(t)
        if not dead:
            return removed
        for t in {t.m_Uuid.AsString(): t for t in dead}.values():
            _detach(board, t)
            removed += 1


def clean_planar_tracks(board):
    """Split collinear/crossing same-net segments on the same layer, deduplicate
    overlapping segments, and prune redundant planar cycles to a forest."""
    tracks = [t for t in board.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T and t.GetNetCode() != 0]
    zero_removed = 0
    by_nl = {}
    for t in tracks:
        if _pt_key(t.GetStart()) == _pt_key(t.GetEnd()):
            _detach(board, t)
            zero_removed += 1
            continue
        by_nl.setdefault((t.GetNetCode(), t.GetLayer()), []).append(t)

    splits_made = 0
    deduped = 0
    cycles_pruned = 0
    for (net, layer), ts in by_nl.items():
        n = len(ts)
        if n < 2:
            continue
        segs = [(t.GetStart(), t.GetEnd(), LineString([(t.GetStart().x, t.GetStart().y),
                                                       (t.GetEnd().x, t.GetEnd().y)])) for t in ts]
        bboxes = [(min(a.x, b.x) - SNAP, min(a.y, b.y) - SNAP,
                   max(a.x, b.x) + SNAP, max(a.y, b.y) + SNAP) for a, b, _ in segs]
        split_pts = [[] for _ in range(n)]
        for i in range(n):
            ai, bi, li = segs[i]
            bx0, by0, bx1, by1 = bboxes[i]
            for j in range(i + 1, n):
                aj, bj, lj = segs[j]
                cx0, cy0, cx1, cy1 = bboxes[j]
                if bx1 < cx0 or cx1 < bx0 or by1 < cy0 or cy1 < by0:
                    continue
                for p in (aj, bj):
                    _, u, d = _nearest_param(p, ai, bi)
                    if d <= SNAP and _dist(p, ai) > SNAP and _dist(p, bi) > SNAP:
                        split_pts[i].append((u, p))
                for p in (ai, bi):
                    _, u, d = _nearest_param(p, aj, bj)
                    if d <= SNAP and _dist(p, aj) > SNAP and _dist(p, bj) > SNAP:
                        split_pts[j].append((u, p))
                if li.intersects(lj):
                    inter = li.intersection(lj)
                    if inter.geom_type == "Point":
                        ip = pcbnew.VECTOR2I(round(inter.x), round(inter.y))
                        if _dist(ip, ai) > SNAP and _dist(ip, bi) > SNAP:
                            _, u, _ = _nearest_param(ip, ai, bi)
                            split_pts[i].append((u, ip))
                        if _dist(ip, aj) > SNAP and _dist(ip, bj) > SNAP:
                            _, u, _ = _nearest_param(ip, aj, bj)
                            split_pts[j].append((u, ip))

        current = []
        for i, t in enumerate(ts):
            if not split_pts[i]:
                current.append(t)
                continue
            ai, bi, _ = segs[i]
            ordered = [ai]
            for _, p in sorted(split_pts[i], key=lambda item: item[0]):
                if _dist(p, ordered[-1]) > SNAP and _dist(p, bi) > SNAP:
                    ordered.append(p)
            ordered.append(bi)
            if len(ordered) == 2:
                current.append(t)
                continue
            w, lk = t.GetWidth(), t.IsLocked()
            _detach(board, t)
            splits_made += len(ordered) - 2
            for p0, p1 in itertools.pairwise(ordered):
                nt = pcbnew.PCB_TRACK(board)
                nt.SetStart(p0)
                nt.SetEnd(p1)
                nt.SetWidth(w)
                nt.SetLayer(layer)
                nt.SetLocked(lk)
                board.Add(nt)
                nt.SetNetCode(net)
                current.append(nt)

        seen = {}
        unique = []
        for t in current:
            k0, k1 = _pt_key(t.GetStart()), _pt_key(t.GetEnd())
            if k0 == k1:
                _detach(board, t)
                zero_removed += 1
                continue
            key = (k0, k1) if k0 <= k1 else (k1, k0)
            if key not in seen:
                seen[key] = t
                unique.append(t)
            else:
                keep = seen[key]
                if t.GetWidth() > keep.GetWidth():
                    keep.SetWidth(t.GetWidth())
                if t.IsLocked():
                    keep.SetLocked(True)
                _detach(board, t)
                deduped += 1

        if len(unique) < 3:
            continue
        parent = {}
        for t in unique:
            if t.IsLocked():
                _uf_union(parent, _pt_key(t.GetStart()), _pt_key(t.GetEnd()))
        for t in sorted((t for t in unique if not t.IsLocked()), key=lambda t: (-t.GetWidth(), t.GetLength())):
            if not _uf_union(parent, _pt_key(t.GetStart()), _pt_key(t.GetEnd())):
                _detach(board, t)
                cycles_pruned += 1

    return zero_removed, splits_made, deduped, cycles_pruned


def planar_anomalies(board):
    """Report zero-length tracks, same-layer collinear overlaps, interior crossings
    or planar cycles on any routed net."""
    tracks = [t for t in board.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T and t.GetNetCode() != 0]
    issues = []
    by_nl = {}
    for t in tracks:
        if _pt_key(t.GetStart()) == _pt_key(t.GetEnd()):
            p = t.GetStart()
            issues.append(f"{t.GetNetname()} {t.GetLayerName()}: zero-length track at "
                          f"({pcbnew.ToMM(p.x):.3f}, {-pcbnew.ToMM(p.y):.3f})")
            continue
        by_nl.setdefault((t.GetNetname(), t.GetLayerName()), []).append(t)

    for (net, layer), ts in sorted(by_nl.items()):
        segs = [(t.GetStart(), t.GetEnd(), LineString([(t.GetStart().x, t.GetStart().y),
                                                       (t.GetEnd().x, t.GetEnd().y)])) for t in ts]
        parent = {}
        seen_edges = set()
        for idx, (a, b, line_i) in enumerate(segs):
            ka, kb = _pt_key(a), _pt_key(b)
            ekey = (ka, kb) if ka <= kb else (kb, ka)
            if ekey in seen_edges:
                issues.append(f"{net} {layer}: duplicate overlapping segment at "
                              f"({pcbnew.ToMM(a.x):.3f}, {-pcbnew.ToMM(a.y):.3f})")
                continue
            seen_edges.add(ekey)
            if not _uf_union(parent, ka, kb):
                issues.append(f"{net} {layer}: planar cycle closed at "
                              f"({pcbnew.ToMM(a.x):.3f}, {-pcbnew.ToMM(a.y):.3f})")
            for j in range(idx + 1, len(segs)):
                aj, bj, line_j = segs[j]
                if not line_i.intersects(line_j):
                    continue
                inter = line_i.intersection(line_j)
                if inter.geom_type != "Point":
                    issues.append(f"{net} {layer}: collinear overlap ({inter.geom_type})")
                    continue
                ip = pcbnew.VECTOR2I(round(inter.x), round(inter.y))
                end_i = _dist(ip, a) <= SNAP or _dist(ip, b) <= SNAP
                end_j = _dist(ip, aj) <= SNAP or _dist(ip, bj) <= SNAP
                if not (end_i and end_j):
                    issues.append(f"{net} {layer}: unsplit interior crossing/T at "
                                  f"({pcbnew.ToMM(ip.x):.3f}, {-pcbnew.ToMM(ip.y):.3f})")
    return issues

