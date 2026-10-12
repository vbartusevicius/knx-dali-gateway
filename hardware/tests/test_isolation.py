"""Isolation gate on the stored main-board layout.

Measures the minimum distance between the copper of different isolation
domains on the real geometry (pads, tracks, vias, filled pours). All copper
layers are merged, so inner-layer spacing is held to the surface creepage
requirement. Nets are assigned to domains by the board's own net classes;
pads of the isolators (optocouplers U4/U5, converter U3) belong to the domain
of their net, and only the distance between two pads of the same isolator is
exempt, because the package body is the barrier there.
"""

import json
from pathlib import Path

import pcbnew
import pytest
from shapely.affinity import translate
from shapely.geometry import Polygon
from shapely.ops import unary_union

REPO = Path(__file__).resolve().parents[2]
BOARD = REPO / "hardware/layout/poc/main/main.kicad_pcb"
ISOLATORS = {"U3", "U4", "U5"}
DOMAIN_OF_CLASS = {"SELV-KNX": "KNX", "KNX-Power": "KNX", "DALI": "DALI",
                   "DALI-Power": "DALI", "SELV-AUX": "AUX", "AUX-Power": "AUX"}
REQUIRED = {("DALI", "KNX"): 6.0, ("AUX", "DALI"): 6.0, ("AUX", "KNX"): 2.0}
COPPER = (pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu)


def to_polygons(polyset):
    mm = pcbnew.ToMM
    result = []
    for i in range(polyset.OutlineCount()):
        rings = [polyset.Outline(i)] + [polyset.Hole(i, h) for h in range(polyset.HoleCount(i))]
        coords = [[(mm(r.CPoint(k).x), mm(r.CPoint(k).y)) for k in range(r.PointCount())]
                  for r in rings]
        if len(coords[0]) > 2:
            result.append(Polygon(coords[0], [c for c in coords[1:] if len(c) > 2]))
    return result


def item_polygons(item, layer):
    polyset = pcbnew.SHAPE_POLY_SET()
    item.TransformShapeToPolygon(polyset, layer, 0, pcbnew.FromMM(0.01), pcbnew.ERROR_INSIDE)
    return to_polygons(polyset)


def net_domains():
    project = json.loads(BOARD.with_suffix(".kicad_pro").read_text())
    return {p["pattern"]: DOMAIN_OF_CLASS[p["netclass"]]
            for p in project["net_settings"]["netclass_patterns"]}


def collect_copper(board, domains):
    shared = {"KNX": [], "DALI": [], "AUX": []}
    isolator_pads = []
    for fp in board.Footprints():
        for pad in fp.Pads():
            domain = domains.get(pad.GetNetname())
            if domain is None:
                continue
            shapes = [p for layer in COPPER if pad.IsOnLayer(layer) for p in item_polygons(pad, layer)]
            if fp.GetReference() in ISOLATORS:
                isolator_pads.append((fp.GetReference(), pad.GetNumber(), domain, unary_union(shapes)))
            else:
                shared[domain].extend(shapes)
    for track in board.GetTracks():
        domain = domains.get(track.GetNetname())
        for layer in COPPER:
            if domain and track.IsOnLayer(layer):
                shared[domain].extend(item_polygons(track, layer))
    for i in range(board.GetAreaCount()):
        zone = board.GetArea(i)
        domain = domains.get(zone.GetNetname())
        for layer in COPPER:
            if domain and not zone.GetIsRuleArea() and zone.IsOnLayer(layer):
                shared[domain].extend(to_polygons(zone.GetFilledPolysList(layer)))
    return {d: unary_union(p) for d, p in shared.items()}, isolator_pads


@pytest.fixture(scope="module")
def copper():
    if not BOARD.exists():
        pytest.skip(f"{BOARD} not found")
    board = pcbnew.LoadBoard(str(BOARD))
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    return collect_copper(board, net_domains())


def test_every_domain_has_copper(copper):
    shared, _ = copper
    assert all(not shared[d].is_empty for d in shared)


def min_gap(shared, isolator_pads, pair):
    a, b = pair
    gaps = [shared[a].distance(shared[b])]
    for ref, _, domain, shape in isolator_pads:
        if domain not in pair:
            continue
        other = b if domain == a else a
        gaps.append(shape.distance(shared[other]))
        gaps += [shape.distance(o) for r, _, d, o in isolator_pads if d == other and r != ref]
    return min(gaps)


@pytest.mark.parametrize("pair", sorted(REQUIRED))
def test_domain_spacing(copper, pair):
    gap = min_gap(*copper, pair)
    print(f"\n  {pair[0]} <-> {pair[1]}: {gap:.2f} mm (required {REQUIRED[pair]} mm)")
    assert gap >= REQUIRED[pair]


def test_spacing_check_catches_dali_copper_moved_towards_knx(copper):
    shared, isolator_pads = copper
    shifted = dict(shared, DALI=translate(shared["DALI"], yoff=1.0))
    real = min_gap(shared, isolator_pads, ("DALI", "KNX"))
    assert min_gap(shifted, isolator_pads, ("DALI", "KNX")) == pytest.approx(real - 1.0, abs=0.05)
