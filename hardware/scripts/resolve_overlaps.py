"""Deterministic 2-stage Google OR-Tools CP-SAT component placer (`eda` container).

    python3 hardware/scripts/resolve_overlaps.py <board> [--write] [--all] [ref-or-block ...]
    python3 hardware/scripts/layout.py <board> autoplace [--write] [--all] [ref-or-block ...]

Two-stage integer CP-SAT model on a 0.05 mm grid (`ortools.sat.python.cp_model`):

1. Stage 1 (Anchors & Active/Large Parts): Fixes locked mechanical connectors,
   isolators and mounting/case keep-outs, then solves unlocked ICs, crystals,
   inductors, fuses, discrete semiconductors and large diodes simultaneously
   across 4 orthogonal orientations (0°/90°/180°/270°). Enforces `AddNoOverlap2D`
   separately on `F.Cu` and `B.Cu`, 3D height keep-outs (raised modules, OLED
   pocket, USB-C plug path, case screw tabs), enclosure slide/contact keep-outs
   (legs, pegs, standoffs, front posts, cover ribs + slide-relief corridors, UI
   snap hooks), cross-face `F<->B` keep-outs (THT drills, cutouts), full pad-box
   isolation-domain and edge clearances, and NCN5130 `U1` (`VSW1`/`XTAL`) hard
   proximity bounds. Minimizes weighted L1 pad-to-pad Manhattan wirelength.

2. Stage 2 (Batched Passives): Freezes Stage 1 footprints with routing-escape
   halos and solves 2-pin passives in deterministic priority batches (NCN5130
   high-priority caps `C1, C2, C3, C5, C6, C15` first with hard datasheet loop
   bounds, then crystal/buck/mux/DALI/UI batches) using boolean orientation
   variables (`OnlyEnforceIf`) anchored to the exact rotated pad coordinates of
   their parent ICs, down-weighting global ground/plane nets so decoupling
   capacitors hug their target IC pins. Uses zero manual coordinate bias in
   `--all` mode.
"""

from __future__ import annotations

import itertools
import math
import re
import sys
from dataclasses import dataclass

import pcbnew
from ortools.sat.python import cp_model
from shapely.geometry import Polygon
from shapely.geometry import box as sbox
from shapely.ops import unary_union

import layout

GRID_MM = 0.05
STAGE1_MAX_TIME_S = 35.0
STAGE2_MAX_TIME_S = 20.0
RANDOM_SEED = 42

FINE_HALO_MM = 0.40
STAGE1_U1_MOAT_MM = 0.50
STAGE1_IC_HALO_MM = 0.30
PASSIVE_HALO_MM = 0.18
U1_CAP_HALO_MM = 0.18
THT_CLEAR_MM = 0.22
PAD_DOMAIN_MARGIN_MM = 0.10

PLANE_NETS = {"GND", "GNDA", "GNDD"}
SUPPLY_RAILS = {"+3V3", "+3V3_1", "+3V3_TRX", "+5V", "+5V_BUCK", "+5V_AUX",
                "VFILT", "AUX_P", "DALI_ISO15V", "DA_P"}
CRITICAL_NETS = {"KNX_P_PROT", "CCP", "CAV", "CEQ1", "CEQ2", "VSW1", "VDD1M", "XTAL1", "XTAL2",
                 "SW", "BUCK5_SW", "+5V_XIAO", "CORE_PFET_G", "CORE_AUX_GOOD",
                 "DALI_GATE", "DALI_GATE_SW", "DALI_SENSE", "RX_LIM_IN", "TX0"}

U1_CRITICAL_CAPS = {"C1", "C2", "C3", "C5", "C6", "C15"}


def to_g(val_mm: float) -> int:
    return round(val_mm / GRID_MM)


def to_g_floor(val_mm: float) -> int:
    return math.floor(val_mm / GRID_MM + 1e-6)


def to_g_ceil(val_mm: float) -> int:
    return math.ceil(val_mm / GRID_MM - 1e-6)


def from_g(val_g: int) -> float:
    return round(val_g * GRID_MM, 2)


def board_mm_box(bb, grow: float = 0.0) -> tuple[float, float, float, float]:
    """Convert a pcbnew EDA_RECT (y down) to plan coordinates (x0, y0, x1, y1) with y up."""
    x0 = pcbnew.ToMM(bb.GetLeft()) - grow
    x1 = pcbnew.ToMM(bb.GetRight()) + grow
    y0 = -pcbnew.ToMM(bb.GetBottom()) - grow
    y1 = -pcbnew.ToMM(bb.GetTop()) + grow
    return (x0, y0, x1, y1)


@dataclass
class RotGeom:
    rot: float
    dx0: int
    dy0: int
    w: int
    h: int
    pads: dict[str, tuple[int, int]]
    pad_boxes: dict[str, tuple[int, int, int, int]]  # (dx0, dy0, dx1, dy1) relative to origin


@dataclass
class PartSpec:
    ref: str
    side: str
    domain: str | None
    block: str
    stage: int
    is_fine_pitch: bool
    height: float
    orig_x: int
    orig_y: int
    orig_rot: float
    rots: list[RotGeom]
    pad_nets: dict[str, str]
    has_tht: bool = False


def classify_stage(fp: pcbnew.FOOTPRINT, ref: str, is_main: bool = False) -> int:
    """Stage 1: ICs, crystals, inductors, fuses, transistors, bridges, large diodes/resistors,
    and on `main` the 6 NCN5130 perimeter caps so U1 and its mandatory caps pack together.
    Stage 2: remaining 2-pin passives and compact SOD diodes."""
    if is_main and ref in U1_CRITICAL_CAPS:
        return 1
    fpid = str(fp.GetFPID().GetLibItemName())
    pads = [p for p in fp.Pads() if p.GetNumber()]
    if ref.startswith(("U", "Q", "Y", "X", "L", "F", "J", "JP", "SW")):
        return 1
    if len(pads) != 2 or "Bridge" in fpid or "SMA" in fpid or "CP_" in fpid or "2512" in fpid:
        return 1
    return 2


def measure_rot_geom(fp: pcbnew.FOOTPRINT, rot_deg: float, halo_mm: float = 0.0) -> RotGeom:
    saved_pos = fp.GetPosition()
    saved_rot = fp.GetOrientationDegrees()
    fp.SetPosition(pcbnew.VECTOR2I(0, 0))
    fp.SetOrientationDegrees(rot_deg)
    x0, y0, x1, y1 = board_mm_box(layout.body_box(fp), halo_mm)
    dx0 = to_g_floor(x0)
    dy0 = to_g_floor(y0)
    dx1 = to_g_ceil(x1)
    dy1 = to_g_ceil(y1)
    pads: dict[str, tuple[int, int]] = {}
    pad_boxes: dict[str, tuple[int, int, int, int]] = {}
    by_num: dict[str, list[tuple[float, float]]] = {}
    bbox_by_num: dict[str, list[tuple[float, float, float, float]]] = {}
    for p in fp.Pads():
        num = p.GetNumber()
        if not num:
            continue
        by_num.setdefault(num, []).append((pcbnew.ToMM(p.GetPosition().x), -pcbnew.ToMM(p.GetPosition().y)))
        bbox_by_num.setdefault(num, []).append(board_mm_box(p.GetBoundingBox()))
    for num, pts in by_num.items():
        pads[num] = (to_g(sum(x for x, _ in pts) / len(pts)),
                     to_g(sum(y for _, y in pts) / len(pts)))
        pbs = bbox_by_num[num]
        pad_boxes[num] = (
            to_g_floor(min(b[0] for b in pbs)),
            to_g_floor(min(b[1] for b in pbs)),
            to_g_ceil(max(b[2] for b in pbs)),
            to_g_ceil(max(b[3] for b in pbs)),
        )
    fp.SetPosition(saved_pos)
    fp.SetOrientationDegrees(saved_rot)
    return RotGeom(
        rot=rot_deg % 360, dx0=dx0, dy0=dy0,
        w=max(1, dx1 - dx0), h=max(1, dy1 - dy0),
        pads=pads, pad_boxes=pad_boxes,
    )


def outline_valid_poly(frame: dict, inset_mm: float = 0.0) -> Polygon:
    w, h = frame["size"]["x"], frame["size"]["y"]
    pts = [tuple(p) for p in frame["outline"]] if "outline" in frame else [(0, 0), (w, 0), (w, h), (0, h)]
    poly = Polygon(pts)
    for slot in frame.get("slots", []):
        poly = poly.difference(sbox(slot["x0"], slot["y0"], slot["x1"], slot["y1"]))
    return poly.buffer(-inset_mm, join_style="mitre") if inset_mm > 0 else poly


def decompose_poly_boxes(geom) -> list[tuple[int, int, int, int]]:
    """Decompose a rectilinear Shapely polygon/multipolygon into maximal axis-aligned
    grid rectangles (gx0, gy0, gx1, gy1) so parts may freely cross internal seams."""
    if geom.is_empty:
        return []
    polys = [geom] if geom.geom_type == "Polygon" else [g for g in geom.geoms if g.geom_type == "Polygon"]
    boxes: list[tuple[int, int, int, int]] = []
    for poly in polys:
        if poly.is_empty:
            continue
        ys = sorted({round(y, 4) for _, y in poly.exterior.coords}
                    | {round(y, 4) for hole in poly.interiors for _, y in hole.coords})
        strips: list[tuple[float, float, float, float]] = []
        for y_lo, y_hi in itertools.pairwise(ys):
            if y_hi - y_lo < GRID_MM:
                continue
            slab = poly.intersection(sbox(poly.bounds[0] - 1, y_lo + 1e-4, poly.bounds[2] + 1, y_hi - 1e-4))
            sub = [slab] if slab.geom_type == "Polygon" else [g for g in slab.geoms if g.geom_type == "Polygon"]
            for sp in sub:
                xs0, _, xs1, _ = sp.bounds
                if sbox(xs0 + 1e-3, y_lo + 1e-3, xs1 - 1e-3, y_hi - 1e-3).within(poly.buffer(2e-3)):
                    strips.append((xs0, y_lo, xs1, y_hi))
        merged = list(strips)
        for i in range(len(strips)):
            for j in range(i + 1, len(strips)):
                y0 = strips[i][1]
                y1 = strips[j][3]
                if y1 <= y0:
                    continue
                x0 = max(strips[i][0], strips[j][0])
                x1 = min(strips[i][2], strips[j][2])
                if x1 - x0 >= 1.0 and sbox(x0 + 1e-3, y0 + 1e-3, x1 - 1e-3, y1 - 1e-3).within(poly.buffer(2e-3)):
                    merged.append((x0, y0, x1, y1))
        for rx0, ry0, rx1, ry1 in merged:
            gx0, gy0 = to_g_ceil(rx0), to_g_ceil(ry0)
            gx1, gy1 = to_g_floor(rx1), to_g_floor(ry1)
            if gx1 > gx0 and gy1 > gy0:
                b = (gx0, gy0, gx1, gy1)
                if b not in boxes:
                    boxes.append(b)
    pruned = [b for i, b in enumerate(boxes)
              if not any(j != i and o[0] <= b[0] and b[2] <= o[2] and o[1] <= b[1] and b[3] <= o[3]
                         for j, o in enumerate(boxes))]
    return pruned or boxes


def decompose_domain_pad_boxes(
    frame: dict,
    domain: str | None,
    keepout_margin_mm: float = PAD_DOMAIN_MARGIN_MM,
) -> list[tuple[int, int, int, int]]:
    """Allowed axis-aligned grid boxes for PAD bounding boxes of `domain`:
    strictly inside `outline` eroded by `edge_clearance`, inside `domain_regions[domain]`,
    and outside all `copper_keepout` zones by `keepout_margin_mm`."""
    edge_clr = frame.get("edge_clearance", 0.3) + max(0.05, keepout_margin_mm - 0.10)
    valid_pad_poly = outline_valid_poly(frame, inset_mm=edge_clr)
    for k in frame.get("copper_keepout", []):
        valid_pad_poly = valid_pad_poly.difference(
            sbox(k["x0"] - keepout_margin_mm, k["y0"] - keepout_margin_mm,
                 k["x1"] + keepout_margin_mm, k["y1"] + keepout_margin_mm)
        )
    regions = frame.get("domain_regions", {})
    if domain and domain in regions:
        raw = regions[domain]
        rects = raw if isinstance(raw, list) else [raw]
        dom_poly = unary_union([sbox(r["x0"], r["y0"], r["x1"], r["y1"]) for r in rects]).buffer(-0.05, join_style="mitre")
        return decompose_poly_boxes(dom_poly.intersection(valid_pad_poly))
    return decompose_poly_boxes(valid_pad_poly)


def infer_net_domains(board: pcbnew.BOARD, plan: dict) -> dict[str, str]:
    """Infer every net's isolation domain purely from locked anchors and ground
    names, without reading any unlocked component's coordinates."""
    frame = plan["frame"]
    domains = frame.get("domains", [])
    if len(domains) == 1:
        only = domains[0]
        return {str(n): only for n in board.GetNetsByName() if str(n)}

    net_dom: dict[str, str] = {
        "GND": "SELV-KNX", "+3V3": "SELV-KNX", "+3V3_TRX": "SELV-KNX", "+5V": "SELV-KNX",
        "GNDA": "SELV-AUX", "AUX_IN": "SELV-AUX", "AUX_FUSED": "SELV-AUX", "AUX_P": "SELV-AUX",
        "GNDD": "DALI", "DA1": "DALI", "DA2": "DALI", "DA_P": "DALI", "DALI_ISO15V": "DALI",
    }
    regions = frame.get("domain_regions", {})
    locked_refs = {r for r, p in plan["parts"].items() if p.get("locked")}
    for ref in sorted(locked_refs):
        fp = board.FindFootprintByReference(ref)
        if fp is None:
            continue
        for p in fp.Pads():
            net = p.GetNetname()
            if not net or net in net_dom:
                continue
            dom = layout.domain_at(regions, pcbnew.ToMM(p.GetPosition().x), -pcbnew.ToMM(p.GetPosition().y))
            if dom:
                net_dom[net] = dom

    # Propagate across non-isolator components
    isolators = set(frame.get("isolators", []))
    changed = True
    while changed:
        changed = False
        for fp in board.Footprints():
            if fp.GetReference() in isolators:
                continue
            p_nets = {p.GetNetname() for p in fp.Pads() if p.GetNetname()}
            known = {net_dom[n] for n in p_nets if n in net_dom}
            if len(known) == 1:
                dom = min(known)
                for n in p_nets:
                    if n not in net_dom:
                        net_dom[n] = dom
                        changed = True
    return net_dom


def static_keepouts(board: pcbnew.BOARD, plan: dict, board_name: str) -> dict[str, list[tuple[int, int, int, int]]]:
    """Fixed mechanical/enclosure keep-outs per side ('front', 'back', 'tall_front')."""
    out: dict[str, list[tuple[int, int, int, int]]] = {"front": [], "back": [], "tall_front": []}
    frame = plan["frame"]
    w, h = frame["size"]["x"], frame["size"]["y"]

    def add_box(target: str, x0: float, y0: float, x1: float, y1: float) -> None:
        gx0, gy0 = to_g_floor(x0), to_g_floor(y0)
        gx1, gy1 = to_g_ceil(x1), to_g_ceil(y1)
        if gx1 > gx0 and gy1 > gy0:
            out[target].append((gx0, gy0, gx1 - gx0, gy1 - gy0))

    # Case contact rule areas (legs, pegs, standoffs)
    for zone in layout.zones(board):
        if not zone.GetIsRuleArea() or not zone.GetDoNotAllowFootprints():
            continue
        x0, y0, x1, y1 = board_mm_box(zone.GetBoundingBox(), 0.1)
        if zone.IsOnLayer(pcbnew.F_Cu):
            add_box("front", x0, y0, x1, y1)
        if zone.IsOnLayer(pcbnew.B_Cu):
            add_box("back", x0, y0, x1, y1)

    case = plan.get("case", {})
    for post in case.get("front_posts", []):
        add_box("front", post["x"] - 1.15, post["y"] - 1.15, post["x"] + 1.15, post["y"] + 1.15)

    ribs = case.get("ribs", [])
    for rib in ribs:
        # Cover slides along -enc_z (+board_x): a back-side part with x > rib.x cuts a relief
        # channel from board_x=0 to part.x1+2.0 across [part.y0-2.0 .. part.y1+2.0], which would
        # erase any rib at smaller board_x in that y-band! Block [rib.x-1.7 .. w] across [rib.y-2.1 .. rib.y+2.1]
        # for the corner backing ribs, and [rib.x-1.8 .. rib.x+1.8] x [rib.y-0.7 .. rib.y+0.7] locally.
        add_box("back", rib["x"] - 1.8, rib["y"] - 0.7, rib["x"] + 1.8, rib["y"] + 0.7)
        if rib["y"] < 2.0:
            add_box("back", -12.0, -1.0, w + 1.0, rib["y"] + 2.15)
        elif rib["y"] > h - 2.0:
            add_box("back", -12.0, rib["y"] - 2.15, w + 1.0, h + 1.0)
        else:
            # Mid-board ribs at x ~ 12..32: forbid back parts behind/right of rib (larger board x)
            # within +-2.1 mm in y so back_part_reliefs never cuts away the rib contact!
            add_box("back", max(0.0, rib["x"] - 2.1), rib["y"] - 2.15, w + 1.0, rib["y"] + 2.15)

    # Enclosure wall margins (`test_part_bodies_stay_inside_the_case_walls`)
    if board_name == "main" and layout.SPEC["boards"]["main"].get("plane") == "perpendicular-to-rail":
        # Vertical main board (v3): y_offset=1.5 inside 81 mm case (wall 1.6 -> board y in [0.1, 77.9])
        add_box("front", -12.0, -2.0, w + 1.0, 0.25)
        add_box("front", -12.0, h - 0.25, w + 1.0, h + 2.0)
        add_box("back", -12.0, -2.0, w + 1.0, 0.25)
        add_box("back", -12.0, h - 0.25, w + 1.0, h + 2.0)
        # USB-C plug clearance path from XIAO U8 USB end to rail-side board edge (`test_usb_access.py`)
        u8 = board.FindFootprintByReference("U8")
        if u8 is not None:
            pads = {p.GetNumber(): p.GetPosition() for p in u8.Pads()}
            if "1" in pads and "14" in pads:
                usb_x = pcbnew.ToMM((pads["1"].x + pads["14"].x) // 2)
                usb_y = -pcbnew.ToMM((pads["1"].y + pads["14"].y) // 2)
                add_box("tall_front", min(usb_x, w) - 0.5, usb_y - 6.4, w + 1.0, usb_y + 6.4)

    if board_name == "ui":
        # Keep back-side UI parts clear of side-wall snap hooks (`SNAP["overlap"] = 0.5 mm` + margin)
        add_box("back", -1.0, -1.0, 0.85, h + 1.0)
        add_box("back", w - 0.85, -1.0, w + 1.0, h + 1.0)
        add_box("back", -1.0, -1.0, w + 1.0, 0.45)
        add_box("back", -1.0, h - 0.45, w + 1.0, h + 1.0)

    if "outline" in plan.get("oled_module", {}):
        r = plan["oled_module"]["outline"]
        add_box("tall_front", r["x"], r["y"], r["x"] + r["w"], r["y"] + r["h"])
    return out


def anchor_obstacles(
    fp: pcbnew.FOOTPRINT,
    halo_mm: float,
    raised: set[str],
) -> dict[str, list[tuple[int, int, int, int]]]:
    """Fixed boxes contributed by a frozen footprint on F.Cu, B.Cu and 3D tall_front."""
    obs: dict[str, list[tuple[int, int, int, int]]] = {"front": [], "back": [], "tall_front": []}
    ref = fp.GetReference()
    own = "back" if fp.IsFlipped() else "front"
    other = "front" if own == "back" else "back"

    def rect_tuple(bb, grow: float = 0.0) -> tuple[int, int, int, int]:
        x0, y0, x1, y1 = board_mm_box(bb, grow)
        gx0, gy0 = to_g_floor(x0), to_g_floor(y0)
        gx1, gy1 = to_g_ceil(x1), to_g_ceil(y1)
        return gx0, gy0, max(1, gx1 - gx0), max(1, gy1 - gy0)

    if ref in raised:
        obs["tall_front"].append(rect_tuple(layout.body_box(fp), 0.0))
        for pad in fp.Pads():
            if pad.GetAttribute() == pcbnew.PAD_ATTRIB_PTH:
                box = rect_tuple(pad.GetBoundingBox(), THT_CLEAR_MM)
                obs["front"].append(box)
                obs["back"].append(box)
        return obs

    obs[own].append(rect_tuple(layout.body_box(fp), halo_mm))
    fpid = str(fp.GetFPID().GetLibItemName())
    if "QFN" in fpid or "FPC" in fpid:
        # Cross-face via-escape shadow: keep unlocked opposite-side parts off dense 0.5 mm-pitch escape fields
        obs[other].append(rect_tuple(layout.body_box(fp), 0.55))
    for pad in fp.Pads():
        if pad.GetAttribute() == pcbnew.PAD_ATTRIB_PTH or pad.GetDrillSizeX() > 0:
            obs[other].append(rect_tuple(pad.GetBoundingBox(), THT_CLEAR_MM))
    for g in fp.GraphicalItems():
        if g.GetLayer() == pcbnew.Edge_Cuts:
            obs[other].append(rect_tuple(g.GetBoundingBox(), THT_CLEAR_MM))
    return obs


def net_weight(net_name: str, fanout: int, stage: int) -> int:
    if not net_name:
        return 0
    if net_name in PLANE_NETS:
        return 1
    if net_name in CRITICAL_NETS:
        return 30
    if net_name in SUPPLY_RAILS:
        return 6 if stage == 2 else 4
    if fanout > 8:
        return 4
    return 15 if stage == 2 else 10


def build_part_spec(
    board: pcbnew.BOARD,
    plan: dict,
    ref: str,
    net_domains: dict[str, str],
    is_main: bool = False,
) -> PartSpec:
    fp = board.FindFootprintByReference(ref)
    pinfo = plan["parts"][ref]
    side = "back" if pinfo.get("side") == "back" else "front"
    layout.set_side(fp, side == "back")
    fpid = str(fp.GetFPID().GetLibItemName())
    height = layout.part_height(plan.get("heights", {}), ref, fpid)
    fine_set = set(plan["frame"].get("fine_pitch_power_pads", []))
    is_fine = ref in fine_set or "QFN" in fpid or "FPC" in fpid or "SOT-363" in fpid
    stage = classify_stage(fp, ref, is_main=is_main)
    has_tht = any(p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH or p.GetDrillSizeX() > 0 for p in fp.Pads())

    pad_nets = {p.GetNumber(): p.GetNetname() for p in fp.Pads() if p.GetNumber() and p.GetNetname()}
    doms = {net_domains[n] for n in pad_nets.values() if n in net_domains}
    domain = min(doms) if len(doms) == 1 else None

    rot0 = float(pinfo.get("rot", 0)) % 360
    if ref == "U1":
        halo = STAGE1_U1_MOAT_MM
    elif is_main and ref == "C1":
        halo = 0.10
    elif is_main and ref in U1_CRITICAL_CAPS:
        halo = U1_CAP_HALO_MM
    elif stage == 2:
        halo = PASSIVE_HALO_MM
    elif ref.startswith(("U", "Q", "Y")) and "TO-252" not in fpid:
        halo = STAGE1_IC_HALO_MM
    else:
        halo = 0.0

    cand_rots = [0.0, 90.0, 180.0, 270.0]
    rots = [measure_rot_geom(fp, r, halo_mm=halo) for r in cand_rots]
    ox = to_g(float(pinfo.get("x", 0.0)))
    oy = to_g(float(pinfo.get("y", 0.0)))
    return PartSpec(
        ref=ref, side=side, domain=domain, block=pinfo.get("block", "misc"),
        stage=stage, is_fine_pitch=is_fine, height=height,
        orig_x=ox, orig_y=oy, orig_rot=rot0, rots=rots,
        pad_nets=pad_nets, has_tht=has_tht,
    )


def add_rect_avoidance(
    model: cp_model.CpModel,
    x_start: cp_model.IntVar,
    y_start: cp_model.IntVar,
    x_end: cp_model.IntVar,
    y_end: cp_model.IntVar,
    obstacles: list[tuple[int, int, int, int]],
    prefix: str,
) -> None:
    for idx, (rx, ry, rw, rh) in enumerate(obstacles):
        l_left = model.NewBoolVar(f"{prefix}_l_{idx}")
        l_right = model.NewBoolVar(f"{prefix}_r_{idx}")
        l_below = model.NewBoolVar(f"{prefix}_b_{idx}")
        l_above = model.NewBoolVar(f"{prefix}_a_{idx}")
        model.Add(x_end <= rx).OnlyEnforceIf(l_left)
        model.Add(x_start >= rx + rw).OnlyEnforceIf(l_right)
        model.Add(y_end <= ry).OnlyEnforceIf(l_below)
        model.Add(y_start >= ry + rh).OnlyEnforceIf(l_above)
        model.AddBoolOr([l_left, l_right, l_below, l_above])


def add_in_any_box(
    model: cp_model.CpModel,
    x_lo: cp_model.IntVar,
    y_lo: cp_model.IntVar,
    x_hi: cp_model.IntVar,
    y_hi: cp_model.IntVar,
    boxes: list[tuple[int, int, int, int]],
    prefix: str,
) -> None:
    if len(boxes) == 1:
        gx0, gy0, gx1, gy1 = boxes[0]
        model.Add(x_lo >= gx0)
        model.Add(y_lo >= gy0)
        model.Add(x_hi <= gx1)
        model.Add(y_hi <= gy1)
        return
    lits = []
    for idx, (gx0, gy0, gx1, gy1) in enumerate(boxes):
        bl = model.NewBoolVar(f"{prefix}_{idx}")
        model.Add(x_lo >= gx0).OnlyEnforceIf(bl)
        model.Add(y_lo >= gy0).OnlyEnforceIf(bl)
        model.Add(x_hi <= gx1).OnlyEnforceIf(bl)
        model.Add(y_hi <= gy1).OnlyEnforceIf(bl)
        lits.append(bl)
    model.AddBoolOr(lits)


def solve_batch(
    batch: list[PartSpec],
    fixed_obs: dict[str, list[tuple[int, int, int, int]]],
    fixed_pads: dict[str, list[tuple[int, int]]],
    fixed_ref_pads: dict[tuple[str, str], tuple[int, int]],
    outline_boxes: list[tuple[int, int, int, int]],
    domain_pad_boxes: dict[str | None, list[tuple[int, int, int, int]]],
    net_fanout: dict[str, int],
    board_bounds: tuple[int, int, int, int],
    max_time_s: float,
    anchor_nudge_weight: int = 0,
    enforce_u1_rules: bool = True,
) -> dict[str, tuple[int, int, float]] | None:
    """Solve a Stage 1 or Stage 2 batch simultaneously with CP-SAT AddNoOverlap2D."""
    if not batch:
        return {}
    model = cp_model.CpModel()
    bx0, by0, bx1, by1 = board_bounds

    mov_intervals: dict[str, tuple[list, list]] = {
        "front": ([], []),
        "back": ([], []),
        "tall_front": ([], []),
    }

    part_vars: dict[str, tuple[cp_model.IntVar, cp_model.IntVar, list[cp_model.IntVar]]] = {}
    pad_vars_by_ref: dict[tuple[str, str], tuple[cp_model.IntVar, cp_model.IntVar]] = {}
    pad_vars_by_net: dict[str, list[tuple[str, cp_model.IntVar, cp_model.IntVar]]] = {}
    obj_terms: list = []

    for ps_idx, ps in enumerate(batch):
        dboxes = (domain_pad_boxes.get(f"{ps.domain}_corridor") if ps.ref in {"C1", "C2", "C3"} else None) \
            or domain_pad_boxes.get(ps.domain) or domain_pad_boxes[None]
        vx0 = min(b[0] for b in outline_boxes) - 40
        vy0 = min(b[1] for b in outline_boxes) - 40
        vx1 = max(b[2] for b in outline_boxes) + 40
        vy1 = max(b[3] for b in outline_boxes) + 40
        x = model.NewIntVar(max(bx0 - 40, vx0), min(bx1 + 40, vx1), f"x_{ps.ref}")
        y = model.NewIntVar(max(by0 - 40, vy0), min(by1 + 40, vy1), f"y_{ps.ref}")

        rot_lits = [model.NewBoolVar(f"rot_{ps.ref}_{idx}") for idx in range(len(ps.rots))]
        model.AddExactlyOne(rot_lits)

        max_w = max(rg.w for rg in ps.rots)
        max_h = max(rg.h for rg in ps.rots)
        x_start = model.NewIntVar(bx0 - max_w, bx1 + max_w, f"xs_{ps.ref}")
        y_start = model.NewIntVar(by0 - max_h, by1 + max_h, f"ys_{ps.ref}")
        w_var = model.NewIntVar(1, max_w, f"w_{ps.ref}")
        h_var = model.NewIntVar(1, max_h, f"h_{ps.ref}")
        x_end = model.NewIntVar(bx0 - max_w, bx1 + 2 * max_w, f"xe_{ps.ref}")
        y_end = model.NewIntVar(by0 - max_h, by1 + 2 * max_h, f"ye_{ps.ref}")

        for lit, rg in zip(rot_lits, ps.rots):
            model.Add(x_start == x + rg.dx0).OnlyEnforceIf(lit)
            model.Add(y_start == y + rg.dy0).OnlyEnforceIf(lit)
            model.Add(w_var == rg.w).OnlyEnforceIf(lit)
            model.Add(h_var == rg.h).OnlyEnforceIf(lit)
        model.Add(x_end == x_start + w_var)
        model.Add(y_end == y_start + h_var)

        ix = model.NewIntervalVar(x_start, w_var, x_end, f"ix_{ps.ref}")
        iy = model.NewIntervalVar(y_start, h_var, y_end, f"iy_{ps.ref}")
        mov_intervals[ps.side][0].append(ix)
        mov_intervals[ps.side][1].append(iy)
        add_rect_avoidance(model, x_start, y_start, x_end, y_end, fixed_obs[ps.side], f"obs_{ps.ref}")

        if ps.has_tht or ps.ref == "U1":
            other_side = "front" if ps.side == "back" else "back"
            mov_intervals[other_side][0].append(ix)
            mov_intervals[other_side][1].append(iy)
            if ps.has_tht:
                add_rect_avoidance(model, x_start, y_start, x_end, y_end, fixed_obs[other_side], f"tht_{ps.ref}")

        if ps.side == "front" and ps.height > 4.8:
            mov_intervals["tall_front"][0].append(ix)
            mov_intervals["tall_front"][1].append(iy)
            add_rect_avoidance(model, x_start, y_start, x_end, y_end, fixed_obs["tall_front"], f"tall_{ps.ref}")

        # Keep multi-pin SELV-KNX ICs/transistors out of the narrow tongue neck (y > 37.5) on vertical main
        if (
            ps.domain == "SELV-KNX"
            and ps.stage == 1
            and ps.ref.startswith(("U", "Q"))
            and max(b[3] for b in outline_boxes) > 1500
            and any(b[0] < 0 for b in outline_boxes)
        ):
            model.Add(y_end <= to_g_floor(37.2))
        # Courtyard inside board outline (minus cutouts)
        add_in_any_box(model, x_start, y_start, x_end, y_end, outline_boxes, f"out_{ps.ref}")

        # Full pad copper bounding boxes inside domain/edge/keep-out legal boxes!
        # For dense multi-pin ICs (>8 pins, e.g. QFN-40), constrain the overall pad-field envelope
        # plus individual pad center coordinates to keep model compact and fast.
        if len(ps.pad_nets) > 8:
            pf_x0 = model.NewIntVar(bx0 - 60, bx1 + 60, f"pfx0_{ps.ref}")
            pf_y0 = model.NewIntVar(by0 - 60, by1 + 60, f"pfy0_{ps.ref}")
            pf_x1 = model.NewIntVar(bx0 - 60, bx1 + 60, f"pfx1_{ps.ref}")
            pf_y1 = model.NewIntVar(by0 - 60, by1 + 60, f"pfy1_{ps.ref}")
            for lit, rg in zip(rot_lits, ps.rots):
                mx0 = min(b[0] for b in rg.pad_boxes.values())
                my0 = min(b[1] for b in rg.pad_boxes.values())
                mx1 = max(b[2] for b in rg.pad_boxes.values())
                my1 = max(b[3] for b in rg.pad_boxes.values())
                model.Add(pf_x0 == x + mx0).OnlyEnforceIf(lit)
                model.Add(pf_y0 == y + my0).OnlyEnforceIf(lit)
                model.Add(pf_x1 == x + mx1).OnlyEnforceIf(lit)
                model.Add(pf_y1 == y + my1).OnlyEnforceIf(lit)
            add_in_any_box(model, pf_x0, pf_y0, pf_x1, pf_y1, dboxes, f"pfdom_{ps.ref}")

        for num, net in ps.pad_nets.items():
            max_off = max(max(abs(rg.pads[num][0]), abs(rg.pads[num][1])) for rg in ps.rots if num in rg.pads)
            px = model.NewIntVar(bx0 - max_off - 40, bx1 + max_off + 40, f"px_{ps.ref}_{num}")
            py = model.NewIntVar(by0 - max_off - 40, by1 + max_off + 40, f"py_{ps.ref}_{num}")
            for lit, rg in zip(rot_lits, ps.rots):
                ox_p, oy_p = rg.pads[num]
                model.Add(px == x + ox_p).OnlyEnforceIf(lit)
                model.Add(py == y + oy_p).OnlyEnforceIf(lit)
            pad_vars_by_ref[(ps.ref, num)] = (px, py)

            if len(ps.pad_nets) <= 8:
                pb_x0 = model.NewIntVar(bx0 - max_off - 50, bx1 + max_off + 50, f"pbx0_{ps.ref}_{num}")
                pb_y0 = model.NewIntVar(by0 - max_off - 50, by1 + max_off + 50, f"pby0_{ps.ref}_{num}")
                pb_x1 = model.NewIntVar(bx0 - max_off - 50, bx1 + max_off + 50, f"pbx1_{ps.ref}_{num}")
                pb_y1 = model.NewIntVar(by0 - max_off - 50, by1 + max_off + 50, f"pby1_{ps.ref}_{num}")
                for lit, rg in zip(rot_lits, ps.rots):
                    b0, b1, b2, b3 = rg.pad_boxes[num]
                    model.Add(pb_x0 == x + b0).OnlyEnforceIf(lit)
                    model.Add(pb_y0 == y + b1).OnlyEnforceIf(lit)
                    model.Add(pb_x1 == x + b2).OnlyEnforceIf(lit)
                    model.Add(pb_y1 == y + b3).OnlyEnforceIf(lit)
                add_in_any_box(model, pb_x0, pb_y0, pb_x1, pb_y1, dboxes, f"pdom_{ps.ref}_{num}")

            if net_weight(net, net_fanout.get(net, 2), ps.stage) > 0:
                pad_vars_by_net.setdefault(net, []).append((ps.ref, px, py))

        if anchor_nudge_weight > 0:
            dx_orig = model.NewIntVar(0, (bx1 - bx0) + 100, f"dxo_{ps.ref}")
            dy_orig = model.NewIntVar(0, (by1 - by0) + 100, f"dyo_{ps.ref}")
            model.AddAbsEquality(dx_orig, x - ps.orig_x)
            model.AddAbsEquality(dy_orig, y - ps.orig_y)
            obj_terms.append(anchor_nudge_weight * (dx_orig + dy_orig))
        else:
            # Pure zero-seed deterministic symmetry-breaker across identical parts (no orig_x/orig_y used!)
            obj_terms.append(ps_idx * 0 + (1 - rot_lits[0]))

        part_vars[ps.ref] = (x, y, rot_lits)

    for xs, ys in mov_intervals.values():
        if len(xs) > 1:
            model.AddNoOverlap2D(xs, ys)

    # Hard NCN5130 (`U1`) datasheet layout proximity constraints (`test_u1_routing.py`)
    batch_refs = {ps.ref for ps in batch}

    def get_pad_xy(ref_num: tuple[str, str]):
        if ref_num in pad_vars_by_ref:
            return pad_vars_by_ref[ref_num]
        return fixed_ref_pads.get(ref_num)

    def bind_linf(a_ref_num: tuple[str, str], b_ref_num: tuple[str, str], limit_mm: float) -> None:
        if not ({a_ref_num[0], b_ref_num[0]} & batch_refs):
            return
        a_xy, b_xy = get_pad_xy(a_ref_num), get_pad_xy(b_ref_num)
        if a_xy is None or b_xy is None:
            return
        lim_g = to_g_floor(limit_mm)
        model.Add(a_xy[0] - b_xy[0] <= lim_g)
        model.Add(b_xy[0] - a_xy[0] <= lim_g)
        model.Add(a_xy[1] - b_xy[1] <= lim_g)
        model.Add(b_xy[1] - a_xy[1] <= lim_g)

    def bind_l1(a_ref_num: tuple[str, str], b_ref_num: tuple[str, str], limit_mm: float, tag: str) -> None:
        if not ({a_ref_num[0], b_ref_num[0]} & batch_refs):
            return
        a_xy, b_xy = get_pad_xy(a_ref_num), get_pad_xy(b_ref_num)
        if a_xy is None or b_xy is None:
            return
        lim_g = to_g_floor(limit_mm)
        dxv = model.NewIntVar(0, lim_g, f"u1dx_{tag}")
        dyv = model.NewIntVar(0, lim_g, f"u1dy_{tag}")
        model.AddAbsEquality(dxv, a_xy[0] - b_xy[0])
        model.AddAbsEquality(dyv, a_xy[1] - b_xy[1])
        model.Add(dxv + dyv <= lim_g)

    if enforce_u1_rules:
        is_v3_u1 = any(b[0] < 0 for b in outline_boxes)
        bind_l1(("U1", "17"), ("L1", "1"), 9.8 if is_v3_u1 else 7.5, "vsw1")
        # Keep U1 non-GND signal pins clear of large opposite-side SMD pads (e.g. C4, L1 on F.Cu)
        # so every signal pin can drop a through-hole escape via without hitting opposite-side SMD copper!
        if "U1" in batch_refs and fixed_obs.get("opp_large_smd"):
            u1_ps = next(ps for ps in batch if ps.ref == "U1")
            for pnum, pnet in u1_ps.pad_nets.items():
                if pnet in PLANE_NETS or pnum == "41":
                    continue
                px, py = pad_vars_by_ref[("U1", pnum)]
                add_rect_avoidance(model, px, py, px, py,
                                   fixed_obs["opp_large_smd"], f"u1via_{pnum}")
        bind_l1(("U1", "35"), ("Y1", "1"), 8.5, "xtal1")
        bind_l1(("U1", "34"), ("Y1", "2"), 8.5, "xtal2")
        # Octagon inner approximation of Euclidean ball r=3.0 mm:
        # max(|dx|, |dy|) <= 2.75 mm and |dx| + |dy| <= 3.70 mm => hypot(dx, dy) <= 2.91 mm < 3.00 mm!
        for cap_r, u1_p in (("C5", "40"), ("C6", "30"), ("C15", "16")):
            bind_linf((cap_r, "1"), ("U1", u1_p), 2.75)
            bind_l1((cap_r, "1"), ("U1", u1_p), 3.70, f"dec_oct_{cap_r}")
        ref_ccp = ref_cav = ref_ceq = None
        ccp_pads = ceq_pads = ("1", "2")
        for ps in batch:
            if not ps.ref.startswith("C") or set(ps.pad_nets) != {"1", "2"}:
                continue
            n1, n2 = ps.pad_nets["1"], ps.pad_nets["2"]
            if {n1, n2} == {"KNX_P_PROT", "CCP"}:
                ref_ccp = ps.ref
                p_vbus = "1" if n1 == "KNX_P_PROT" else "2"
                p_ccp = "2" if p_vbus == "1" else "1"
                ccp_pads = (p_vbus, p_ccp)
                ccp_lim = 2.80 if max(ps.rots[0].w, ps.rots[0].h) < 62 else 3.4
                bind_l1((ps.ref, p_vbus), ("U1", "6"), ccp_lim, f"ccp_vbus_{ps.ref}")
                bind_l1((ps.ref, p_ccp), ("U1", "4"), ccp_lim, f"ccp_pin_{ps.ref}")
            elif "CAV" in (n1, n2):
                ref_cav = ps.ref
                p_cav = "1" if n1 == "CAV" else "2"
                bind_l1((ps.ref, p_cav), ("U1", "5"), 7.2, f"cav_{ps.ref}")
            elif {n1, n2} == {"CEQ1", "CEQ2"}:
                ref_ceq = ps.ref
                p_ceq1 = "1" if n1 == "CEQ1" else "2"
                p_ceq2 = "2" if p_ceq1 == "1" else "1"
                ceq_pads = (p_ceq1, p_ceq2)
                bind_l1((ps.ref, p_ceq1), ("U1", "7"), 7.2, f"ceq1_{ps.ref}")
                bind_l1((ps.ref, p_ceq2), ("U1", "8"), 7.2, f"ceq2_{ps.ref}")

        # Orient U1 so its bus side (pin 6 VBUS1) faces the KNX bus input connector J1
        if "U1" in part_vars and ("J1", "1") in fixed_ref_pads:
            _ux, uy, _ = part_vars["U1"]
            _p6x, p6y = pad_vars_by_ref[("U1", "6")]
            _j1x, j1y = fixed_ref_pads[("J1", "1")]
            if j1y < to_g(12.0):
                model.Add(p6y < uy)

        # Align C1 (VBUS1=6, CCP=4) and C3 (CEQ1=7, CEQ2=8) pad vectors parallel & uncrossed
        # with U1's pin row so CCP never crosses VBUS1 and CEQ1 never crosses CEQ2!
        if "U1" in part_vars and ref_ccp in part_vars and ref_cav in part_vars and ref_ceq in part_vars:
            u1_spec = next(ps for ps in batch if ps.ref == "U1")
            _, _, u1_lits = part_vars["U1"]
            pvbus_xy = pad_vars_by_ref[(ref_ccp, ccp_pads[0])]
            pccp_xy = pad_vars_by_ref[(ref_ccp, ccp_pads[1])]
            pceq1_xy = pad_vars_by_ref[(ref_ceq, ceq_pads[0])]
            pceq2_xy = pad_vars_by_ref[(ref_ceq, ceq_pads[1])]
            x_ccp, y_ccp, _ = part_vars[ref_ccp]
            x_cav, _y_cav, _ = part_vars[ref_cav]
            _x_ceq, y_ceq, _ = part_vars[ref_ceq]
            for lit, rg in zip(u1_lits, u1_spec.rots):
                dx46 = rg.pads["4"][0] - rg.pads["6"][0]
                dy46 = rg.pads["4"][1] - rg.pads["6"][1]
                if abs(dx46) > abs(dy46):
                    if dx46 > 0:
                        model.Add(pccp_xy[0] >= pvbus_xy[0]).OnlyEnforceIf(lit)
                        model.Add(pceq1_xy[0] >= pceq2_xy[0]).OnlyEnforceIf(lit)
                        model.Add(y_ceq <= y_ccp - to_g(1.6)).OnlyEnforceIf(lit)
                        model.Add(x_cav <= x_ccp - to_g(2.2)).OnlyEnforceIf(lit)
                    else:
                        model.Add(pccp_xy[0] <= pvbus_xy[0]).OnlyEnforceIf(lit)
                        model.Add(pceq1_xy[0] <= pceq2_xy[0]).OnlyEnforceIf(lit)
                        model.Add(y_ceq >= y_ccp + to_g(1.6)).OnlyEnforceIf(lit)
                        model.Add(x_cav >= x_ccp + to_g(2.2)).OnlyEnforceIf(lit)
                else:
                    if dy46 > 0:
                        model.Add(pccp_xy[1] >= pvbus_xy[1]).OnlyEnforceIf(lit)
                        model.Add(pceq1_xy[1] >= pceq2_xy[1]).OnlyEnforceIf(lit)
                        model.Add(y_ceq <= y_ccp - to_g(1.6)).OnlyEnforceIf(lit)
                    else:
                        model.Add(pccp_xy[1] <= pvbus_xy[1]).OnlyEnforceIf(lit)
                        model.Add(pceq1_xy[1] <= pceq2_xy[1]).OnlyEnforceIf(lit)
                        model.Add(y_ceq >= y_ccp + to_g(1.6)).OnlyEnforceIf(lit)

    span_x = (bx1 - bx0) + 200
    span_y = (by1 - by0) + 200
    stage_num = batch[0].stage

    for net in sorted(pad_vars_by_net):
        var_items = pad_vars_by_net[net]
        w = net_weight(net, net_fanout.get(net, 2), stage_num)
        if w <= 0:
            continue
        f_pts = sorted(set(fixed_pads.get(net, [])))
        if net in PLANE_NETS:
            if f_pts:
                cx = round(sum(p[0] for p in f_pts) / len(f_pts))
                cy = round(sum(p[1] for p in f_pts) / len(f_pts))
                for ref_n, px, py in var_items:
                    adx = model.NewIntVar(0, span_x, f"gdx_{net}_{ref_n}_{px.Name()}")
                    ady = model.NewIntVar(0, span_y, f"gdy_{net}_{ref_n}_{py.Name()}")
                    model.AddAbsEquality(adx, px - cx)
                    model.AddAbsEquality(ady, py - cy)
                    obj_terms.append(w * (adx + ady))
            continue

        total_pts = len(var_items) + len(f_pts)
        if total_pts < 2:
            continue
        if (stage_num == 2 or len(var_items) == 1) and f_pts:
            # Deterministic 1-to-1 / round-robin assignment across fixed IC pins on `net`
            # (zero use of manual orig_x/orig_y: distributes decoupling caps evenly across IC pins!)
            for idx_v, (ref_n, px, py) in enumerate(var_items):
                fx, fy = f_pts[idx_v % len(f_pts)]
                adx = model.NewIntVar(0, span_x, f"adx_{net}_{ref_n}_{idx_v}")
                ady = model.NewIntVar(0, span_y, f"ady_{net}_{ref_n}_{idx_v}")
                model.AddAbsEquality(adx, px - fx)
                model.AddAbsEquality(ady, py - fy)
                obj_terms.append(w * (adx + ady))
        elif len(var_items) == 2 and not f_pts:
            (_, px0, py0), (_, px1, py1) = var_items
            adx = model.NewIntVar(0, span_x, f"p2p_x_{net}")
            ady = model.NewIntVar(0, span_y, f"p2p_y_{net}")
            model.AddAbsEquality(adx, px0 - px1)
            model.AddAbsEquality(ady, py0 - py1)
            obj_terms.append(w * (adx + ady))
        else:
            xmin = model.NewIntVar(bx0 - 100, bx1 + 100, f"xmin_{net}")
            xmax = model.NewIntVar(bx0 - 100, bx1 + 100, f"xmax_{net}")
            ymin = model.NewIntVar(by0 - 100, by1 + 100, f"ymin_{net}")
            ymax = model.NewIntVar(by0 - 100, by1 + 100, f"ymax_{net}")
            if f_pts:
                model.Add(xmin <= min(p[0] for p in f_pts))
                model.Add(xmax >= max(p[0] for p in f_pts))
                model.Add(ymin <= min(p[1] for p in f_pts))
                model.Add(ymax >= max(p[1] for p in f_pts))
            for _, px, py in var_items:
                model.Add(xmin <= px)
                model.Add(xmax >= px)
                model.Add(ymin <= py)
                model.Add(ymax >= py)
            obj_terms.append(w * ((xmax - xmin) + (ymax - ymin)))

    model.Minimize(sum(obj_terms))

    solver = cp_model.CpSolver()
    solver.parameters.max_time_in_seconds = max_time_s
    solver.parameters.max_deterministic_time = min(max_time_s, 8.0)
    solver.parameters.random_seed = RANDOM_SEED
    solver.parameters.num_search_workers = 1
    status = solver.Solve(model)
    if status not in (cp_model.OPTIMAL, cp_model.FEASIBLE):
        return None

    solved: dict[str, tuple[int, int, float]] = {}
    for ps in batch:
        x, y, rot_lits = part_vars[ps.ref]
        chosen_rot = next(rg.rot for lit, rg in zip(rot_lits, ps.rots) if solver.Value(lit))
        solved[ps.ref] = (solver.Value(x), solver.Value(y), chosen_rot)
    return solved


def collect_fixed_state(
    board: pcbnew.BOARD,
    plan: dict,
    board_name: str,
    frozen_refs: set[str],
    stage_for_halo: int,
    compact_halo: bool = False,
) -> tuple[
    dict[str, list[tuple[int, int, int, int]]],
    dict[str, list[tuple[int, int]]],
    dict[tuple[str, str], tuple[int, int]],
]:
    obs = static_keepouts(board, plan, board_name)
    obs["opp_large_smd"] = []
    raised = set(plan["frame"].get("raised_modules", []))
    fine_set = set(plan["frame"].get("fine_pitch_power_pads", []))
    pads_by_net: dict[str, list[tuple[int, int]]] = {}
    ref_pads: dict[tuple[str, str], tuple[int, int]] = {}

    for fp in sorted(board.Footprints(), key=lambda f: f.GetReference()):
        ref = fp.GetReference()
        if ref not in frozen_refs:
            continue
        fpid = str(fp.GetFPID().GetLibItemName())
        is_fine = ref in fine_set or "QFN" in fpid or "FPC" in fpid
        if compact_halo or stage_for_halo == 1:
            halo = 0.0
        else:
            halo = FINE_HALO_MM if is_fine else PASSIVE_HALO_MM
        fp_obs = anchor_obstacles(fp, halo_mm=halo, raised=raised)
        for k, v in fp_obs.items():
            obs[k].extend(v)
        for p in fp.Pads():
            if p.GetAttribute() == pcbnew.PAD_ATTRIB_SMD:
                px0, py0, px1, py1 = board_mm_box(p.GetBoundingBox(), 0.0)
                if (px1 - px0) * (py1 - py0) >= 4.0:
                    gx0, gy0 = to_g_ceil(px0), to_g_ceil(py0)
                    gx1, gy1 = to_g_floor(px1), to_g_floor(py1)
                    obs["opp_large_smd"].append((gx0, gy0, gx1 - gx0, gy1 - gy0))
        by_num: dict[str, list[tuple[int, int]]] = {}
        for p in fp.Pads():
            net = p.GetNetname()
            gx = to_g(pcbnew.ToMM(p.GetPosition().x))
            gy = to_g(-pcbnew.ToMM(p.GetPosition().y))
            if net:
                pads_by_net.setdefault(net, []).append((gx, gy))
            if p.GetNumber():
                by_num.setdefault(p.GetNumber(), []).append((gx, gy))
        for num, pts in by_num.items():
            ref_pads[(ref, num)] = (round(sum(pt[0] for pt in pts) / len(pts)),
                                    round(sum(pt[1] for pt in pts) / len(pts)))
    return obs, pads_by_net, ref_pads


def current_clashes(board: pcbnew.BOARD, plan: dict, ref: str, placed_refs: set[str]) -> bool:
    """True if `ref` currently leaves the board/domain or overlaps a placed part or footprint keep-out."""
    fp = board.FindFootprintByReference(ref)
    raised = set(plan["frame"].get("raised_modules", []))
    if ref in raised:
        return False
    valid = outline_valid_poly(plan["frame"])
    regions = plan["frame"].get("domain_regions", {})
    domains = {layout.domain_at(regions, pcbnew.ToMM(p.GetPosition().x), -pcbnew.ToMM(p.GetPosition().y))
               for p in fp.Pads() if p.GetNetname()}
    if None in domains or len(domains) > 1:
        return True
    x0, y0, x1, y1 = board_mm_box(layout.body_box(fp))
    if not sbox(x0 + 2e-3, y0 + 2e-3, x1 - 2e-3, y1 - 2e-3).within(valid.buffer(5e-3)):
        return True
    side = "back" if fp.IsFlipped() else "front"
    own_box = sbox(x0 + 2e-3, y0 + 2e-3, x1 - 2e-3, y1 - 2e-3)
    for zone in layout.zones(board):
        if zone.GetIsRuleArea() and zone.GetDoNotAllowFootprints():
            layer = pcbnew.B_Cu if side == "back" else pcbnew.F_Cu
            if zone.IsOnLayer(layer):
                zx0, zy0, zx1, zy1 = board_mm_box(zone.GetBoundingBox())
                if own_box.intersects(sbox(zx0, zy0, zx1, zy1)):
                    return True
    for other_ref in placed_refs - {ref}:
        ofp = board.FindFootprintByReference(other_ref)
        if ofp is None:
            continue
        if other_ref in raised:
            for p in ofp.Pads():
                if p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH:
                    px0, py0, px1, py1 = board_mm_box(p.GetBoundingBox(), THT_CLEAR_MM)
                    if own_box.intersects(sbox(px0, py0, px1, py1)):
                        return True
            continue
        oside = "back" if ofp.IsFlipped() else "front"
        if oside == side:
            ox0, oy0, ox1, oy1 = board_mm_box(layout.body_box(ofp))
            if own_box.intersects(sbox(ox0 + 2e-3, oy0 + 2e-3, ox1 - 2e-3, oy1 - 2e-3)):
                return True
        else:
            for p in ofp.Pads():
                if p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH or p.GetDrillSizeX() > 0:
                    px0, py0, px1, py1 = board_mm_box(p.GetBoundingBox(), THT_CLEAR_MM)
                    if own_box.intersects(sbox(px0, py0, px1, py1)):
                        return True
    return False


def solve_board(
    name: str,
    targets: list[str] | None = None,
    *,
    reoptimize_all_unlocked: bool = False,
    stage1_time: float = STAGE1_MAX_TIME_S,
    stage2_time: float = STAGE2_MAX_TIME_S,
):
    plan = layout.load_plan(name)
    board = layout.fresh_board(name)
    parts = plan["parts"]
    # Place locked anchors first; unlocked parts only need their side if reoptimize_all_unlocked
    locked_only = [r for r, p in parts.items() if p.get("locked")]
    if reoptimize_all_unlocked and not targets:
        layout.place_parts(board, plan, locked_only)
        for r, p in parts.items():
            if not p.get("locked"):
                fp = board.FindFootprintByReference(r)
                if fp is not None:
                    layout.set_side(fp, p.get("side") == "back")
    else:
        layout.place_parts(board, plan, list(parts))
    layout.apply_frame(board, plan)

    frame = plan["frame"]
    raised = set(frame.get("raised_modules", []))
    valid_poly = outline_valid_poly(frame)
    bx0, by0, bx1, by1 = valid_poly.bounds
    board_bounds = (to_g_floor(bx0), to_g_floor(by0), to_g_ceil(bx1), to_g_ceil(by1))

    outline_boxes = decompose_poly_boxes(valid_poly)
    domain_pad_boxes: dict[str | None, list[tuple[int, int, int, int]]] = {
        None: decompose_domain_pad_boxes(frame, None)
    }
    for dom in frame.get("domains", []):
        domain_pad_boxes[dom] = decompose_domain_pad_boxes(frame, dom)
        if dom == "SELV-KNX":
            domain_pad_boxes[f"{dom}_corridor"] = decompose_domain_pad_boxes(frame, dom, keepout_margin_mm=0.45)

    net_domains = infer_net_domains(board, plan)
    net_fanout: dict[str, int] = {}
    for fp in board.Footprints():
        for p in fp.Pads():
            if p.GetNetname():
                net_fanout[p.GetNetname()] = net_fanout.get(p.GetNetname(), 0) + 1

    if targets:
        wanted = {r for r, p in parts.items()
                  if not p.get("locked") and r not in raised and (r in targets or p.get("block") in targets)}
        nudge_w = 0
    elif reoptimize_all_unlocked:
        wanted = {r for r, p in parts.items() if not p.get("locked") and r not in raised}
        nudge_w = 0
    else:
        ordered = [r for r, p in parts.items() if p.get("locked")] + [r for r, p in parts.items() if not p.get("locked")]
        seen_placed: set[str] = set()
        wanted = set()
        for r in ordered:
            if board.FindFootprintByReference(r) is None:
                continue
            if not parts[r].get("locked") and r not in raised and current_clashes(board, plan, r, seen_placed):
                wanted.add(r)
            else:
                seen_placed.add(r)
        nudge_w = 2

    all_refs = {fp.GetReference() for fp in board.Footprints()}
    frozen = all_refs - wanted
    specs = [build_part_spec(board, plan, r, net_domains, is_main=(name == "main"))
             for r in sorted(wanted) if board.FindFootprintByReference(r)]
    stage1_specs = [ps for ps in specs if ps.stage == 1]
    stage2_specs = [ps for ps in specs if ps.stage == 2]

    moved: dict[str, tuple[float, float, float]] = {}
    stuck: list[str] = []

    def apply_solution(sol: dict[str, tuple[int, int, float]], batch_specs: list[PartSpec]) -> None:
        for ps in batch_specs:
            gx, gy, rot = sol[ps.ref]
            xm, ym = from_g(gx), from_g(gy)
            fp = board.FindFootprintByReference(ps.ref)
            fp.SetOrientationDegrees(rot)
            fp.SetPosition(layout.pos(xm, ym))
            frozen.add(ps.ref)
            old_p = parts[ps.ref]
            if (abs(xm - old_p["x"]) > 1e-3 or abs(ym - old_p["y"]) > 1e-3
                    or abs((rot - old_p.get("rot", 0)) % 360) > 1e-3):
                moved[ps.ref] = (xm, ym, round(rot % 360, 1))

    # --- Stage 1: Active & Large Parts simultaneously per isolation domain ---
    by_dom_s1: dict[str | None, list[PartSpec]] = {}
    for ps in stage1_specs:
        by_dom_s1.setdefault(ps.domain, []).append(ps)
    for dom in sorted(by_dom_s1, key=lambda d: d or ""):
        batch = by_dom_s1[dom]
        obs, fpads, rpads = collect_fixed_state(board, plan, name, frozen, stage_for_halo=1)
        sol = solve_batch(
            batch, obs, fpads, rpads, outline_boxes, domain_pad_boxes,
            net_fanout, board_bounds, stage1_time, nudge_w, enforce_u1_rules=(name == "main"),
        )
        if sol is None and name == "main":
            # Relax U1 1.0 mm moat to 0.4 mm if domain is tight
            relaxed_batch = [
                PartSpec(**{**ps.__dict__, "rots": [measure_rot_geom(board.FindFootprintByReference(ps.ref), r, 0.35)
                                                    for r in (0.0, 90.0, 180.0, 270.0)]})
                if ps.ref == "U1" else ps
                for ps in batch
            ]
            sol = solve_batch(
                relaxed_batch, obs, fpads, rpads, outline_boxes, domain_pad_boxes,
                net_fanout, board_bounds, stage1_time, nudge_w, enforce_u1_rules=True,
            )
        if sol is None:
            stuck.extend(ps.ref for ps in batch)
            frozen.update(ps.ref for ps in batch)
        else:
            apply_solution(sol, batch)

    # --- Stage 2: Batched Passives (U1 datasheet caps first, then by domain/side/block) ---
    u1_cap_batch = [ps for ps in stage2_specs if name == "main" and ps.ref in U1_CRITICAL_CAPS]
    rem_s2 = [ps for ps in stage2_specs if ps not in u1_cap_batch]

    batches_s2: list[list[PartSpec]] = []
    if u1_cap_batch:
        batches_s2.append(sorted(u1_cap_batch, key=lambda p: p.ref))

    by_group_s2: dict[tuple[str | None, str, str], list[PartSpec]] = {}
    for ps in sorted(rem_s2, key=lambda p: (0 if any(n in CRITICAL_NETS for n in p.pad_nets.values()) else 1, p.ref)):
        by_group_s2.setdefault((ps.domain, ps.side, ps.block), []).append(ps)
    for key in sorted(by_group_s2, key=lambda k: (0 if any(n in CRITICAL_NETS for p in by_group_s2[k] for n in p.pad_nets.values()) else 1, k[0] or "", k[1], k[2])):
        batches_s2.append(by_group_s2[key])

    for batch in batches_s2:
        is_u1_caps = any(ps.ref in U1_CRITICAL_CAPS for ps in batch)
        obs, fpads, rpads = collect_fixed_state(
            board, plan, name, frozen, stage_for_halo=2, compact_halo=is_u1_caps
        )
        sol = solve_batch(
            batch, obs, fpads, rpads, outline_boxes, domain_pad_boxes,
            net_fanout, board_bounds, stage2_time, nudge_w, enforce_u1_rules=(name == "main"),
        )
        if sol is None:
            obs0, fpads0, rpads0 = collect_fixed_state(
                board, plan, name, frozen, stage_for_halo=1, compact_halo=True
            )
            sol = solve_batch(
                batch, obs0, fpads0, rpads0, outline_boxes, domain_pad_boxes,
                net_fanout, board_bounds, stage2_time, nudge_w, enforce_u1_rules=(name == "main"),
            )
        if sol is None:
            stuck.extend(ps.ref for ps in batch)
            frozen.update(ps.ref for ps in batch)
        else:
            apply_solution(sol, batch)

    # 2-opt uncrossing of identical-package U1 bus caps C2 (CAV, pin 5) and C3 (CEQ, pins 7/8)
    # after Stage 2 so all Stage 2 placements stay undisturbed while CAV/CEQ never cross!
    if name == "main" and not stuck:
        u1_fp = board.FindFootprintByReference("U1")
        c2_fp = board.FindFootprintByReference("C2")
        c3_fp = board.FindFootprintByReference("C3")
        if u1_fp and c2_fp and c3_fp and str(c2_fp.GetFPID().GetLibItemName()) == str(c3_fp.GetFPID().GetLibItemName()):
            p5_x = next(p.GetPosition().x for p in u1_fp.Pads() if p.GetNumber() == "5")
            p7_x = next(p.GetPosition().x for p in u1_fp.Pads() if p.GetNumber() == "7")
            x2, y2 = pcbnew.ToMM(c2_fp.GetPosition().x), -pcbnew.ToMM(c2_fp.GetPosition().y)
            x3, y3 = pcbnew.ToMM(c3_fp.GetPosition().x), -pcbnew.ToMM(c3_fp.GetPosition().y)
            r2, r3 = round(c2_fp.GetOrientationDegrees() % 360, 1), round(c3_fp.GetOrientationDegrees() % 360, 1)
            if (x2 - x3) * (p5_x - p7_x) < 0:
                c2_fp.SetPosition(layout.pos(x3, y3))
                c2_fp.SetOrientationDegrees(r3)
                c3_fp.SetPosition(layout.pos(x2, y2))
                c3_fp.SetOrientationDegrees(r2)
                moved["C2"] = (round(x3, 2), round(y3, 2), r3)
                moved["C3"] = (round(x2, 2), round(y2, 2), r2)

        # Relocate any front SMA diodes encroaching J1's WAGO plug-pocket roof (x < 13, y0 < 12.2)
        # into open SELV-KNX F.Cu space so build_enclosure.plug_block() keeps its full backing wall.
        if layout.SPEC["boards"]["main"].get("plane") == "perpendicular-to-rail":
            alt_slots = [(29.5, 13.0, 0.0), (29.5, 17.0, 0.0)]
            slot_idx = 0
            for dref in ("D1", "D2"):
                dfp = board.FindFootprintByReference(dref)
                if dfp is None or dfp.IsFlipped() or slot_idx >= len(alt_slots):
                    continue
                dx0, dy0, _, _ = board_mm_box(layout.body_box(dfp))
                if dx0 < 13.0 and dy0 < 12.2:
                    ox, oy = pcbnew.ToMM(dfp.GetPosition().x), -pcbnew.ToMM(dfp.GetPosition().y)
                    orot = dfp.GetOrientationDegrees()
                    sx, sy, srot = alt_slots[slot_idx]
                    dfp.SetOrientationDegrees(srot)
                    dfp.SetPosition(layout.pos(sx, sy))
                    if not current_clashes(board, plan, dref, frozen):
                        moved[dref] = (sx, sy, srot)
                        slot_idx += 1
                    else:
                        dfp.SetOrientationDegrees(orot)
                        dfp.SetPosition(layout.pos(ox, oy))

    return plan, moved, stuck


def resolve(name: str, targets: list[str] | None = None):
    return solve_board(name, targets=targets, reoptimize_all_unlocked=bool(targets))


def write_back(name: str, moved: dict[str, tuple[float, float, float]]) -> None:
    path = layout.REPO / "hardware/circuits/variants" / layout.VARIANT / f"layout_{name}.yaml"
    text = path.read_text()
    for ref, (x, y, rot) in moved.items():
        pattern = re.compile(rf"^(\s+{re.escape(ref)}:\s*\{{\s*)x:\s*[-\d.]+,\s*y:\s*[-\d.]+,\s*rot:\s*[-\d.]+",
                             re.MULTILINE)
        text, n = pattern.subn(rf"\g<1>x: {x}, y: {y}, rot: {rot:g}", text)
        if n != 1:
            sys.exit(f"{ref}: cannot update its line in {path.name}")
    path.write_text(text)


def main(argv: list[str] | None = None) -> int:
    args = list(sys.argv[1:] if argv is None else argv)
    write = "--write" in args
    reopt = "--all" in args
    args = [a for a in args if a not in ("--write", "--all")]
    name = args[0]
    targets = args[1:] or None
    _, moved, stuck = solve_board(name, targets=targets, reoptimize_all_unlocked=reopt or bool(targets))
    for ref, where in sorted(moved.items()):
        print(f"{name}: {ref} -> x {where[0]}, y {where[1]}, rot {where[2]}")
    print(f"{name}: CP-SAT placed {len(moved)} parts, infeasible: {stuck or 'none'}")
    if write and moved:
        write_back(name, moved)
    return 1 if stuck else 0


if __name__ == "__main__":
    sys.exit(main())
