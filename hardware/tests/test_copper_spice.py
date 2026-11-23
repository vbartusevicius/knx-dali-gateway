"""Pure Copper-Extracted End-to-End `ngspice` Simulation Gate (`libngspice` via `ctypes`).

Operates with ZERO imports from `hardware/circuits/` or any schematic generator:
1. Loads the final routed `.kicad_pcb` boards (`pcbnew`), asserts
   `GetConnectivity().GetUnconnectedCount(False) == 0`, and reconstructs every
   electrical node purely via geometric Union-Find (`STRtree`) over physical
   pads, tracks, vias and filled copper zones—including distributed copper trace
   `R`/`C` parasitics.
2. Reads all component values and MPNs strictly from the exported production BOM
   CSVs (`hardware/releases/<variant>/<board>/<board>_bom.csv`).
3. Stitches mated multi-PCB assemblies: board-to-board headers (`v2` `main` <->
   `interface`) strictly by 3D `(X, Y)` mating pad coordinates (`<= 0.05 mm`),
   and FFC/cable interconnects (`main` <-> `ui`) by physical pin rank.
4. Drives `libngspice.so.0` via `ctypes` across `.op`, `.dc`, `.ac`, `.noise`
   (`setplot noise1` / `noise2`) and `.tran` to verify input reverse-polarity
   and surge clamping, SCT2400 buck UVLO & regulation, 5 V priority mux
   handover & LM66100 USB reverse-blocking, power/ADC filter stability & noise,
   DALI internal supply current limiting & CTRL shut-off, DALI TX 250 mA low
   level + 1.3 ms bootstrap hold + fault clamp, DALI RX 6.5 V / 9.5 V zener
   thresholds, UI button ADC ladder states and 5 V WS2812B level shifting.
"""

from __future__ import annotations

import ast
import csv
import ctypes
import itertools
import math
import os
import re
from dataclasses import dataclass, field
from pathlib import Path

import pcbnew
import pytest
from shapely.geometry import Point, Polygon
from shapely.ops import unary_union
from shapely.strtree import STRtree

REPO = Path(__file__).resolve().parents[2]
VARIANT = os.environ.get("HARDWARE_VARIANT", "v3")
LAYOUT_ROOT = REPO / "hardware/layout" / VARIANT
RELEASE_ROOT = REPO / "hardware/releases" / VARIANT
BOARDS = tuple(sorted(p.name for p in LAYOUT_ROOT.iterdir() if (p / f"{p.name}.kicad_pcb").exists()))


# ---------------------------------------------------------------------------
# 1. `libngspice.so.0` ctypes wrapper (singleton per process)
# ---------------------------------------------------------------------------

class NgComplex(ctypes.Structure):
    _fields_ = [("cx_real", ctypes.c_double), ("cx_imag", ctypes.c_double)]


class VectorInfo(ctypes.Structure):
    _fields_ = [
        ("v_name", ctypes.c_char_p),
        ("v_type", ctypes.c_int),
        ("v_flags", ctypes.c_short),
        ("v_realdata", ctypes.POINTER(ctypes.c_double)),
        ("v_compdata", ctypes.POINTER(NgComplex)),
        ("v_length", ctypes.c_int),
    ]


SEND_CHAR = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_void_p)
SEND_STAT = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_void_p)
CONTROLLED_EXIT = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_int, ctypes.c_bool, ctypes.c_bool, ctypes.c_int, ctypes.c_void_p)


class NgSpiceRunner:
    def __init__(self) -> None:
        self.logs: list[str] = []
        self.errors: list[str] = []

        @SEND_CHAR
        def _on_char(msg, _ident, _user):
            if msg:
                text = msg.decode("utf-8", errors="replace")
                self.logs.append(text)
                if text.startswith("stderr Error:") and "no such vector" not in text.lower():
                    self.errors.append(text)
            return 0

        @SEND_STAT
        def _on_stat(_msg, _ident, _user):
            return 0

        @CONTROLLED_EXIT
        def _on_exit(status, _imm, _quit, _ident, _user):
            return status

        self._cb_char = _on_char
        self._cb_stat = _on_stat
        self._cb_exit = _on_exit
        self.lib = ctypes.CDLL("libngspice.so.0")
        self.lib.ngSpice_Init.argtypes = [
            SEND_CHAR, SEND_STAT, CONTROLLED_EXIT,
            ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
        ]
        self.lib.ngSpice_Init.restype = ctypes.c_int
        self.lib.ngSpice_Circ.argtypes = [ctypes.POINTER(ctypes.c_char_p)]
        self.lib.ngSpice_Circ.restype = ctypes.c_int
        self.lib.ngSpice_Command.argtypes = [ctypes.c_char_p]
        self.lib.ngSpice_Command.restype = ctypes.c_int
        self.lib.ngGet_Vec_Info.argtypes = [ctypes.c_char_p]
        self.lib.ngGet_Vec_Info.restype = ctypes.POINTER(VectorInfo)
        self.lib.ngSpice_Init(self._cb_char, self._cb_stat, self._cb_exit, None, None, None, None)

    def run_deck(self, deck: str) -> None:
        self.cmd("destroy all")
        self.cmd("remcirc")
        self.logs.clear()
        self.errors.clear()
        encoded = [line.encode("utf-8") for line in deck.splitlines() if line.strip()]
        arr = (ctypes.c_char_p * (len(encoded) + 1))(*encoded, None)
        rc = self.lib.ngSpice_Circ(arr)
        assert rc == 0, f"ngSpice_Circ failed (rc={rc}): {self.logs[-15:]}"
        rc = self.lib.ngSpice_Command(b"run")
        aborted = [l for l in self.logs if "simulation(s) aborted" in l.lower() or "singular matrix" in l.lower()]
        assert rc == 0 and not aborted, "ngspice run failed:\n" + "\n".join(self.logs[-25:])

    def cmd(self, command: str) -> None:
        self.lib.ngSpice_Command(command.encode("utf-8"))

    def vec(self, name: str) -> list[float]:
        ptr = self.lib.ngGet_Vec_Info(name.encode("utf-8"))
        assert ptr, f"ngspice vector {name!r} not found; recent logs: {self.logs[-10:]}"
        vi = ptr.contents
        if vi.v_realdata:
            return [float(vi.v_realdata[i]) for i in range(vi.v_length)]
        if vi.v_compdata:
            return [math.hypot(vi.v_compdata[i].cx_real, vi.v_compdata[i].cx_imag) for i in range(vi.v_length)]
        return []

    def cvec(self, name: str) -> list[complex]:
        ptr = self.lib.ngGet_Vec_Info(name.encode("utf-8"))
        assert ptr, f"ngspice complex vector {name!r} not found; recent logs: {self.logs[-10:]}"
        vi = ptr.contents
        if vi.v_compdata:
            return [complex(vi.v_compdata[i].cx_real, vi.v_compdata[i].cx_imag) for i in range(vi.v_length)]
        return [complex(vi.v_realdata[i], 0.0) for i in range(vi.v_length)]


_NGSPICE: NgSpiceRunner | None = None


def ngspice() -> NgSpiceRunner:
    global _NGSPICE
    if _NGSPICE is None:
        _NGSPICE = NgSpiceRunner()
    return _NGSPICE


# ---------------------------------------------------------------------------
# 2. Pure copper Union-Find + BOM parser + 3D B2B stitching (zero generator imports)
# ---------------------------------------------------------------------------

class UnionFind:
    def __init__(self) -> None:
        self.parent: dict[int, int] = {}

    def add(self, x: int) -> None:
        self.parent.setdefault(x, x)

    def find(self, x: int) -> int:
        while self.parent[x] != x:
            self.parent[x] = self.parent[self.parent[x]]
            x = self.parent[x]
        return x

    def union(self, a: int, b: int) -> None:
        ra, rb = self.find(a), self.find(b)
        if ra != rb:
            if ra < rb:
                self.parent[rb] = ra
            else:
                self.parent[ra] = rb


def poly_set_to_shapely(pset: pcbnew.SHAPE_POLY_SET) -> list[Polygon]:
    mm = pcbnew.ToMM
    out: list[Polygon] = []
    for i in range(pset.OutlineCount()):
        ext = pset.Outline(i)
        if ext.PointCount() < 3:
            continue
        shell = [(mm(ext.CPoint(k).x), -mm(ext.CPoint(k).y)) for k in range(ext.PointCount())]
        holes = []
        for h in range(pset.HoleCount(i)):
            ring = pset.Hole(i, h)
            if ring.PointCount() >= 3:
                holes.append([(mm(ring.CPoint(k).x), -mm(ring.CPoint(k).y)) for k in range(ring.PointCount())])
        p = Polygon(shell, holes)
        if not p.is_valid:
            p = p.buffer(0)
        if not p.is_empty:
            if p.geom_type == "Polygon":
                out.append(p)
            else:
                out.extend(g for g in p.geoms if g.geom_type == "Polygon")
    return out


def item_copper_poly(item, layer: int):
    pset = pcbnew.SHAPE_POLY_SET()
    item.TransformShapeToPolygon(pset, layer, 0, pcbnew.FromMM(0.015), pcbnew.ERROR_INSIDE)
    polys = poly_set_to_shapely(pset)
    return unary_union(polys).buffer(0.002) if polys else None


def parse_bom(board: str) -> dict[str, dict[str, str]]:
    by_ref: dict[str, dict[str, str]] = {}
    with open(RELEASE_ROOT / board / f"{board}_bom.csv", newline="") as f:
        for row in csv.DictReader(f):
            for ref in (r.strip() for r in row["Designator"].split(",") if r.strip()):
                by_ref[ref] = {
                    "value": row["Value"].strip(),
                    "footprint": row["Footprint"].strip(),
                    "mpn": row["Manufacturer Part Number"].strip(),
                    "lcsc": row["LCSC Part #"].strip(),
                    "mount": row["Mount"].strip(),
                }
    return by_ref


SI_MULT = {"p": 1e-12, "n": 1e-9, "u": 1e-6, "µ": 1e-6, "m": 1e-3, "k": 1e3, "M": 1e6, "G": 1e9, "": 1.0}


def parse_si_value(val_str: str) -> float:
    tok = val_str.split()[0].strip()
    m = re.fullmatch(r"(\d+(?:\.\d+)?)([pnuµmkMGR]?)(\d*)\s*[ΩFH]?", tok)
    assert m, f"cannot parse BOM passive value {val_str!r}"
    whole, pfx, frac = m.groups()
    unit = "" if pfx == "R" else pfx
    return float(f"{whole}.{frac}" if frac else whole) * SI_MULT[unit]


@dataclass
class BoardExtraction:
    name: str
    bom: dict[str, dict[str, str]]
    pad_node: dict[tuple[str, str], int]
    pad_xy: dict[tuple[str, str], tuple[float, float]]
    node_squares: dict[int, float] = field(default_factory=dict)
    node_length_mm: dict[int, float] = field(default_factory=dict)
    kicad_pad_nets: dict[tuple[str, str], str] = field(default_factory=dict)


def extract_board_copper(board_name: str) -> BoardExtraction:
    pcb = pcbnew.LoadBoard(str(LAYOUT_ROOT / board_name / f"{board_name}.kicad_pcb"))
    pcbnew.ZONE_FILLER(pcb).Fill(pcb.Zones())
    pcb.BuildConnectivity()
    unconn = pcb.GetConnectivity().GetUnconnectedCount(False)
    assert unconn == 0, f"{board_name}: {unconn} unconnected items in KiCad connectivity"

    bom = parse_bom(board_name)
    cu_layers = list(pcb.GetEnabledLayers().CuStack())
    outer_layers = {cu_layers[0], cu_layers[-1]}

    uf = UnionFind()
    next_id = 0

    def alloc_id() -> int:
        nonlocal next_id
        cid = next_id
        next_id += 1
        uf.add(cid)
        return cid

    layer_shapes: dict[int, list[tuple[int, object]]] = {l: [] for l in cu_layers}
    pad_ids: dict[tuple[str, str], list[int]] = {}
    pad_pts: dict[tuple[str, str], list[tuple[float, float]]] = {}
    kicad_pad_nets: dict[tuple[str, str], str] = {}
    track_metrics: list[tuple[int, float, float]] = []  # (item_id, squares, length_mm)

    for fp in pcb.Footprints():
        ref = fp.GetReference()
        for pad in fp.Pads():
            num = pad.GetNumber()
            if not num:
                continue
            on_cu = [l for l in cu_layers if pad.IsOnLayer(l)]
            if not on_cu:
                continue
            pid = alloc_id()
            pad_ids.setdefault((ref, num), []).append(pid)
            pad_pts.setdefault((ref, num), []).append(
                (pcbnew.ToMM(pad.GetPosition().x), -pcbnew.ToMM(pad.GetPosition().y))
            )
            if pad.GetNetname():
                kicad_pad_nets[(ref, num)] = pad.GetNetname()
            for l in on_cu:
                shp = item_copper_poly(pad, l)
                if shp is not None and not shp.is_empty:
                    layer_shapes[l].append((pid, shp))

    for trk in pcb.GetTracks():
        t_type = trk.Type()
        if t_type == pcbnew.PCB_VIA_T:
            vid = alloc_id()
            vx, vy = pcbnew.ToMM(trk.GetPosition().x), -pcbnew.ToMM(trk.GetPosition().y)
            for l in cu_layers:
                if trk.IsOnLayer(l):
                    r = pcbnew.ToMM(trk.GetWidth(l)) / 2.0 + 0.002
                    layer_shapes[l].append((vid, Point(vx, vy).buffer(r, quad_segs=8)))
        elif t_type in (pcbnew.PCB_TRACE_T, pcbnew.PCB_ARC_T):
            l = trk.GetLayer()
            if l not in layer_shapes:
                continue
            tid = alloc_id()
            shp = item_copper_poly(trk, l)
            if shp is not None and not shp.is_empty:
                layer_shapes[l].append((tid, shp))
            length_mm = pcbnew.ToMM(trk.GetLength())
            width_mm = max(0.05, pcbnew.ToMM(trk.GetWidth()))
            sheet_factor = 1.0 if l in outer_layers else 2.0  # 1 oz outer (0.5 mOhm/sq), 0.5 oz inner (1.0 mOhm/sq)
            track_metrics.append((tid, sheet_factor * (length_mm / width_mm), length_mm))

    for i in range(pcb.GetAreaCount()):
        zone = pcb.GetArea(i)
        if zone.GetIsRuleArea():
            continue
        for l in cu_layers:
            if not zone.IsOnLayer(l):
                continue
            for island in poly_set_to_shapely(zone.GetFilledPolysList(l)):
                zid = alloc_id()
                layer_shapes[l].append((zid, island.buffer(0.002)))

    # Spatial Union-Find per copper layer
    for l, items in layer_shapes.items():
        if len(items) < 2:
            continue
        ids = [it[0] for it in items]
        geoms = [it[1] for it in items]
        tree = STRtree(geoms)
        pairs = tree.query(geoms, predicate="intersects")
        for idx_a, idx_b in zip(pairs[0], pairs[1]):
            if idx_a < idx_b:
                uf.union(ids[int(idx_a)], ids[int(idx_b)])

    pad_node: dict[tuple[str, str], int] = {}
    pad_xy: dict[tuple[str, str], tuple[float, float]] = {}
    for key, pids in pad_ids.items():
        roots = {uf.find(pid) for pid in pids}
        # Connected duplicate-number pads on a routed board must be united by real copper
        if key in kicad_pad_nets:
            assert len(roots) == 1, f"{board_name}:{key} duplicate pads not united by copper: {roots}"
        root = min(roots)
        for r in roots:
            uf.union(root, r)
        pad_node[key] = uf.find(root)
        pts = pad_pts[key]
        pad_xy[key] = (sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts))

    node_squares: dict[int, float] = {}
    node_length_mm: dict[int, float] = {}
    for tid, sq, l_mm in track_metrics:
        r = uf.find(tid)
        node_squares[r] = node_squares.get(r, 0.0) + sq
        node_length_mm[r] = node_length_mm.get(r, 0.0) + l_mm

    return BoardExtraction(
        name=board_name,
        bom=bom,
        pad_node={k: uf.find(v) for k, v in pad_node.items()},
        pad_xy=pad_xy,
        node_squares=node_squares,
        node_length_mm=node_length_mm,
        kicad_pad_nets=kicad_pad_nets,
    )


@dataclass
class StitchedSystem:
    boards: dict[str, BoardExtraction]
    global_pad_node: dict[tuple[str, str, str], str]
    trace_r_ohm: dict[str, float]
    trace_c_f: dict[str, float]
    b2b_matched_pairs: list[tuple[tuple[str, str, str], tuple[str, str, str], float]]
    cable_links: list[tuple[str, str]]
    gnd_node: str
    gnda_node: str
    gndd_node: str


def build_stitched_system() -> StitchedSystem:
    extracted = {b: extract_board_copper(b) for b in BOARDS}

    # Global Union-Find over (board, cluster_id) for rigid board-to-board headers
    g_uf = UnionFind()
    key_to_gid: dict[tuple[str, int], int] = {}

    def gid_of(board: str, cid: int) -> int:
        k = (board, cid)
        if k not in key_to_gid:
            nid = len(key_to_gid)
            key_to_gid[k] = nid
            g_uf.add(nid)
        return key_to_gid[k]

    for b, ext in extracted.items():
        for cid in set(ext.pad_node.values()):
            gid_of(b, cid)

    # 1. Rigid 3D (X, Y) mating pad stitching for stacked PCBs (`v2`: `main` <-> `interface`)
    b2b_matched: list[tuple[tuple[str, str, str], tuple[str, str, str], float]] = []
    if "interface" in extracted:
        main_ext = extracted["main"]
        intf_ext = extracted["interface"]
        sockets = [r for r, info in main_ext.bom.items() if info["footprint"].startswith("PinSocket_1x")]
        headers = [r for r, info in intf_ext.bom.items() if info["footprint"].startswith("PinHeader_1x")]
        assert sockets and len(sockets) == len(headers)

        def pads_of_ref(ext: BoardExtraction, ref: str) -> dict[str, tuple[float, float]]:
            return {num: xy for (r, num), xy in ext.pad_xy.items() if r == ref}

        # Derive unique rigid 2D board translation (dx, dy) from the 4-pin connector centroid,
        # then require EVERY pad across ALL board-to-board connectors to match strictly by (X, Y) <= 0.05 mm!
        s0 = max(sockets, key=lambda r: len(pads_of_ref(main_ext, r)))
        h0 = max(headers, key=lambda r: len(pads_of_ref(intf_ext, r)))
        sp0, hp0 = pads_of_ref(main_ext, s0), pads_of_ref(intf_ext, h0)
        dx = sum(v[0] for v in sp0.values()) / len(sp0) - sum(v[0] for v in hp0.values()) / len(hp0)
        dy = sum(v[1] for v in sp0.values()) / len(sp0) - sum(v[1] for v in hp0.values()) / len(hp0)

        all_spads = [(r, num, xy) for r in sockets for num, xy in pads_of_ref(main_ext, r).items()]
        all_hpads = [(r, num, (xy[0] + dx, xy[1] + dy)) for r in headers for num, xy in pads_of_ref(intf_ext, r).items()]
        assert len(all_spads) == len(all_hpads) >= 7

        used_h: set[int] = set()
        for sr, snum, (sx, sy) in all_spads:
            dists = [(idx, math.hypot(sx - hx, sy - hy)) for idx, (_, _, (hx, hy)) in enumerate(all_hpads)]
            best_idx, best_d = min(dists, key=lambda item: item[1])
            assert best_d <= 0.05 and best_idx not in used_h, (
                f"3D B2B pad mismatch for main:{sr}.{snum} at ({sx:.2f}, {sy:.2f}): min dist {best_d:.3f} mm"
            )
            used_h.add(best_idx)
            hr, hnum, _ = all_hpads[best_idx]
            g_uf.union(gid_of("main", main_ext.pad_node[(sr, snum)]),
                       gid_of("interface", intf_ext.pad_node[(hr, hnum)]))
            b2b_matched.append((("main", sr, snum), ("interface", hr, hnum), best_d))

    # Map global clusters to SPICE node names (`0` = SELV-KNX GND on main:J1 pin 2)
    gnd_root = g_uf.find(gid_of("main", extracted["main"].pad_node[("J1", "2")]))
    gnda_root = g_uf.find(gid_of("main", extracted["main"].pad_node[("J2", "2")]))
    gndd_root = g_uf.find(gid_of("main", extracted["main"].pad_node[("JP1", "3")]))

    def spice_name(root_gid: int) -> str:
        if root_gid == gnd_root:
            return "0"
        if root_gid == gnda_root:
            return "n_gnda"
        if root_gid == gndd_root:
            return "n_gndd"
        return f"n_{root_gid}"

    global_pad_node: dict[tuple[str, str, str], str] = {}
    for b, ext in extracted.items():
        for (ref, num), cid in ext.pad_node.items():
            global_pad_node[(b, ref, num)] = spice_name(g_uf.find(gid_of(b, cid)))

    trace_r_ohm: dict[str, float] = {}
    trace_c_f: dict[str, float] = {}
    for b, ext in extracted.items():
        for cid, sq in ext.node_squares.items():
            sn = spice_name(g_uf.find(gid_of(b, cid)))
            trace_r_ohm[sn] = trace_r_ohm.get(sn, 0.0) + 0.0005 * sq
        for cid, l_mm in ext.node_length_mm.items():
            sn = spice_name(g_uf.find(gid_of(b, cid)))
            trace_c_f[sn] = trace_c_f.get(sn, 0.0) + 0.08e-12 * l_mm

    # 2. Cable / FFC link between `main:J4` and `ui:J1` (50 mOhm + 3 pF per conductor)
    cable_links: list[tuple[str, str]] = []
    main_j4_pins = sorted(
        int(num) for (r, num) in extracted["main"].pad_node if r == "J4" and num.isdigit() and int(num) <= 10 and
        not (extracted["main"].bom["J4"]["mpn"] == "KH-FPC0.5-H1.2SMT-8P-QCHF" and int(num) > 8)
    )
    for pin_i in main_j4_pins:
        pstr = str(pin_i)
        na = global_pad_node[("main", "J4", pstr)]
        nb = global_pad_node[("ui", "J1", pstr)]
        cable_links.append((na, nb))

    return StitchedSystem(
        boards=extracted,
        global_pad_node=global_pad_node,
        trace_r_ohm=trace_r_ohm,
        trace_c_f=trace_c_f,
        b2b_matched_pairs=b2b_matched,
        cable_links=cable_links,
        gnd_node="0",
        gnda_node="n_gnda",
        gndd_node="n_gndd",
    )


# ---------------------------------------------------------------------------
# 3. Smooth C^inf SPICE library + copper-extracted netlist emitter
# ---------------------------------------------------------------------------

SPICE_MODELS_AND_SUBCKTS = """\
.options rshunt=1e11 reltol=1e-3 abstol=1e-9 vntol=1e-6 itl1=400 itl4=100
.func stanh(x) {tanh(min(max(x, -10.0), 10.0))}
.func spos(x, w) {0.5 * ((x) + sqrt((x)*(x) + (w)*(w)))}

* Discrete semiconductor models
.model D_SS16 D(IS=50n N=1.04 RS=0.045 BV=60 IBV=100u CJO=110p)
.model D_B5819W D(IS=80n N=1.04 RS=0.055 BV=40 IBV=50u CJO=85p)
.model D_DF06S D(IS=2n N=1.75 RS=0.08 BV=600 CJO=25p)
.model D_Z5V6 D(IS=1p N=1.2 RS=1.5 BV=5.55 IBV=5m CJO=40p)
.model D_Z12V D(IS=1p N=1.2 RS=2.0 BV=11.9 IBV=5m CJO=25p)
.model D_TVS24 D(IS=10p N=1.2 RS=0.15 BV=26.7 IBV=1m CJO=300p)
.model D_TVS33 D(IS=10p N=1.2 RS=0.20 BV=36.7 IBV=1m CJO=250p)
.model D_TVS40_UNI D(IS=10p N=1.2 RS=0.15 BV=44.4 IBV=1m CJO=200p)
.model D_OPTO_LED D(IS=1e-16 N=1.65 RS=2.0 CJO=15p)
.model D_BODY D(IS=1e-12 N=1.1 RS=0.05 CJO=40p)

.model Q_BC847 NPN(IS=1.8e-14 BF=280 VAF=80 RB=30 RC=1.0 RE=0.4 CJE=11p CJC=4p TF=0.5n)
.model Q_BC857 PNP(IS=1.8e-14 BF=420 VAF=60 RB=30 RC=1.0 RE=0.4 CJE=12p CJC=5p TF=0.6n)
.model Q_MJD45H11 PNP(IS=2e-12 BF=90 VAF=60 RB=0.8 RC=0.05 RE=0.03 CJE=350p CJC=120p TF=8n)

.model M_IRLML0060 NMOS(LEVEL=1 VTO=1.75 KP=18 RD=0.045 RS=0.045 CBD=60p)
.model M_2N7002 NMOS(LEVEL=1 VTO=1.60 KP=0.6 RD=1.2 RS=0.5 CBD=10p)
.model M_AO3401A PMOS(LEVEL=1 VTO=-1.00 KP=22 RD=0.028 RS=0.028 CBD=90p)

* Bidirectional TVS SMAJ40CA
.subckt SUB_SMAJ40CA p1 p2
D1 p1 mid D_TVS40_UNI
D2 p2 mid D_TVS40_UNI
Rleak p1 p2 100Meg
.ends

* Bridge rectifier DF06S-T (1=+, 2=-, 3=~, 4=~)
.subckt SUB_DF06S pos neg ac1 ac2
Da1 ac1 pos D_DF06S
Da2 ac2 pos D_DF06S
Db1 neg ac1 D_DF06S
Db2 neg ac2 D_DF06S
.ends

* NMOS with explicit S->D body diode + realistic Ciss/Crss
.subckt SUB_IRLML0060 g s d
M1 d g s s M_IRLML0060 W=1 L=1
Dbody s d D_BODY
Cgs g s 250p
Cgd g d 35p
.ends

.subckt SUB_2N7002 g s d
M1 d g s s M_2N7002 W=1 L=1
Dbody s d D_BODY
.ends

* PMOS AO3401A (1=G, 2=S, 3=D; body tied to S so body diode points D -> S!)
.subckt SUB_AO3401A g s d
M1 d g s s M_AO3401A W=1 L=1
Dbody d s D_BODY
Cgs g s 300p
Cgd g d 50p
.ends

* Toshiba TLP383 optocoupler (1=A, 2=NC, 3=K, 4=E, 5=NC, 6=C)
.subckt SUB_TLP383 a nc2 k e nc5 c
Dled a a_int D_OPTO_LED
Vsns a_int k DC 0
Rnc2 nc2 k 1G
Rnc5 nc5 e 1G
Bp_in p_in e V = {spos(I(Vsns), 1e-6)}
Rp p_in p_flt 1k
Cp p_flt e 2n
Bic c e I = {1.6 * V(p_flt, e) * stanh(V(c, e) / 0.10)}
Rce c e 100Meg
.ends

* SCT2400 40V buck regulator (1=BST, 2=GND, 3=FB, 4=EN, 5=VIN, 6=SW)
* Dominant pole referenced to GND; drives real external L2 + Cout + FB divider
.subckt SUB_SCT2400 bst gnd fb en vin sw
Ren en gnd 480k
Rbst bst sw 100k
Rin_q vin gnd 150k
Bea ea_raw gnd V = {0.5 * spos(V(vin, gnd), 0.1) * (1.0 + stanh(90.0 * (0.81 - V(fb, gnd))))}
Rpole ea_raw ea_flt 1k
Cpole ea_flt gnd 159n
Bsw sw_int gnd V = {V(ea_flt, gnd) * 0.5 * (1.0 + stanh((V(en, gnd) - 1.21) / 0.035)) * 0.5 * (1.0 + stanh((V(vin, gnd) - 4.3) / 0.2))}
Rsw sw_int sw 0.15
.ends

* RECOM REC3-2415SRW/H2/A/CTRL (1=CTRL, 2=VIN_N, 14=VOUT_P, 16=VOUT_N, 22=VIN_P, 23=VIN_P2)
.subckt SUB_REC3_CTRL ctrl vin_n vout_p vout_n vin_p vin_p2
Rvp vin_p vin_p2 1m
Rctrl ctrl vin_n 100k
Rin vin_p vin_n 12k
Bon en_n vout_n V = {0.5 * (1.0 + stanh((V(vin_p, vin_n) - 17.0) / 0.5)) * 0.5 * (1.0 - stanh((V(ctrl, vin_n) - 1.70) / 0.15))}
Rflt en_n en_f 1k
Cflt en_f vout_n 10n
Bvout v_int vout_n V = {15.0 * V(en_f, vout_n)}
Rout v_int vout_p 0.35
.ends

* TRACO TMR 2411 (1=VIN_N, 2=VIN_P, 3=REM, 6=VOUT_P, 7=VOUT_N)
.subckt SUB_TMR2411 vin_n vin_p rem vout_p vout_n
Rrem rem vin_n 100k
Rin vin_p vin_n 8k
Bon en_n vout_n V = {0.5 * (1.0 + stanh((V(vin_p, vin_n) - 17.0) / 0.5))}
Rflt en_n en_f 500
Cflt en_f vout_n 10n
Bvout v_int vout_n V = {5.00 * V(en_f, vout_n)}
Rout v_int vout_p 0.08
.ends

* TRACO TRN 3-2411 (1=VIN_N, 2=VIN_P, 3=VOUT_P, 5=VOUT_N)
.subckt SUB_TRN3_2411 vin_n vin_p vout_p vout_n
Rin vin_p vin_n 8k
Bon en_n vout_n V = {0.5 * (1.0 + stanh((V(vin_p, vin_n) - 17.0) / 0.5))}
Rflt en_n en_f 500
Cflt en_f vout_n 10n
Bvout v_int vout_n V = {5.00 * V(en_f, vout_n)}
Rout v_int vout_p 0.06
.ends

* TLV803M 4.38 V open-drain active-low supervisor (1=GND, 2=RESET_N, 3=VCC)
.subckt SUB_TLV803M gnd rst_n vcc
Rvcc vcc gnd 500k
Brst rst_n gnd I = {0.015 * V(rst_n, gnd) * 0.5 * (1.0 + stanh((4.38 - V(vcc, gnd)) / 0.025))}
.ends

* TI LM66100DCKR ideal diode (1=VIN, 2=GND, 3=CE_N, 4=ST, 5=NC, 6=VOUT)
* Strictly monotonic C^inf RCB law: forward R_on ~ 85 mOhm, reverse leakage < 20 nA
.subckt SUB_LM66100 vin gnd ce_n st nc5 vout
Rg gnd 0 1G
Rst st gnd 1G
Rnc nc5 gnd 1G
Rce ce_n gnd 10Meg
Bid vin vout I = {spos(V(vin, vout) - 0.020, 0.005) / 0.085}
.ends

* TI SN74AHCT1G125 5 V buffer with TTL VIH=2.0V threshold (1=OE_N, 2=A, 3=GND, 4=Y, 5=VCC)
.subckt SUB_74AHCT1G125 oe_n a gnd y vcc
Rin_a a gnd 10Meg
Rin_oe oe_n gnd 10Meg
Rvcc vcc gnd 500k
By y_int gnd V = {V(vcc, gnd) * 0.5 * (1.0 + stanh((V(a, gnd) - 1.40) / 0.08)) * 0.5 * (1.0 - stanh((V(oe_n, gnd) - 1.40) / 0.08))}
Rout y_int y 45
.ends

* WS2812B-MINI-V3J addressable RGB LED load & regenerating buffer (1=VDD, 2=DOUT, 3=GND, 4=DIN)
.subckt SUB_WS2812B vdd dout gnd din
Rbias vdd gnd 5k
Rdin din gnd 5Meg
Cdin din gnd 5p
Bdout d_int gnd V = {V(vdd, gnd) * 0.5 * (1.0 + stanh((V(din, gnd) - 2.2) / 0.1))}
Rdout d_int dout 60
.ends
"""


def emit_system_netlist(
    sys_ext: StitchedSystem,
    *,
    extra_lines: list[str],
    jp1_closed: bool = True,
    sw1_closed: bool = False,
    sw2_closed: bool = False,
    v_led_gpio: str = "DC 0",
    v_sup_off_gpio: str = "DC 0",
    v_dali_tx_gpio: str = "DC 0",
    v33_ac_mag: float = 0.0,
) -> str:
    lines = ["Copper-extracted KNX-DALI Gateway SPICE Deck", SPICE_MODELS_AND_SUBCKTS]

    # Galvanic barrier leakage + isolation capacitance across SELV-KNX (0), SELV-AUX, DALI
    lines.append(f"R_iso_aux {sys_ext.gnda_node} 0 1G")
    lines.append(f"C_iso_aux {sys_ext.gnda_node} 0 15p")
    lines.append(f"R_iso_dali {sys_ext.gndd_node} 0 1G")
    lines.append(f"C_iso_dali {sys_ext.gndd_node} 0 20p")

    # Distributed trace C parasitics extracted from real copper track lengths
    for idx, (node, c_val) in enumerate(sorted(sys_ext.trace_c_f.items())):
        if node in ("0", sys_ext.gnda_node, sys_ext.gndd_node) or c_val <= 0:
            continue
        lines.append(f"C_cu_{idx} {node} 0 {c_val:.3e}")

    # Inter-board FFC / cable parasitic R + C links (`main:J4` <-> `ui:J1`)
    for idx, (na, nb) in enumerate(sys_ext.cable_links):
        if na == nb:
            continue
        lines.append(f"R_ffc_{idx} {na} {nb} 0.05")
        if na != "0":
            lines.append(f"C_ffc_{idx} {na} 0 3p")

    def pnode(board: str, ref: str, pin: str, default_nc: str | None = None) -> str:
        key = (board, ref, pin)
        if key in sys_ext.global_pad_node:
            return sys_ext.global_pad_node[key]
        assert default_nc is not None, f"missing physical pad {key}"
        return default_nc

    for bname, ext in sorted(sys_ext.boards.items()):
        for ref, info in sorted(ext.bom.items()):
            mpn = info["mpn"]
            val = info["value"]
            tag = f"{bname}_{ref}"
            r_trace = 0.0
            if (bname, ref, "1") in sys_ext.global_pad_node:
                r_trace = min(0.25, sys_ext.trace_r_ohm.get(sys_ext.global_pad_node[(bname, ref, "1")], 0.0))

            if ref.startswith("R"):
                n1, n2 = pnode(bname, ref, "1"), pnode(bname, ref, "2")
                ohms = parse_si_value(val) + r_trace
                lines.append(f"R_{tag} {n1} {n2} {ohms:.6g}")

            elif ref.startswith("C"):
                n1, n2 = pnode(bname, ref, "1"), pnode(bname, ref, "2")
                farads = parse_si_value(val)
                esr = 0.08 if farads >= 50e-6 else (0.02 if farads >= 1e-6 else 0.05)
                lines.append(f"R_esr_{tag} {n1} c_int_{tag} {esr + r_trace:.4g}")
                lines.append(f"C_{tag} c_int_{tag} {n2} {farads:.6g}")

            elif ref.startswith("L"):
                n1, n2 = pnode(bname, ref, "1"), pnode(bname, ref, "2")
                henries = parse_si_value(val)
                dcr = 0.35 if henries >= 100e-6 else 0.09
                lines.append(f"R_dcr_{tag} {n1} l_int_{tag} {dcr + r_trace:.4g}")
                lines.append(f"L_{tag} l_int_{tag} {n2} {henries:.6g}")

            elif ref.startswith("F"):
                # 1812L110/33MR PTC cold resistance ~0.12 Ohm
                n1, n2 = pnode(bname, ref, "1"), pnode(bname, ref, "2")
                lines.append(f"R_ptc_{tag} {n1} {n2} {0.12 + r_trace:.4g}")

            elif ref.startswith("Y"):
                n1, n2 = pnode(bname, ref, "1"), pnode(bname, ref, "2")
                lines.append(f"C_xtal0_{tag} {n1} {n2} 4p")
                lines.append(f"R_xtal_leak_{tag} {n1} {n2} 10Meg")

            elif ref.startswith("SW"):
                n1, n2 = pnode(bname, ref, "1"), pnode(bname, ref, "2")
                closed = (sw1_closed and ref == "SW1") or (sw2_closed and ref == "SW2")
                lines.append(f"R_{tag} {n1} {n2} {'0.05' if closed else '100Meg'}")

            elif ref == "JP1":
                p1, p2 = pnode(bname, ref, "1"), pnode(bname, ref, "2")
                p3, p4 = pnode(bname, ref, "3"), pnode(bname, ref, "4")
                r_jp = "0.02" if jp1_closed else "100Meg"
                lines.append(f"R_{tag}_a {p1} {p2} {r_jp}")
                lines.append(f"R_{tag}_b {p3} {p4} {r_jp}")

            elif mpn == "SS16":
                lines.append(f"D_{tag} {pnode(bname, ref, '2')} {pnode(bname, ref, '1')} D_SS16")
            elif mpn == "B5819W SL":
                lines.append(f"D_{tag} {pnode(bname, ref, '2')} {pnode(bname, ref, '1')} D_B5819W")
            elif mpn == "MMSZ5V6T1G":
                lines.append(f"D_{tag} {pnode(bname, ref, '2')} {pnode(bname, ref, '1')} D_Z5V6")
            elif mpn == "MMSZ12T1G":
                lines.append(f"D_{tag} {pnode(bname, ref, '2')} {pnode(bname, ref, '1')} D_Z12V")
            elif mpn == "SMAJ24A":
                lines.append(f"D_{tag} {pnode(bname, ref, '2')} {pnode(bname, ref, '1')} D_TVS24")
            elif mpn == "SMAJ33A":
                lines.append(f"D_{tag} {pnode(bname, ref, '2')} {pnode(bname, ref, '1')} D_TVS33")
            elif mpn == "SMAJ40CA":
                lines.append(f"X_{tag} {pnode(bname, ref, '1')} {pnode(bname, ref, '2')} SUB_SMAJ40CA")
            elif mpn == "DF06S-T":
                lines.append(
                    f"X_{tag} {pnode(bname, ref, '1')} {pnode(bname, ref, '2')} "
                    f"{pnode(bname, ref, '3')} {pnode(bname, ref, '4')} SUB_DF06S"
                )
            elif mpn == "BC847B":
                # SOT-23: 1=B, 2=E, 3=C -> SPICE Q: C B E
                lines.append(
                    f"Q_{tag} {pnode(bname, ref, '3')} {pnode(bname, ref, '1')} {pnode(bname, ref, '2')} Q_BC847"
                )
            elif mpn == "BC857C":
                lines.append(
                    f"Q_{tag} {pnode(bname, ref, '3')} {pnode(bname, ref, '1')} {pnode(bname, ref, '2')} Q_BC857"
                )
            elif mpn == "MJD45H11":
                # TO-252-2: 1=B, 2=C, 3=E -> SPICE Q: C B E
                lines.append(
                    f"Q_{tag} {pnode(bname, ref, '2')} {pnode(bname, ref, '1')} {pnode(bname, ref, '3')} Q_MJD45H11"
                )
            elif mpn == "IRLML0060TRPBF":
                lines.append(
                    f"X_{tag} {pnode(bname, ref, '1')} {pnode(bname, ref, '2')} {pnode(bname, ref, '3')} SUB_IRLML0060"
                )
            elif mpn == "2N7002":
                lines.append(
                    f"X_{tag} {pnode(bname, ref, '1')} {pnode(bname, ref, '2')} {pnode(bname, ref, '3')} SUB_2N7002"
                )
            elif mpn == "AO3401A":
                lines.append(
                    f"X_{tag} {pnode(bname, ref, '1')} {pnode(bname, ref, '2')} {pnode(bname, ref, '3')} SUB_AO3401A"
                )
            elif mpn == "TLP383(D4BLLTL,E":
                lines.append(
                    f"X_{tag} {pnode(bname, ref, '1')} {pnode(bname, ref, '2', f'nc2_{tag}')} "
                    f"{pnode(bname, ref, '3')} {pnode(bname, ref, '4')} "
                    f"{pnode(bname, ref, '5', f'nc5_{tag}')} {pnode(bname, ref, '6')} SUB_TLP383"
                )
            elif mpn == "SCT2400TVBR":
                lines.append(
                    f"X_{tag} {pnode(bname, ref, '1')} {pnode(bname, ref, '2')} {pnode(bname, ref, '3')} "
                    f"{pnode(bname, ref, '4')} {pnode(bname, ref, '5')} {pnode(bname, ref, '6')} SUB_SCT2400"
                )
            elif mpn == "REC3-2415SRW/H2/A/CTRL":
                lines.append(
                    f"X_{tag} {pnode(bname, ref, '1')} {pnode(bname, ref, '2')} {pnode(bname, ref, '14')} "
                    f"{pnode(bname, ref, '16')} {pnode(bname, ref, '22')} {pnode(bname, ref, '23')} SUB_REC3_CTRL"
                )
            elif mpn == "TMR 2411":
                lines.append(
                    f"X_{tag} {pnode(bname, ref, '1')} {pnode(bname, ref, '2')} "
                    f"{pnode(bname, ref, '3', f'rem_{tag}')} {pnode(bname, ref, '6')} {pnode(bname, ref, '7')} SUB_TMR2411"
                )
            elif mpn == "TRN 3-2411":
                lines.append(
                    f"X_{tag} {pnode(bname, ref, '1')} {pnode(bname, ref, '2')} "
                    f"{pnode(bname, ref, '3')} {pnode(bname, ref, '5')} SUB_TRN3_2411"
                )
            elif mpn == "TLV803MDBZR-TP":
                lines.append(
                    f"X_{tag} {pnode(bname, ref, '1')} {pnode(bname, ref, '2')} {pnode(bname, ref, '3')} SUB_TLV803M"
                )
            elif mpn == "LM66100DCKR":
                lines.append(
                    f"X_{tag} {pnode(bname, ref, '1')} {pnode(bname, ref, '2')} {pnode(bname, ref, '3')} "
                    f"{pnode(bname, ref, '4', f'st_{tag}')} {pnode(bname, ref, '5', f'nc_{tag}')} "
                    f"{pnode(bname, ref, '6')} SUB_LM66100"
                )
            elif mpn == "SN74AHCT1G125DBVR":
                lines.append(
                    f"X_{tag} {pnode(bname, ref, '1')} {pnode(bname, ref, '2')} {pnode(bname, ref, '3')} "
                    f"{pnode(bname, ref, '4')} {pnode(bname, ref, '5')} SUB_74AHCT1G125"
                )
            elif mpn == "WS2812B-MINI-V3J":
                lines.append(
                    f"X_{tag} {pnode(bname, ref, '1')} {pnode(bname, ref, '2', f'dout_{tag}')} "
                    f"{pnode(bname, ref, '3')} {pnode(bname, ref, '4')} SUB_WS2812B"
                )
            elif mpn == "102010636":
                # Seeed XIAO ESP32C6: onboard 5V -> 3.30V buck on pin 12, GPIO drivers & high-Z inputs
                n_gnd = pnode(bname, ref, "13")
                n_5v = pnode(bname, ref, "14")
                n_3v3 = pnode(bname, ref, "12")
                lines.append(f"R_xiao_5v_{tag} {n_5v} {n_gnd} 120")  # ~40 mA MCU core load on +5V_XIAO
                lines.append(
                    f"B_xiao_3v3_{tag} x3v3_reg_{tag} {n_gnd} V = "
                    f"{{3.30 * 0.5 * (1.0 + stanh((V({n_5v}, {n_gnd}) - 3.4) / 0.15))}}"
                )
                lines.append(f"V_3v3_ac x3v3_ac_{tag} x3v3_reg_{tag} DC 0 AC {v33_ac_mag}")
                lines.append(f"R_xiao_3v3_{tag} x3v3_ac_{tag} {n_3v3} 0.12")
                # Controlled GPIO outputs (35 Ohm CMOS driver impedance)
                for pnum, pname, src_spec in [
                    ("1", "led", v_led_gpio),
                    ("2", "supoff", v_sup_off_gpio),
                    ("11", "dalitx", v_dali_tx_gpio),
                ]:
                    np = pnode(bname, ref, pnum)
                    lines.append(f"V_gpio_{pname} gpio_{pname}_{tag} {n_gnd} {src_spec}")
                    lines.append(f"R_gpio_{pname} gpio_{pname}_{tag} {np} 35")
                # High-impedance ADC / digital inputs
                for pnum in ("3", "4", "5", "6", "7", "8", "9", "10"):
                    if (bname, ref, pnum) in sys_ext.global_pad_node:
                        np = pnode(bname, ref, pnum)
                        lines.append(f"R_xin_{pnum}_{tag} {np} {n_gnd} 10Meg")
                        lines.append(f"C_xin_{pnum}_{tag} {np} {n_gnd} 4p")

            elif mpn == "NCN5130MNTWG":
                # Coupler drops ~2.5 V from VBUS1 (6) to VFILT (9); DC1 produces 3.30 V on VDD1 (19) when VFILT > 9.5 V
                n_vbus1 = pnode(bname, ref, "6")
                n_gnd = pnode(bname, ref, "1")
                n_vfilt = pnode(bname, ref, "9")
                n_v3t = pnode(bname, ref, "19")
                lines.append(f"R_ncn_vbus_{tag} {n_vbus1} {n_gnd} 150k")
                lines.append(f"D_ncn_sub_{tag} {n_gnd} {n_vbus1} D_BODY")
                lines.append(
                    f"B_ncn_cpl_{tag} {n_vbus1} {n_vfilt} I = "
                    f"{{(spos(V({n_vbus1}, {n_vfilt}) - 2.2, 0.1) / 8.0) * 0.5 * (1.0 + stanh((V({n_vbus1}, {n_gnd}) - 10.0) / 0.5))}}"
                )
                lines.append(f"R_ncn_iq_{tag} {n_vfilt} {n_gnd} 8k")
                lines.append(
                    f"B_ncn_3v3_{tag} ncn33_{tag} {n_gnd} V = "
                    f"{{3.30 * 0.5 * (1.0 + stanh((V({n_vfilt}, {n_gnd}) - 9.4) / 0.25))}}"
                )
                lines.append(f"R_ncn_3v3_{tag} ncn33_{tag} {n_v3t} 0.5")
                # Tie remaining connected signal pins with realistic high-Z termination so zero pins float
                for pnum in ("3", "4", "5", "7", "8", "17", "20", "27", "28", "34", "35", "36", "38"):
                    np = pnode(bname, ref, pnum)
                    lines.append(f"R_ncn_p{pnum}_{tag} {np} {n_gnd} 2Meg")

            elif mpn in ("QG-6428TSWCG02", "WEA012864DWPP3N00003"):
                # OLED module quiescent load + pin terminations so all 4 or 14 pins are terminated
                n_gnd = pnode(bname, ref, "7" if mpn == "QG-6428TSWCG02" else "2")
                n_vcc = pnode(bname, ref, "8" if mpn == "QG-6428TSWCG02" else "1")
                lines.append(f"R_oled_load_{tag} {n_vcc} {n_gnd} 330")
                for (b_k, r_k, p_k), sn in sys_ext.global_pad_node.items():
                    if b_k == bname and r_k == ref and sn not in (n_gnd, n_vcc):
                        lines.append(f"R_oled_p{p_k}_{tag} {sn} {n_gnd} 5Meg")

    lines.extend(extra_lines)
    lines.append(".end")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Pytest fixtures & verification suite
# ---------------------------------------------------------------------------

@pytest.fixture(scope="module")
def sys_ext() -> StitchedSystem:
    return build_stitched_system()


def find_mpn_pad_node(sys_ext: StitchedSystem, mpn: str, pin: str, board: str | None = None) -> str:
    for b, ext in sys_ext.boards.items():
        if board and b != board:
            continue
        for ref, info in ext.bom.items():
            if info["mpn"] == mpn:
                return sys_ext.global_pad_node[(b, ref, pin)]
    raise KeyError((mpn, pin, board))


def test_zero_circuit_generator_imports():
    forbidden = {"skidl", "kinet2pcb", "gen_circuits", "product_spec", "board_variant", "common", "layout", "blocks"}
    tree = ast.parse(Path(__file__).read_text())
    imported = {
        alias.name.split(".")[0]
        for node in ast.walk(tree) if isinstance(node, ast.Import)
        for alias in node.names
    } | {
        node.module.split(".")[0]
        for node in ast.walk(tree) if isinstance(node, ast.ImportFrom) and node.module
    }
    assert not (imported & forbidden), f"SPICE gate imports generator modules: {imported & forbidden}"


def test_copper_union_find_and_galvanic_isolation(sys_ext: StitchedSystem):
    """Physical copper Union-Find matches board connectivity 1-for-1, stitches 3D B2B
    connectors strictly by `(X, Y)` <= 0.05 mm, and proves all 3 grounds are isolated."""
    assert len({sys_ext.gnd_node, sys_ext.gnda_node, sys_ext.gndd_node}) == 3

    for b, ext in sys_ext.boards.items():
        # Every KiCad net maps to a unique copper Union-Find cluster (zero copper opens, zero shorts)
        net_to_clusters: dict[str, set[int]] = {}
        cluster_to_nets: dict[int, set[str]] = {}
        for pad_key, net_name in ext.kicad_pad_nets.items():
            cid = ext.pad_node[pad_key]
            net_to_clusters.setdefault(net_name, set()).add(cid)
            cluster_to_nets.setdefault(cid, set()).add(net_name)
        opens = {n: c for n, c in net_to_clusters.items() if len(c) != 1}
        shorts = {c: ns for c, ns in cluster_to_nets.items() if len(ns) != 1}
        assert not opens, f"{b}: copper open circuits detected by Union-Find: {opens}"
        assert not shorts, f"{b}: copper short circuits detected by Union-Find: {shorts}"

    if "interface" in sys_ext.boards:
        assert len(sys_ext.b2b_matched_pairs) == 7
        assert all(d <= 0.05 for _, _, d in sys_ext.b2b_matched_pairs)


def test_input_reverse_polarity_and_surge_clamping(sys_ext: StitchedSystem):
    """`.op` + `.dc`: +24V normal feed vs -30V reverse polarity and surge clamping
    on both KNX (J1) and AUX (J2) terminals."""
    sim = ngspice()
    n_knx_in = sys_ext.global_pad_node[("main", "J1", "1")]
    n_knx_prot = find_mpn_pad_node(sys_ext, "NCN5130MNTWG", "6", "main")
    n_aux_in = sys_ext.global_pad_node[("main", "J2", "1")]
    n_aux_p = find_mpn_pad_node(sys_ext, "REC3-2415SRW/H2/A/CTRL", "22", "main")
    n_gnda = sys_ext.gnda_node

    # 1. Normal +24 V on KNX and +24 V on AUX
    deck_fwd = emit_system_netlist(
        sys_ext,
        extra_lines=[
            f"V_knx {n_knx_in} 0 DC 24.0",
            f"V_aux {n_aux_in} {n_gnda} DC 24.0",
            ".op",
        ],
    )
    sim.run_deck(deck_fwd)
    v_knx_prot = sim.vec(f"v({n_knx_prot})")[0]
    v_aux_p = sim.vec(f"v({n_aux_p})")[0] - sim.vec(f"v({n_gnda})")[0]
    assert 23.2 <= v_knx_prot <= 24.0, f"forward KNX_P_PROT={v_knx_prot:.2f} V"
    assert 23.0 <= v_aux_p <= 24.0, f"forward AUX_P={v_aux_p:.2f} V"

    # 2. Reverse -30 V on BOTH KNX and AUX: Schottky diodes block reverse bias cleanly
    deck_rev = emit_system_netlist(
        sys_ext,
        extra_lines=[
            f"V_knx {n_knx_in} 0 DC -30.0",
            f"V_aux {n_aux_in} {n_gnda} DC -30.0",
            ".op",
        ],
    )
    sim.run_deck(deck_rev)
    v_knx_rev = sim.vec(f"v({n_knx_prot})")[0]
    v_aux_rev = sim.vec(f"v({n_aux_p})")[0] - sim.vec(f"v({n_gnda})")[0]
    assert -0.25 <= v_knx_rev <= 0.1, f"reverse -30V leaked onto KNX_P_PROT={v_knx_rev:.3f} V"
    assert -0.25 <= v_aux_rev <= 0.1, f"reverse -30V leaked onto AUX_P={v_aux_rev:.3f} V"

    # 3. Surge overload clamping on AUX via 10 Ohm source resistance (80 V surge clamped by SMAJ33A < 45 V)
    deck_surge = emit_system_netlist(
        sys_ext,
        extra_lines=[
            f"V_knx {n_knx_in} 0 DC 24.0",
            f"V_aux_s aux_src {n_gnda} DC 80.0",
            f"R_aux_s aux_src {n_aux_in} 10.0",
            ".op",
        ],
    )
    sim.run_deck(deck_surge)
    v_aux_clamp = sim.vec(f"v({n_aux_p})")[0] - sim.vec(f"v({n_gnda})")[0]
    assert 35.0 <= v_aux_clamp <= 44.0, f"SMAJ33A surge clamp out of range: {v_aux_clamp:.2f} V"


def test_sct2400_buck_uvlo_regulation_and_usb_reverse_blocking(sys_ext: StitchedSystem):
    """`.dc` + `.op`: SCT2400 EN UVLO starts below 16.4 V (21 V KNX bus), stops above
    9.7 V (NCN5130 VFILT reset), regulates +5V_BUCK ~4.94 V, and LM66100 blocks USB 5 V back-feed."""
    sim = ngspice()
    n_vfilt = find_mpn_pad_node(sys_ext, "SCT2400TVBR", "5", "main")
    n_5v = find_mpn_pad_node(sys_ext, "LM66100DCKR", "1")
    n_5v_xiao = find_mpn_pad_node(sys_ext, "LM66100DCKR", "6")
    n_3v3 = find_mpn_pad_node(sys_ext, "102010636", "12")
    n_aux_in = sys_ext.global_pad_node[("main", "J2", "1")]

    # 1. DC sweep VFILT 8 V -> 26 V with AUX=0 (KNX-only power)
    deck_uvlo = emit_system_netlist(
        sys_ext,
        extra_lines=[
            f"V_vfilt {n_vfilt} 0 DC 20.0",
            f"V_aux {n_aux_in} {sys_ext.gnda_node} DC 0.0",
            ".dc V_vfilt 8.0 26.0 0.25",
        ],
    )
    sim.run_deck(deck_uvlo)
    vfilt_sweep = sim.vec("v-sweep")
    v5_rail = sim.vec(f"v({n_5v})")
    v33_rail = sim.vec(f"v({n_3v3})")

    # At VFILT <= 9.7 V (NCN5130 reset guard), buck must be OFF (< 0.5 V)
    off_vals = [v5 for vf, v5 in zip(vfilt_sweep, v5_rail) if vf <= 9.7]
    assert max(off_vals) < 0.5, f"buck failed to shut off before NCN5130 reset: {max(off_vals):.2f} V"

    # At VFILT >= 16.5 V (21 V KNX bus minus coupler drop), buck + 3V3 must be fully ON
    on_v5 = [v5 for vf, v5 in zip(vfilt_sweep, v5_rail) if vf >= 16.5]
    on_v33 = [v3 for vf, v3 in zip(vfilt_sweep, v33_rail) if vf >= 16.5]
    assert all(4.45 <= v5 <= 4.95 for v5 in on_v5), f"KNX-only +5V rail out of regulation: {min(on_v5):.2f}..{max(on_v5):.2f}"
    assert all(3.25 <= v3 <= 3.35 for v3 in on_v33), f"XIAO +3V3 out of regulation: {min(on_v33):.2f}..{max(on_v33):.2f}"

    # 2. USB-only service mode (+5V_XIAO driven to 5.0 V while KNX=0 and AUX=0): LM66100 blocks back-feed
    n_knx_in = sys_ext.global_pad_node[("main", "J1", "1")]
    deck_usb = emit_system_netlist(
        sys_ext,
        extra_lines=[
            f"V_knx {n_knx_in} 0 DC 0.0",
            f"V_aux {n_aux_in} {sys_ext.gnda_node} DC 0.0",
            f"V_usb {n_5v_xiao} 0 DC 5.0",
            ".op",
        ],
    )
    sim.run_deck(deck_usb)
    v5_backfed = sim.vec(f"v({n_5v})")[0]
    assert v5_backfed < 0.05, f"LM66100 failed to block USB 5V back-feed onto +5V core rail: {v5_backfed:.3f} V"


def test_core_power_priority_mux_and_hot_unplug_transient(sys_ext: StitchedSystem):
    """`.op` + `.tran`: AUX converter takes priority (~4.98 V via AO3401A PMOS while
    B5819W reverse-blocks KNX buck), and hot-unplugging AUX hands over seamlessly to KNX."""
    sim = ngspice()
    n_knx_in = sys_ext.global_pad_node[("main", "J1", "1")]
    n_aux_in = sys_ext.global_pad_node[("main", "J2", "1")]
    n_gnda = sys_ext.gnda_node
    n_5v = find_mpn_pad_node(sys_ext, "AO3401A", "2", "main")
    n_5v_aux = find_mpn_pad_node(sys_ext, "AO3401A", "3", "main")
    n_pfet_g = find_mpn_pad_node(sys_ext, "AO3401A", "1", "main")
    n_3v3 = find_mpn_pad_node(sys_ext, "102010636", "12")

    # 1. Both AUX (24 V) and KNX (24 V) present: Q9 PMOS gate pulled near 0 V, +5V fed from +5V_AUX
    deck_dual = emit_system_netlist(
        sys_ext,
        extra_lines=[
            f"V_knx {n_knx_in} 0 DC 24.0",
            f"V_aux {n_aux_in} {n_gnda} DC 24.0",
            ".op",
        ],
    )
    sim.run_deck(deck_dual)
    assert sim.vec(f"v({n_pfet_g})")[0] < 0.2, "Q9 PMOS gate not pulled low when AUX is healthy"
    assert 4.90 <= sim.vec(f"v({n_5v})")[0] <= 5.05
    assert abs(sim.vec(f"v({n_5v_aux})")[0] - sim.vec(f"v({n_5v})")[0]) < 0.05

    # 2. Transient hot-unplug of AUX at t=200 us while KNX (24 V) is present
    deck_tran = emit_system_netlist(
        sys_ext,
        extra_lines=[
            f"V_knx {n_knx_in} 0 DC 24.0",
            f"V_aux {n_aux_in} {n_gnda} PULSE(24.0 0.0 200u 20u 20u 10m 20m)",
            ".tran 5u 1.5m",
        ],
    )
    sim.run_deck(deck_tran)
    v5_t = sim.vec(f"v({n_5v})")
    v33_t = sim.vec(f"v({n_3v3})")
    assert min(v5_t) >= 4.40, f"+5V drooped below 4.40 V during AUX->KNX handover: {min(v5_t):.2f} V"
    assert min(v33_t) >= 3.25, f"+3V3 glitched during AUX->KNX handover: {min(v33_t):.2f} V"


def test_ac_filter_stability_and_adc_ladder_noise(sys_ext: StitchedSystem):
    """`.ac` + `.noise` (`setplot noise1` & `noise2`): verifies BTN_LADDER RC filter
    corner (~159 Hz, >30 dB HF rejection at 100 kHz, zero peaking) and integrated RMS noise."""
    sim = ngspice()
    n_knx_in = sys_ext.global_pad_node[("main", "J1", "1")]
    n_aux_in = sys_ext.global_pad_node[("main", "J2", "1")]
    n_btn = find_mpn_pad_node(sys_ext, "102010636", "3")

    # 1. AC frequency response from +3V3 rail into BTN_LADDER ADC input (10k + 100nF -> fc ~ 159 Hz)
    deck_ac = emit_system_netlist(
        sys_ext,
        v33_ac_mag=1.0,
        extra_lines=[
            f"V_knx {n_knx_in} 0 DC 24.0",
            f"V_aux {n_aux_in} {sys_ext.gnda_node} DC 24.0",
            ".ac dec 25 1 100k",
        ],
    )
    sim.run_deck(deck_ac)
    assert len(sim.vec("frequency")) > 50
    mag = [abs(c) for c in sim.cvec(f"v({n_btn})")]
    assert max(mag) <= 1.02, f"undamped peaking detected on BTN_LADDER: max gain {max(mag):.3f}"
    gain_1hz = mag[0]
    gain_100khz = mag[-1]
    assert 0.98 <= gain_1hz <= 1.01
    assert gain_100khz < 0.005, f"BTN_LADDER HF rejection insufficient at 100 kHz: {gain_100khz:.4f}"

    # 1b. Closed-loop SCT2400 buck power-rail AC PSRR / LC damping check (VFILT AC ripple -> +5V_BUCK)
    n_vfilt = find_mpn_pad_node(sys_ext, "SCT2400TVBR", "5", "main")
    n_5v_buck = find_mpn_pad_node(sys_ext, "B5819W SL", "2", "main")
    deck_buck_ac = emit_system_netlist(
        sys_ext,
        extra_lines=[
            f"V_vfilt_ac {n_vfilt} 0 DC 20.0 AC 1.0",
            f"V_aux {n_aux_in} {sys_ext.gnda_node} DC 0.0",
            ".ac dec 25 10 1Meg",
        ],
    )
    sim.run_deck(deck_buck_ac)
    psrr_mag = [abs(c) for c in sim.cvec(f"v({n_5v_buck})")]
    # Closed-loop buck + real L2 (4.7uH) + 2x22uF Cout must attenuate line ripple (< 0.25 V/V everywhere, no >6 dB LC spike)
    assert max(psrr_mag) < 0.25, f"SCT2400 buck loop peaking / poor PSRR: max |Vout/Vin|={max(psrr_mag):.3f}"

    # 2. Thermal noise analysis (.noise -> setplot noise1 spectral + noise2 integrated RMS)
    deck_noise = emit_system_netlist(
        sys_ext,
        v33_ac_mag=1.0,
        extra_lines=[
            f"V_knx {n_knx_in} 0 DC 24.0",
            f"V_aux {n_aux_in} {sys_ext.gnda_node} DC 24.0",
            f".noise v({n_btn}) v_3v3_ac dec 20 1 100k",
        ],
    )
    sim.run_deck(deck_noise)
    sim.cmd("setplot noise1")
    spec = sim.vec("onoise_spectrum")
    assert len(spec) > 50 and max(spec) < 1e-6
    sim.cmd("setplot noise2")
    onoise_rms = sim.vec("onoise_total")[0]
    assert 50e-9 <= onoise_rms <= 10e-6, f"unexpected ADC ladder integrated RMS noise: {onoise_rms:.3e} V"


def test_dali_supply_limiter_ctrl_shutoff_tx_and_rx_thresholds(sys_ext: StitchedSystem):
    """End-to-end DALI verification on copper-extracted circuit:
    - Internal supply idle voltage (14..15 V) & quiescent sink (< 2 mA)
    - Short-circuit current limiting via MJD45H11 + 3R3 + BC857C (150..240 mA <= 250 mA)
    - DALI_SUPPLY_OFF GPIO shuts off REC3 via optocoupler + 5.6 V CTRL clamp
    - DALI RX zener + two-NPN limiter: HIGH (>3 V) at <=6.5 V bus, LOW (<0.4 V) at >=9.5 V bus
    - DALI TX: <4.5 V at 250 mA sink, V_GS held >=4.5 V across 1.3 ms collision break, 12 V zener clamp, fault clamp."""
    sim = ngspice()
    n_knx_in = sys_ext.global_pad_node[("main", "J1", "1")]
    n_aux_in = sys_ext.global_pad_node[("main", "J2", "1")]
    n_da1 = sys_ext.global_pad_node[("main", "J3", "1")]
    n_da2 = sys_ext.global_pad_node[("main", "J3", "2")]
    n_dali_rx = find_mpn_pad_node(sys_ext, "102010636", "9")
    n_dali_gate = find_mpn_pad_node(sys_ext, "IRLML0060TRPBF", "1")
    n_gndd = sys_ext.gndd_node

    # 1. Internal DALI supply idle (JP1 closed, DALI_SUPPLY_OFF = 0 V)
    deck_idle = emit_system_netlist(
        sys_ext,
        jp1_closed=True,
        extra_lines=[
            f"V_knx {n_knx_in} 0 DC 24.0",
            f"V_aux {n_aux_in} {sys_ext.gnda_node} DC 24.0",
            ".op",
        ],
    )
    sim.run_deck(deck_idle)
    v_bus_idle = sim.vec(f"v({n_da1})")[0] - sim.vec(f"v({n_da2})")[0]
    assert 13.8 <= v_bus_idle <= 15.2, f"internal DALI idle bus voltage={v_bus_idle:.2f} V"
    assert sim.vec(f"v({n_dali_rx})")[0] < 0.35, "DALI_RX must be active-low when bus idles high"

    # 2. Sustained DALI bus short (0.05 Ohm across DA1-DA2): MJD45H11 + BC857C + 3R3 limits current <= 250 mA
    deck_short = emit_system_netlist(
        sys_ext,
        jp1_closed=True,
        extra_lines=[
            f"V_knx {n_knx_in} 0 DC 24.0",
            f"V_aux {n_aux_in} {sys_ext.gnda_node} DC 24.0",
            f"V_short {n_da1} da_sh DC 0",
            f"R_short da_sh {n_da2} 0.05",
            ".op",
        ],
    )
    sim.run_deck(deck_short)
    i_short = abs(sim.vec("i(v_short)")[0])
    assert 0.145 <= i_short <= 0.245, f"DALI internal supply short-circuit current {i_short * 1e3:.1f} mA outside 145..245 mA"

    # 3. DALI_SUPPLY_OFF = 3.3 V shuts off REC3 isolated supply via CTRL optocoupler
    deck_off = emit_system_netlist(
        sys_ext,
        jp1_closed=True,
        v_sup_off_gpio="DC 3.3",
        extra_lines=[
            f"V_knx {n_knx_in} 0 DC 24.0",
            f"V_aux {n_aux_in} {sys_ext.gnda_node} DC 24.0",
            ".op",
        ],
    )
    sim.run_deck(deck_off)
    v_bus_off = abs(sim.vec(f"v({n_da1})")[0] - sim.vec(f"v({n_da2})")[0])
    assert v_bus_off < 0.5, f"DALI_SUPPLY_OFF failed to disable REC3 output: {v_bus_off:.2f} V"

    # 4. External DALI bus DC sweep (0..22.5 V, JP1 open): quiescent sink <= 2 mA & RX thresholds (6.5 V / 9.5 V)
    deck_rx = emit_system_netlist(
        sys_ext,
        jp1_closed=False,
        extra_lines=[
            f"V_knx {n_knx_in} 0 DC 24.0",
            f"V_aux {n_aux_in} {sys_ext.gnda_node} DC 24.0",
            f"V_ext_dali {n_da1} {n_da2} DC 16.0",
            f"R_da2_ref {n_da2} {n_gndd} 1Meg",
            ".dc V_ext_dali 0.0 22.5 0.5",
        ],
    )
    sim.run_deck(deck_rx)
    v_sweep = sim.vec("v-sweep")
    v_rx_out = sim.vec(f"v({n_dali_rx})")
    i_bus_q = [abs(i) for i in sim.vec("i(v_ext_dali)")]
    assert max(i_bus_q) <= 2.0e-3, f"quiescent DALI bus current {max(i_bus_q) * 1e3:.2f} mA > 2 mA IEC limit"
    rx_below_6v5 = [vr for vb, vr in zip(v_sweep, v_rx_out) if vb <= 6.5]
    rx_above_9v5 = [vr for vb, vr in zip(v_sweep, v_rx_out) if vb >= 9.5]
    assert min(rx_below_6v5) >= 3.0, f"DALI_RX not guaranteed HIGH below 6.5 V: {min(rx_below_6v5):.2f} V"
    assert max(rx_above_9v5) <= 0.35, f"DALI_RX not guaranteed LOW above 9.5 V: {max(rx_above_9v5):.2f} V"

    # 5. DALI TX 250 mA low-level voltage (< 4.5 V) + 1.3 ms collision-break bootstrap gate hold + 12 V gate zener clamp
    for bus_v in (9.5, 22.5):
        # External 250 mA current-limited DALI PSU modeled by 250 mA source clamped to bus_v
        deck_tx = emit_system_netlist(
            sys_ext,
            jp1_closed=False,
            v_dali_tx_gpio="PULSE(0 3.3 200u 2u 2u 1.3m 3.0m)",
            extra_lines=[
                f"V_knx {n_knx_in} 0 DC 24.0",
                f"V_aux {n_aux_in} {sys_ext.gnda_node} DC 24.0",
                f"B_psu {n_da2} {n_da1} I = {{0.250 * 0.5 * (1.0 + stanh(({bus_v} - V({n_da1}, {n_da2})) / 0.15))}}",
                ".tran 10u 1.8m",
            ],
        )
        sim.run_deck(deck_tx)
        t_vec = sim.vec("time")
        v_da = [a - b for a, b in zip(sim.vec(f"v({n_da1})"), sim.vec(f"v({n_da2})"))]
        v_gs = [g - s for g, s in zip(sim.vec(f"v({n_dali_gate})"), sim.vec(f"v({n_gndd})"))]

        # Sample across the active window [0.4 ms .. 1.45 ms] (up to end of 1.3 ms collision break)
        active_da = [vd for t, vd in zip(t_vec, v_da) if 0.4e-3 <= t <= 1.45e-3]
        active_vgs = [vg for t, vg in zip(t_vec, v_gs) if 0.4e-3 <= t <= 1.45e-3]
        assert max(active_da) <= 4.5, f"TX low level {max(active_da):.2f} V > 4.5 V at bus_v={bus_v} V"
        assert min(active_vgs) >= 4.5, f"bootstrap drooped V_GS to {min(active_vgs):.2f} V < 4.5 V at bus_v={bus_v} V"
        assert max(v_gs) <= 13.2, f"12 V gate zener failed to clamp V_GS: {max(v_gs):.2f} V"

    # 6. TX hardware fault clamp: hard 16 V stiff supply across DA1-DA2 while TX active -> BC847B + 2R limits sink current
    deck_tx_fault = emit_system_netlist(
        sys_ext,
        jp1_closed=False,
        v_dali_tx_gpio="PULSE(0 3.3 50u 2u 2u 300u 1m)",
        extra_lines=[
            f"V_knx {n_knx_in} 0 DC 24.0",
            f"V_aux {n_aux_in} {sys_ext.gnda_node} DC 24.0",
            f"V_stiff {n_da1} da_stiff DC 16.0",
            f"R_stiff da_stiff {n_da2} 0.5",
            ".tran 5u 250u",
        ],
    )
    sim.run_deck(deck_tx_fault)
    t_flt = sim.vec("time")
    i_flt = [abs(i) for t, i in zip(t_flt, sim.vec("i(v_stiff)")) if 120e-6 <= t <= 240e-6]
    assert 0.260 <= max(i_flt) <= 0.420, f"TX fault clamp current {max(i_flt) * 1e3:.1f} mA outside 260..420 mA"


def test_ui_button_ladder_states_and_ws2812b_5v_level_shifter(sys_ext: StitchedSystem):
    """`.op` + `.tran` across mated main<->ui boards: verifies all 4 button ADC
    states (>= 280 mV separation) and 3.3 V -> 5.0 V WS2812B data level shifting."""
    sim = ngspice()
    n_knx_in = sys_ext.global_pad_node[("main", "J1", "1")]
    n_aux_in = sys_ext.global_pad_node[("main", "J2", "1")]
    n_btn = find_mpn_pad_node(sys_ext, "102010636", "3")
    n_d1_din = sys_ext.global_pad_node[("ui", "D1", "4")]

    levels: dict[str, float] = {}
    for label, sw1, sw2 in [
        ("none", False, False),
        ("reset", False, True),
        ("scroll", True, False),
        ("both", True, True),
    ]:
        deck = emit_system_netlist(
            sys_ext,
            sw1_closed=sw1,
            sw2_closed=sw2,
            extra_lines=[
                f"V_knx {n_knx_in} 0 DC 24.0",
                f"V_aux {n_aux_in} {sys_ext.gnda_node} DC 24.0",
                ".op",
            ],
        )
        sim.run_deck(deck)
        levels[label] = sim.vec(f"v({n_btn})")[0]

    assert levels["none"] == pytest.approx(3.30, abs=0.05)
    assert levels["reset"] == pytest.approx(2.20, abs=0.06)
    assert levels["scroll"] == pytest.approx(1.65, abs=0.06)
    assert levels["both"] == pytest.approx(1.32, abs=0.06)
    ordered = [levels["none"], levels["reset"], levels["scroll"], levels["both"]]
    assert all(hi - lo >= 0.28 for hi, lo in itertools.pairwise(ordered))

    # 3.3 V LED_DATA pulse through SN74AHCT1G125 + 330R produces full 5 V swing at D1.DIN
    deck_led = emit_system_netlist(
        sys_ext,
        v_led_gpio="PULSE(0 3.3 2u 20n 20n 5u 10u)",
        extra_lines=[
            f"V_knx {n_knx_in} 0 DC 24.0",
            f"V_aux {n_aux_in} {sys_ext.gnda_node} DC 24.0",
            ".tran 100n 10u",
        ],
    )
    sim.run_deck(deck_led)
    v_din = sim.vec(f"v({n_d1_din})")
    assert min(v_din) <= 0.15 and max(v_din) >= 4.75, (
        f"WS2812B 5 V level shifter swing {min(v_din):.2f}..{max(v_din):.2f} V insufficient"
    )
