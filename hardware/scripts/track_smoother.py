"""DRC-safe octilinear (0° / 45° / 90°) post-route track smoother (`eda` container).

Runs right after Specctra `.ses` import (and before saving/scoring the final
`.kicad_pcb`) to eliminate autorouter V-detours and micro-staircase jogs while
preserving strict clearance against board edges, cutouts, mounting holes,
keep-out rule areas, other-net copper (including isolation barriers), and
same-net self-overlaps:

    smooth_tracks(board, frame=None, isolation=None, net_settings=None,
                  tuned_nets=(), max_tuned_reduction_mm=1.0)
        -> (chains_smoothed, segs_before, segs_after, saved_mm)
"""

from __future__ import annotations

import math

import pcbnew
from shapely.geometry import LineString, Polygon
from shapely.ops import unary_union
from track_joints import _detach

SNAP_TOL_NM = 15_000          # 0.015 mm endpoint clustering tolerance for chain extraction
PIN_TOL_NM = 20_000           # 0.020 mm pad/via pinning tolerance
OCT_TOL_NM = 500              # 0.5 um tolerance for exact 0/45/90 deg alignment
LEN_EPS_NM = 1_000            # 1 um minimum improvement / tie epsilon
MAX_LOOKAHEAD = 8

BASE_CLEARANCE_MM = 0.20
HOLE_CLEARANCE_MM = 0.25
SAFETY_MARGIN_MM = 0.012      # 12 um safety margin above DRC minimums
SELF_SEP_MM = 0.015           # 15 um minimum separation for non-adjacent same-net tracks
POWER_TRACK_MIN_MM = 0.22
PLANE_NETS = {"GND", "GNDA", "GNDD"}


def _dist_mm(a: tuple[float, float], b: tuple[float, float]) -> float:
    return math.hypot(a[0] - b[0], a[1] - b[1])


def _dist_nm(a: tuple[int, int], b: tuple[int, int]) -> float:
    return math.hypot(a[0] - b[0], a[1] - b[1])


def _seg_point_dist_mm(
    ax: float, ay: float, bx: float, by: float, px: float, py: float
) -> float:
    dx, dy = bx - ax, by - ay
    l2 = dx * dx + dy * dy
    if l2 == 0.0:
        return math.hypot(px - ax, py - ay)
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / l2))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def _seg_seg_dist_mm(
    a0: tuple[float, float],
    a1: tuple[float, float],
    b0: tuple[float, float],
    b1: tuple[float, float],
) -> float:
    """Exact Euclidean distance between two 2D segments in mm."""
    # Quick intersection check via orientation / cross products
    ax, ay = a0
    bx, by = a1
    cx, cy = b0
    dx, dy = b1

    def cross(ox: float, oy: float, ux: float, uy: float, vx: float, vy: float) -> float:
        return (ux - ox) * (vy - oy) - (uy - oy) * (vx - ox)

    d1 = cross(cx, cy, dx, dy, ax, ay)
    d2 = cross(cx, cy, dx, dy, bx, by)
    d3 = cross(ax, ay, bx, by, cx, cy)
    d4 = cross(ax, ay, bx, by, dx, dy)
    if ((d1 > 0 and d2 < 0) or (d1 < 0 and d2 > 0)) and ((d3 > 0 and d4 < 0) or (d3 < 0 and d4 > 0)):
        return 0.0
    return min(
        _seg_point_dist_mm(ax, ay, bx, by, cx, cy),
        _seg_point_dist_mm(ax, ay, bx, by, dx, dy),
        _seg_point_dist_mm(cx, cy, dx, dy, ax, ay),
        _seg_point_dist_mm(cx, cy, dx, dy, bx, by),
    )


def _polyset_to_polygons_mm(polyset: pcbnew.SHAPE_POLY_SET) -> list[Polygon]:
    polys: list[Polygon] = []
    for i in range(polyset.OutlineCount()):
        ext = polyset.Outline(i)
        if ext.PointCount() < 3:
            continue
        shell = [(pcbnew.ToMM(ext.CPoint(k).x), pcbnew.ToMM(ext.CPoint(k).y)) for k in range(ext.PointCount())]
        holes = []
        for h in range(polyset.HoleCount(i)):
            hr = polyset.Hole(i, h)
            if hr.PointCount() >= 3:
                holes.append([(pcbnew.ToMM(hr.CPoint(k).x), pcbnew.ToMM(hr.CPoint(k).y))
                              for k in range(hr.PointCount())])
        p = Polygon(shell, holes)
        if not p.is_valid:
            p = p.buffer(0)
        if not p.is_empty:
            polys.append(p)
    return polys


def _net_domains_from_frame(board: pcbnew.BOARD, frame: dict | None) -> dict[str, str]:
    if not frame or "domain_regions" not in frame:
        return {}
    regions = frame["domain_regions"]

    def domain_at(x_mm: float, y_up_mm: float) -> str | None:
        for dom, areas in regions.items():
            for r in (areas if isinstance(areas, list) else [areas]):
                if r["x0"] <= x_mm <= r["x1"] and r["y0"] <= y_up_mm <= r["y1"]:
                    return dom
        return None

    doms: dict[str, str] = {}
    for fp in board.Footprints():
        for pad in fp.Pads():
            net = pad.GetNetname()
            if not net or net in doms:
                continue
            p = pad.GetPosition()
            d = domain_at(pcbnew.ToMM(p.x), -pcbnew.ToMM(p.y))
            if d:
                doms[net] = d
    return doms


def _build_clearance_fn(
    board: pcbnew.BOARD,
    frame: dict | None,
    isolation: list[dict] | None,
    net_settings: dict | None,
):
    net_clr: dict[str, float] = {}
    if net_settings:
        cls_clr = {c["name"]: float(c.get("clearance", BASE_CLEARANCE_MM))
                   for c in net_settings.get("classes", [])}
        for pat in net_settings.get("netclass_patterns", []):
            net_clr[pat["pattern"]] = cls_clr.get(pat["netclass"], BASE_CLEARANCE_MM)

    net_dom = _net_domains_from_frame(board, frame)
    iso_pairs: dict[frozenset[str], float] = {}
    if isolation:
        for rule in isolation:
            iso_pairs[frozenset(rule["between"])] = float(rule["creepage_min"])
    elif frame and len(frame.get("domains", [])) > 1:
        domains = frame["domains"]
        for i in range(len(domains)):
            for j in range(i + 1, len(domains)):
                iso_pairs[frozenset((domains[i], domains[j]))] = 1.0

    def pair_clearance(net_a: str, net_b: str) -> float:
        clr = max(BASE_CLEARANCE_MM, net_clr.get(net_a, BASE_CLEARANCE_MM), net_clr.get(net_b, BASE_CLEARANCE_MM))
        da, db = net_dom.get(net_a), net_dom.get(net_b)
        if da and db and da != db:
            clr = max(clr, iso_pairs.get(frozenset((da, db)), 1.0))
        return clr + SAFETY_MARGIN_MM

    return pair_clearance


def octilinear_candidates(
    a: tuple[int, int], b: tuple[int, int]
) -> list[list[tuple[tuple[int, int], tuple[int, int]]]]:
    """Return 0°/45°/90° shortcut candidates from `a` to `b`:
    either a single straight 0°/45°/90° segment, or both 2-segment 45° + H/V doglegs."""
    ax, ay = a
    bx, by = b
    dx, dy = bx - ax, by - ay
    adx, ady = abs(dx), abs(dy)
    if adx == 0 and ady == 0:
        return []
    if adx <= OCT_TOL_NM or ady <= OCT_TOL_NM or abs(adx - ady) <= OCT_TOL_NM:
        return [[(a, b)]]
    d = min(adx, ady)
    sx = 1 if dx > 0 else -1
    sy = 1 if dy > 0 else -1
    m1 = (ax + sx * d, ay + sy * d)
    m2 = (bx - sx * d, by - sy * d)
    cands = []
    if m1 != a and m1 != b:
        cands.append([(a, m1), (m1, b)])
    if m2 != a and m2 != b and m2 != m1:
        cands.append([(a, m2), (m2, b)])
    return cands


class _LayerChecker:
    """Exact pre-commit clearance and self-overlap verifier for one copper layer."""

    def __init__(
        self,
        board: pcbnew.BOARD,
        layer: int,
        pair_clearance,
        edge_clearance_mm: float,
        board_poly,
        edge_segs: list[tuple[tuple[float, float], tuple[float, float]]],
        unnetted_holes: list[tuple[float, float, float]],
        fine_courtyards: list[Polygon],
        power_nets: set[str],
    ):
        self.layer = layer
        self.pair_clearance = pair_clearance
        self.edge_clearance_mm = edge_clearance_mm
        self.board_poly = board_poly
        self.edge_segs = edge_segs
        self.unnetted_holes = unnetted_holes
        self.fine_courtyards = fine_courtyards
        self.power_nets = power_nets

        # Rule-area keepouts blocking tracks on this layer
        self.keepouts: list[tuple[tuple[float, float, float, float], Polygon]] = []
        for z in board.Zones():
            if z.GetIsRuleArea() and z.IsOnLayer(layer) and z.GetDoNotAllowTracks():
                for poly in _polyset_to_polygons_mm(z.Outline()):
                    self.keepouts.append((poly.bounds, poly))

        # Pads on this layer: circular pads use exact point-to-center distance - radius;
        # non-circular pads use exact polygon distance from TransformShapeToPolygon.
        self.circ_pads: list[tuple[float, float, float, float, int, str]] = []
        self.poly_pads: list[tuple[tuple[float, float, float, float], Polygon, float, float, float, int, str]] = []
        for fp in board.Footprints():
            for pad in fp.Pads():
                if not pad.IsOnLayer(layer):
                    continue
                net_code = pad.GetNetCode()
                net_name = pad.GetNetname()
                pos = pad.GetPosition()
                cx, cy = pcbnew.ToMM(pos.x), pcbnew.ToMM(pos.y)
                drill_r = pcbnew.ToMM(max(pad.GetDrillSizeX(), pad.GetDrillSizeY())) / 2.0
                if pad.GetShape() == pcbnew.PAD_SHAPE_CIRCLE and abs(pad.GetSizeX() - pad.GetSizeY()) <= 1000:
                    rad = pcbnew.ToMM(pad.GetSizeX()) / 2.0
                    self.circ_pads.append((cx, cy, rad, drill_r, net_code, net_name))
                else:
                    pset = pcbnew.SHAPE_POLY_SET()
                    pad.TransformShapeToPolygon(pset, layer, 0, pcbnew.FromMM(0.005), pcbnew.ERROR_INSIDE)
                    polys = _polyset_to_polygons_mm(pset)
                    if polys:
                        u = unary_union(polys) if len(polys) > 1 else polys[0]
                        self.poly_pads.append((u.bounds, u, cx, cy, drill_r, net_code, net_name))

        # Vias on this layer (use GetWidth(F_Cu) as via annular diameter)
        self.vias: list[tuple[float, float, float, float, int, str]] = []
        for item in board.GetTracks():
            if item.Type() == pcbnew.PCB_VIA_T and item.IsOnLayer(layer):
                pos = item.GetPosition()
                cx, cy = pcbnew.ToMM(pos.x), pcbnew.ToMM(pos.y)
                rad = pcbnew.ToMM(item.GetWidth(pcbnew.F_Cu)) / 2.0
                drill_r = pcbnew.ToMM(item.GetDrillValue()) / 2.0
                self.vias.append((cx, cy, rad, drill_r, item.GetNetCode(), item.GetNetname()))

        # Live track segments on this layer: id -> (a_mm, b_mm, w_mm, net_code, net_name)
        self.tracks: dict[int, tuple[tuple[float, float], tuple[float, float], float, int, str]] = {}
        self._next_tid = 1

    def add_track_record(
        self, a_nm: tuple[int, int], b_nm: tuple[int, int], w_nm: int, net_code: int, net_name: str
    ) -> int:
        tid = self._next_tid
        self._next_tid += 1
        self.tracks[tid] = (
            (pcbnew.ToMM(a_nm[0]), pcbnew.ToMM(a_nm[1])),
            (pcbnew.ToMM(b_nm[0]), pcbnew.ToMM(b_nm[1])),
            pcbnew.ToMM(w_nm),
            net_code,
            net_name,
        )
        return tid

    def remove_track_record(self, tid: int) -> None:
        self.tracks.pop(tid, None)

    def effective_width_nm(
        self, net_name: str, base_w_nm: int, segs_nm: list[tuple[tuple[int, int], tuple[int, int]]]
    ) -> int:
        """Keep `min(chain_widths)` across degree-2 chains, while never letting a
        power-net shortcut outside fine-pitch courtyards drop below POWER_TRACK_MIN_MM."""
        if net_name not in self.power_nets or base_w_nm >= pcbnew.FromMM(POWER_TRACK_MIN_MM):
            return base_w_nm
        for a, b in segs_nm:
            am = (pcbnew.ToMM(a[0]), pcbnew.ToMM(a[1]))
            bm = (pcbnew.ToMM(b[0]), pcbnew.ToMM(b[1]))
            in_fine = any(
                c.contains(LineString([am, bm]))
                or c.distance(LineString([am, am])) == 0.0
                or c.distance(LineString([bm, bm])) == 0.0
                for c in self.fine_courtyards
            )
            if not in_fine:
                return pcbnew.FromMM(POWER_TRACK_MIN_MM)
        return base_w_nm

    def segment_is_clear(
        self,
        a_nm: tuple[int, int],
        b_nm: tuple[int, int],
        w_nm: int,
        net_code: int,
        net_name: str,
        ignore_tids: set[int],
        allowed_endpoints_mm: list[tuple[float, float]],
    ) -> bool:
        a = (pcbnew.ToMM(a_nm[0]), pcbnew.ToMM(a_nm[1]))
        b = (pcbnew.ToMM(b_nm[0]), pcbnew.ToMM(b_nm[1]))
        w = pcbnew.ToMM(w_nm)
        half_w = w / 2.0
        line = LineString([a, b])
        sx0, sy0 = min(a[0], b[0]), min(a[1], b[1])
        sx1, sy1 = max(a[0], b[0]), max(a[1], b[1])

        # 1. Board outline containment & edge clearance
        if self.board_poly is not None and not self.board_poly.covers(line):
            return False
        edge_req = half_w + self.edge_clearance_mm + SAFETY_MARGIN_MM
        for e0, e1 in self.edge_segs:
            if (max(e0[0], e1[0]) < sx0 - edge_req or min(e0[0], e1[0]) > sx1 + edge_req
                    or max(e0[1], e1[1]) < sy0 - edge_req or min(e0[1], e1[1]) > sy1 + edge_req):
                continue
            if _seg_seg_dist_mm(a, b, e0, e1) < edge_req:
                return False

        # 2. Unnetted mounting holes / NPTH cutouts
        for hx, hy, hr in self.unnetted_holes:
            req = half_w + hr + HOLE_CLEARANCE_MM + SAFETY_MARGIN_MM
            if hx < sx0 - req or hx > sx1 + req or hy < sy0 - req or hy > sy1 + req:
                continue
            if _seg_point_dist_mm(a[0], a[1], b[0], b[1], hx, hy) < req:
                return False

        # 3. Rule-area keepout zones
        ko_req = half_w + SAFETY_MARGIN_MM
        for (kx0, ky0, kx1, ky1), kpoly in self.keepouts:
            if kx1 < sx0 - ko_req or kx0 > sx1 + ko_req or ky1 < sy0 - ko_req or ky0 > sy1 + ko_req:
                continue
            if line.distance(kpoly) < ko_req:
                return False

        # 4. Circular pads (exact point-to-segment distance minus pad radius, no sqrt(2) bbox inflation!)
        for cx, cy, rad, drill_r, pnet_c, pnet_n in self.circ_pads:
            if pnet_c == net_code:
                # Avoid clipping an unintended same-net pad mid-segment
                if any(_dist_mm((cx, cy), ep) <= rad + 0.03 for ep in allowed_endpoints_mm):
                    continue
                if _seg_point_dist_mm(a[0], a[1], b[0], b[1], cx, cy) < rad + half_w + SELF_SEP_MM:
                    return False
                continue
            clr = self.pair_clearance(net_name, pnet_n)
            req = half_w + rad + clr
            hole_req = (half_w + drill_r + HOLE_CLEARANCE_MM + SAFETY_MARGIN_MM) if drill_r > 0 else 0.0
            lim = max(req, hole_req)
            if cx < sx0 - lim or cx > sx1 + lim or cy < sy0 - lim or cy > sy1 + lim:
                continue
            if _seg_point_dist_mm(a[0], a[1], b[0], b[1], cx, cy) < lim:
                return False

        # 5. Non-circular pads (exact polygon distance)
        for (px0, py0, px1, py1), ppoly, cx, cy, drill_r, pnet_c, pnet_n in self.poly_pads:
            if pnet_c == net_code:
                if any(ppoly.distance(LineString([ep, ep])) <= 0.03 for ep in allowed_endpoints_mm):
                    continue
                if line.distance(ppoly) < half_w + SELF_SEP_MM:
                    return False
                continue
            clr = self.pair_clearance(net_name, pnet_n)
            req = half_w + clr
            if drill_r <= 0 and (px1 < sx0 - req or px0 > sx1 + req or py1 < sy0 - req or py0 > sy1 + req):
                continue
            if line.distance(ppoly) < req:
                return False
            if drill_r > 0:
                hole_req = half_w + drill_r + HOLE_CLEARANCE_MM + SAFETY_MARGIN_MM
                if _seg_point_dist_mm(a[0], a[1], b[0], b[1], cx, cy) < hole_req:
                    return False

        # 6. Vias on this layer
        for vx, vy, vrad, vdrill, vnet_c, vnet_n in self.vias:
            if vnet_c == net_code:
                if any(_dist_mm((vx, vy), ep) <= vrad + 0.03 for ep in allowed_endpoints_mm):
                    continue
                if _seg_point_dist_mm(a[0], a[1], b[0], b[1], vx, vy) < vrad + half_w + SELF_SEP_MM:
                    return False
                continue
            clr = self.pair_clearance(net_name, vnet_n)
            req = max(half_w + vrad + clr, half_w + vdrill + HOLE_CLEARANCE_MM + SAFETY_MARGIN_MM)
            if vx < sx0 - req or vx > sx1 + req or vy < sy0 - req or vy > sy1 + req:
                continue
            if _seg_point_dist_mm(a[0], a[1], b[0], b[1], vx, vy) < req:
                return False

        # 7. All other tracks on this layer (other-net clearance + same-net self-overlap/crossing)
        for tid, (ta, tb, tw, tnet_c, tnet_n) in self.tracks.items():
            if tid in ignore_tids:
                continue
            if tnet_c != net_code:
                clr = self.pair_clearance(net_name, tnet_n)
                req = half_w + tw / 2.0 + clr
                if (max(ta[0], tb[0]) < sx0 - req or min(ta[0], tb[0]) > sx1 + req
                        or max(ta[1], tb[1]) < sy0 - req or min(ta[1], tb[1]) > sy1 + req):
                    continue
                if _seg_seg_dist_mm(a, b, ta, tb) < req:
                    return False
            else:
                # Same-net track outside the replaced sub-chain:
                # If it meets cleanly at an allowed boundary endpoint (p[i] or p[j]), reject only if
                # it folds back collinearly/overlaps (angle < 40 deg) or crosses elsewhere;
                # if non-adjacent, require full separation to prevent self-crossings / self-overlaps.
                shared = None
                for ep in allowed_endpoints_mm:
                    if (_dist_mm(a, ep) <= 0.02 or _dist_mm(b, ep) <= 0.02) and (
                        _dist_mm(ta, ep) <= 0.02 or _dist_mm(tb, ep) <= 0.02
                    ):
                        shared = ep
                        break
                if shared is not None:
                    other_cand = b if _dist_mm(a, shared) <= 0.02 else a
                    other_trk = tb if _dist_mm(ta, shared) <= 0.02 else ta
                    v1x, v1y = other_cand[0] - shared[0], other_cand[1] - shared[1]
                    v2x, v2y = other_trk[0] - shared[0], other_trk[1] - shared[1]
                    n1, n2 = math.hypot(v1x, v1y), math.hypot(v2x, v2y)
                    if n1 > 1e-6 and n2 > 1e-6:
                        cos_a = (v1x * v2x + v1y * v2y) / (n1 * n2)
                        if cos_a > 0.76:  # acute fold-back (< ~40 deg) or collinear overlap
                            return False
                else:
                    req = half_w + tw / 2.0 + SELF_SEP_MM
                    if (max(ta[0], tb[0]) < sx0 - req or min(ta[0], tb[0]) > sx1 + req
                            or max(ta[1], tb[1]) < sy0 - req or min(ta[1], tb[1]) > sy1 + req):
                        continue
                    if _seg_seg_dist_mm(a, b, ta, tb) < req:
                        return False
        return True


def _cluster_points(pts: list[tuple[int, int]], tol_nm: int = SNAP_TOL_NM) -> tuple[list[int], list[tuple[int, int]]]:
    """Cluster 2D integer-nm points within `tol_nm` using Union-Find.
    Returns (point_to_node_id, node_representative_xy)."""
    n = len(pts)
    parent = list(range(n))

    def find(x: int) -> int:
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    def union(a: int, b: int) -> None:
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[rb] = ra

    cell = max(1, tol_nm)
    grid: dict[tuple[int, int], list[int]] = {}
    for idx, (px, py) in enumerate(pts):
        gx, gy = px // cell, py // cell
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for other in grid.get((gx + dx, gy + dy), ()):
                    if _dist_nm(pts[idx], pts[other]) <= tol_nm:
                        union(idx, other)
        grid.setdefault((gx, gy), []).append(idx)

    groups: dict[int, list[int]] = {}
    for idx in range(n):
        groups.setdefault(find(idx), []).append(idx)

    node_map: dict[int, int] = {}
    reps: list[tuple[int, int]] = []
    for root, members in groups.items():
        node_map[root] = len(reps)
        # Keep the first exact point as representative (or average if drifted)
        if all(pts[m] == pts[members[0]] for m in members):
            reps.append(pts[members[0]])
        else:
            rx = round(sum(pts[m][0] for m in members) / len(members))
            ry = round(sum(pts[m][1] for m in members) / len(members))
            reps.append((rx, ry))
    return [node_map[find(i)] for i in range(n)], reps


def _extract_chains_for_net_layer(
    all_tracks: list[pcbnew.PCB_TRACK],
    same_pads: list[pcbnew.PAD],
    same_vias: list[pcbnew.PCB_VIA],
    layer: int,
) -> list[tuple[list[tuple[int, int]], list[pcbnew.PCB_TRACK]]]:
    """Extract maximal degree-2 chains of unlocked tracks between pinned junctions
    (degree != 2, locked tracks, pad centers, or via centers) without splitting
    when track width necks down mid-chain."""
    if not all_tracks:
        return []
    raw_pts: list[tuple[int, int]] = []
    for t in all_tracks:
        s, e = t.GetStart(), t.GetEnd()
        raw_pts.append((s.x, s.y))
        raw_pts.append((e.x, e.y))

    pt_nodes, node_xy = _cluster_points(raw_pts, SNAP_TOL_NM)
    adj: dict[int, list[tuple[int, int]]] = {}  # node -> [(edge_idx, other_node)]
    locked_nodes: set[int] = set()
    edge_nodes: list[tuple[int, int]] = []
    for idx, t in enumerate(all_tracks):
        u, v = pt_nodes[2 * idx], pt_nodes[2 * idx + 1]
        edge_nodes.append((u, v))
        if u == v:
            continue
        adj.setdefault(u, []).append((idx, v))
        adj.setdefault(v, []).append((idx, u))
        if t.IsLocked():
            locked_nodes.add(u)
            locked_nodes.add(v)

    pinned: set[int] = set(locked_nodes)
    for nid, (nx, ny) in enumerate(node_xy):
        if len(adj.get(nid, [])) != 2:
            pinned.add(nid)
            continue
        pt_v = pcbnew.VECTOR2I(nx, ny)
        if any(p.HitTest(pt_v, PIN_TOL_NM) for p in same_pads):
            pinned.add(nid)
            continue
        if any(_dist_nm((nx, ny), (v.GetPosition().x, v.GetPosition().y)) <= v.GetWidth(layer) / 2 + PIN_TOL_NM
               for v in same_vias):
            pinned.add(nid)

    visited_edges: set[int] = set()
    chains: list[tuple[list[tuple[int, int]], list[pcbnew.PCB_TRACK]]] = []

    def _exact_end(edge_idx: int, node_id: int) -> tuple[int, int]:
        u, _v = edge_nodes[edge_idx]
        pt = all_tracks[edge_idx].GetStart() if u == node_id else all_tracks[edge_idx].GetEnd()
        return (pt.x, pt.y)

    def walk(start_node: int, first_edge: int) -> None:
        u, v = edge_nodes[first_edge]
        cur = v if u == start_node else u
        pts = [_exact_end(first_edge, start_node), node_xy[cur]]
        trks = [all_tracks[first_edge]]
        visited_edges.add(first_edge)
        prev_edge = first_edge
        while cur not in pinned:
            nbrs = [pair for pair in adj[cur] if pair[0] != prev_edge]
            if len(nbrs) != 1:
                break
            nxt_edge, nxt_node = nbrs[0]
            if nxt_edge in visited_edges or all_tracks[nxt_edge].IsLocked():
                break
            visited_edges.add(nxt_edge)
            trks.append(all_tracks[nxt_edge])
            pts.append(node_xy[nxt_node])
            prev_edge = nxt_edge
            cur = nxt_node
        pts[-1] = _exact_end(prev_edge, cur)
        if len(trks) >= 2:
            chains.append((pts, trks))

    for nid in sorted(pinned):
        for eidx, _ in adj.get(nid, []):
            if eidx not in visited_edges and not all_tracks[eidx].IsLocked():
                walk(nid, eidx)

    return chains


def smooth_tracks(
    board: pcbnew.BOARD,
    frame: dict | None = None,
    isolation: list[dict] | None = None,
    net_settings: dict | None = None,
    tuned_nets: set[str] | tuple[str, ...] = (),
    max_tuned_reduction_mm: float = 1.0,
) -> tuple[int, int, int, float]:
    """Smooth autorouted tracks across all copper layers of `board` using greedy
    octilinear (0°/45°/90°) shortcutting with strict pre-commit DRC clearance
    and same-net self-overlap verification.

    Returns (chains_smoothed, segs_before, segs_after, saved_length_mm).
    """
    tuned = set(tuned_nets)
    pair_clearance = _build_clearance_fn(board, frame, isolation, net_settings)
    edge_clearance_mm = pcbnew.ToMM(board.GetDesignSettings().m_CopperEdgeClearance)
    power_nets = set(frame.get("power_nets", ())) if frame else set()

    # Plane/pour nets are skipped so pours and ground stubs are never disturbed
    pour_nets = set(PLANE_NETS) | {
        z.GetNetname() for z in board.Zones() if not z.GetIsRuleArea() and z.GetNetname()
    }

    # Board outline polygon + all Edge.Cuts segments (board + footprint cutouts)
    b_pset = pcbnew.SHAPE_POLY_SET()
    board.GetBoardPolygonOutlines(b_pset, False)
    b_polys = _polyset_to_polygons_mm(b_pset)
    board_poly = unary_union(b_polys) if b_polys else None

    edge_segs: list[tuple[tuple[float, float], tuple[float, float]]] = []
    for item in list(board.GetDrawings()):
        if (
            item.GetLayer() == pcbnew.Edge_Cuts
            and isinstance(item, pcbnew.PCB_SHAPE)
            and item.GetShape() == pcbnew.SHAPE_T_SEGMENT
        ):
            s, e = item.GetStart(), item.GetEnd()
            edge_segs.append(((pcbnew.ToMM(s.x), pcbnew.ToMM(s.y)), (pcbnew.ToMM(e.x), pcbnew.ToMM(e.y))))
    for fp in board.Footprints():
        for item in fp.GraphicalItems():
            if (
                item.GetLayer() == pcbnew.Edge_Cuts
                and isinstance(item, pcbnew.PCB_SHAPE)
                and item.GetShape() == pcbnew.SHAPE_T_SEGMENT
            ):
                s, e = item.GetStart(), item.GetEnd()
                edge_segs.append(((pcbnew.ToMM(s.x), pcbnew.ToMM(s.y)), (pcbnew.ToMM(e.x), pcbnew.ToMM(e.y))))

    # Unnetted mounting holes / NPTH pads
    unnetted_holes: list[tuple[float, float, float]] = []
    for fp in board.Footprints():
        for pad in fp.Pads():
            if pad.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH or (pad.GetNetCode() == 0 and pad.GetDrillSizeX() > 0):
                pos = pad.GetPosition()
                rad = pcbnew.ToMM(max(pad.GetSizeX(), pad.GetSizeY(), pad.GetDrillSizeX(), pad.GetDrillSizeY())) / 2.0
                unnetted_holes.append((pcbnew.ToMM(pos.x), pcbnew.ToMM(pos.y), rad))

    # Fine-pitch courtyards (for power-net neckdown rule preservation)
    fine_courtyards: list[Polygon] = []
    if frame:
        for ref in frame.get("fine_pitch_power_pads", ()):
            fp = board.FindFootprintByReference(ref)
            if fp is None:
                continue
            fp.BuildCourtyardCaches()
            cy = fp.GetCourtyard(pcbnew.B_CrtYd if fp.IsFlipped() else pcbnew.F_CrtYd)
            fine_courtyards.extend(_polyset_to_polygons_mm(cy))

    # Group all tracks by layer
    all_tracks = [t for t in board.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T and t.GetNetCode() != 0]
    all_vias = [v for v in board.GetTracks() if v.Type() == pcbnew.PCB_VIA_T]
    all_pads = list(board.GetPads())

    layers = sorted({t.GetLayer() for t in all_tracks})
    chains_smoothed = 0
    segs_before = 0
    segs_after = 0
    saved_mm = 0.0

    for layer in layers:
        layer_tracks = [t for t in all_tracks if t.GetLayer() == layer]
        checker = _LayerChecker(
            board, layer, pair_clearance, edge_clearance_mm,
            board_poly, edge_segs, unnetted_holes, fine_courtyards, power_nets,
        )
        track_to_tid: dict[str, int] = {}
        by_net: dict[int, list[pcbnew.PCB_TRACK]] = {}
        for t in layer_tracks:
            s, e = t.GetStart(), t.GetEnd()
            tid = checker.add_track_record((s.x, s.y), (e.x, e.y), t.GetWidth(), t.GetNetCode(), t.GetNetname())
            track_to_tid[t.m_Uuid.AsString()] = tid
            by_net.setdefault(t.GetNetCode(), []).append(t)

        for net_code in sorted(by_net):
            net_tracks = by_net[net_code]
            net_name = net_tracks[0].GetNetname()
            if net_name in pour_nets:
                continue
            same_pads = [p for p in all_pads if p.GetNetCode() == net_code and p.IsOnLayer(layer)]
            same_vias = [v for v in all_vias if v.GetNetCode() == net_code and v.IsOnLayer(layer)]
            chains = _extract_chains_for_net_layer(net_tracks, same_pads, same_vias, layer)

            for pts, chain_trks in chains:
                chain_min_w = min(t.GetWidth() for t in chain_trks)
                cur_pts = list(pts)
                cur_tids = [track_to_tid[t.m_Uuid.AsString()] for t in chain_trks]
                orig_seg_count = len(chain_trks)
                orig_len_nm = sum(_dist_nm(cur_pts[k], cur_pts[k + 1]) for k in range(len(cur_pts) - 1))

                modified = False
                changed = True
                while changed:
                    changed = False
                    i = 0
                    while i < len(cur_pts) - 2:
                        best = None
                        for j in range(min(len(cur_pts) - 1, i + MAX_LOOKAHEAD), i + 1, -1):
                            sub_len = sum(_dist_nm(cur_pts[k], cur_pts[k + 1]) for k in range(i, j))
                            sub_tids = set(cur_tids[i:j])
                            ep_i = (pcbnew.ToMM(cur_pts[i][0]), pcbnew.ToMM(cur_pts[i][1]))
                            ep_j = (pcbnew.ToMM(cur_pts[j][0]), pcbnew.ToMM(cur_pts[j][1]))
                            for cand in octilinear_candidates(cur_pts[i], cur_pts[j]):
                                cand_len = sum(_dist_nm(a, b) for a, b in cand)
                                if cand_len > sub_len + LEN_EPS_NM:
                                    continue
                                if not (cand_len < sub_len - LEN_EPS_NM or len(cand) < j - i):
                                    continue
                                if net_name in tuned and (sub_len - cand_len) > pcbnew.FromMM(max_tuned_reduction_mm):
                                    continue
                                cand_w = checker.effective_width_nm(net_name, chain_min_w, cand)
                                # Verify every candidate segment clears all obstacles and non-adjacent same-net copper
                                ok = True
                                for seg_idx, (sa, sb) in enumerate(cand):
                                    allowed = []
                                    if seg_idx == 0:
                                        allowed.append(ep_i)
                                    if seg_idx == len(cand) - 1:
                                        allowed.append(ep_j)
                                    if not checker.segment_is_clear(
                                        sa, sb, cand_w, net_code, net_name, sub_tids, allowed
                                    ):
                                        ok = False
                                        break
                                if not ok:
                                    continue
                                key = (round(cand_len), len(cand))
                                if best is None or key < best[0]:
                                    best = (key, j, cand, cand_w, sub_tids)
                        if best is not None:
                            _, j, cand, cand_w, sub_tids = best
                            for old_tid in sub_tids:
                                checker.remove_track_record(old_tid)
                            new_tids = [
                                checker.add_track_record(sa, sb, cand_w, net_code, net_name)
                                for sa, sb in cand
                            ]
                            mid_pts = [cand[0][1]] if len(cand) == 2 else []
                            cur_pts = cur_pts[:i + 1] + mid_pts + cur_pts[j:]
                            cur_tids = cur_tids[:i] + new_tids + cur_tids[j:]
                            modified = True
                            changed = True
                            i += len(cand)
                        else:
                            i += 1

                if not modified:
                    continue

                # Commit smoothed chain to pcbnew board (preserving KiCad 10 SWIG safety via _detach)
                new_len_nm = sum(_dist_nm(cur_pts[k], cur_pts[k + 1]) for k in range(len(cur_pts) - 1))
                for old_t in chain_trks:
                    _detach(board, old_t)
                for k in range(len(cur_pts) - 1):
                    sa, sb = cur_pts[k], cur_pts[k + 1]
                    seg_w = checker.effective_width_nm(net_name, chain_min_w, [(sa, sb)])
                    nt = pcbnew.PCB_TRACK(board)
                    nt.SetStart(pcbnew.VECTOR2I(sa[0], sa[1]))
                    nt.SetEnd(pcbnew.VECTOR2I(sb[0], sb[1]))
                    nt.SetWidth(seg_w)
                    nt.SetLayer(layer)
                    board.Add(nt)
                    nt.SetNetCode(net_code)

                chains_smoothed += 1
                segs_before += orig_seg_count
                segs_after += len(cur_pts) - 1
                saved_mm += pcbnew.ToMM(round(orig_len_nm - new_len_nm))

    return chains_smoothed, segs_before, segs_after, round(saved_mm, 3)
