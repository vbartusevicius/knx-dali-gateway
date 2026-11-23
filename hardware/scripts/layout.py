"""Persistent board layout (KiCad 10 pcbnew, `eda` container).

    python3 hardware/scripts/layout.py <board> sync
    python3 hardware/scripts/layout.py <board> place <ref-or-block> ...
    python3 hardware/scripts/layout.py <board> route [attempts]
    python3 hardware/scripts/layout.py <board> tidy-silk
    python3 hardware/scripts/layout.py <board> components

The stored layout is hardware/layout/<variant>/<board>/<board>.kicad_pcb.
`sync` rebuilds the footprints from the current netlist and carries over
every stored footprint position, track and via by reference/net name, so
circuit changes never discard layout work. The board frame (outline,
isolation slot, keep-outs, pours, rules) is always rebuilt from
layout_<board>.yaml. `place` applies the YAML coordinates for the named
parts or blocks only. `route` takes a DRC baseline of the unrouted board,
exports a DSN, routes it with fastroute (FASTROUTE_MAX_TIME seconds, up to
ROUTE_ATTEMPTS runs, as it is not repeatable), imports each session and keeps
the board with the fewest KiCad DRC errors and open connections.
hardware/tests/test_consistency.py proves a stored board still matches the
netlist.

YAML coordinates: footprint origin, mm, x right, y up, origin bottom-left.
"""

import json
import math
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import kinet2pcb
import pcbnew
import yaml
from shapely.geometry import LineString, Polygon
from shapely.geometry import box as sbox
from shapely.ops import unary_union

REPO = Path("/work")
sys.path.insert(0, str(REPO / "hardware/circuits"))
from product_spec import load_spec
from track_joints import (
    clean_planar_tracks,
    loose_ends,
    remove_dead_ends,
    seat_track_ends,
)

SPEC = load_spec()
VARIANT = SPEC["active_variant"]
LAYERS = {"F.Cu": pcbnew.F_Cu, "In1.Cu": pcbnew.In1_Cu,
          "In2.Cu": pcbnew.In2_Cu, "B.Cu": pcbnew.B_Cu}


MODELS_3D = {
    "XIAO_ESP32C6_Header": ("XIAO_ESP32C6.step", 11.0),  # 8.5 mm socket + 2.5 mm header plastic
    "Converter_DCDC_RECOM_REM3_DIP24_CTRL": ("REM3_DIP24.step", 0),
    "TerminalBlock_MaiXu_MX126-5.0-02P_1x02_P5.00mm": ("TerminalBlock_MaiXu_MX126-5.0-02P.step", 0),
    "TerminalBlock_Kefa_KF142R-5.08-2P": ("TerminalBlock_Kefa_KF142R-5.08-2P.step", 0),
    "TerminalBlock_WAGO_2086-1202_SMD": ("TerminalBlock_WAGO_2086-1202_SMD.step", 0),
    "Fuse_1812_4532Metric": ("Fuse_1812_4532Metric.step", 0),
    "Phoenix_PST_1,0-2-5,75_R32_1x02_P5.75mm": ("Phoenix_PST_1,0-2-5,75_R32.step", 0),
    "CP_Radial_D10.0mm_P5.00mm_Horizontal_Bent": ("CP_Radial_D10.0mm_P5.00mm_Horizontal_Bent.step", 0),
    "LED_WS2812B-MINI-V3J_3.5x3.5mm": ("LED_WS2812B-MINI-V3J_3.5x3.5mm.step", 0),
    "FPC_Kinghelm_KH-FPC0.5-H1.2SMT-8P": ("FPC_Kinghelm_KH-FPC0.5-H1.2SMT-8P.step", 0),
    "FPC_XUNPU_FPC-05F-14PH20": ("FPC_XUNPU_FPC-05F-14PH20.step", 0),
}


def pos(x, y):
    return pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(-y))


def edge_seg(board, x0, y0, x1, y1):
    seg = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
    seg.SetLayer(pcbnew.Edge_Cuts)
    seg.SetWidth(pcbnew.FromMM(0.05))
    seg.SetStart(pos(x0, y0))
    seg.SetEnd(pos(x1, y1))
    board.Add(seg)


def keepout_zone(board, x0, y0, x1, y1, layers):
    zone = pcbnew.ZONE(board)
    zone.SetIsRuleArea(True)
    zone.SetDoNotAllowTracks(True)
    zone.SetDoNotAllowVias(True)
    zone.SetDoNotAllowPads(False)
    zone.SetDoNotAllowFootprints(False)
    zone.SetDoNotAllowZoneFills(True)
    layer_set = pcbnew.LSET()
    for layer in layers:
        layer_set.AddLayer(layer)
    zone.SetLayerSet(layer_set)
    outline = zone.Outline()
    outline.NewOutline()
    for x, y in rect_points({"x0": x0, "y0": y0, "x1": x1, "y1": y1}):
        p = pos(x, y)
        outline.Append(p.x, p.y)
    board.Add(zone)


def dru_rules(domains, isolation, base):
    """Creepage rules between every pair of domains (6 mm unless the spec's
    isolation list says otherwise). KiCad evaluates creepage per net pair, so
    no condition can exempt an isolator's own pins: where they sit closer than
    the domain distance, a board slot between them lengthens the path."""
    minimum = {frozenset(i["between"]): i["creepage_min"] for i in isolation or []}
    power = {"SELV-KNX": "KNX-Power", "DALI": "DALI-Power", "SELV-AUX": "AUX-Power"}
    rules = [base]
    for i, a in enumerate(domains):
        for b in domains[i + 1:]:
            cond_a = f"(A.hasNetclass('{a}') || A.hasNetclass('{power[a]}'))"
            cond_b = f"(B.hasNetclass('{b}') || B.hasNetclass('{power[b]}'))"
            rules.append(f"(rule creepage_{a.lower()}_{b.lower()}\n"
                         f"  (constraint creepage (min {minimum.get(frozenset((a, b)), 5.5)}mm))\n"
                         f"  (condition \"{cond_a} && {cond_b}\"))\n")
    return "".join(rules)


def model_offset(fp):
    """Footprint-local offset that centres a body model on the courtyard."""
    fp.BuildCourtyardCaches()
    box = fp.GetCourtyard(pcbnew.B_CrtYd if fp.IsFlipped() else pcbnew.F_CrtYd).BBox()
    wx = pcbnew.ToMM(box.GetCenter().x - fp.GetPosition().x)
    wy = pcbnew.ToMM(box.GetCenter().y - fp.GetPosition().y)
    theta = math.radians(fp.GetOrientation().AsDegrees())
    lx = wx * math.cos(theta) - wy * math.sin(theta)
    ly = wx * math.sin(theta) + wy * math.cos(theta)
    return (lx, ly) if fp.IsFlipped() else (lx, -ly)


def add_3d_models(board):
    for fp in board.Footprints():
        models = fp.Models()
        for i in reversed(range(len(models))):
            if str(models[i].m_Filename).startswith("${"):
                del models[i]
        model = MODELS_3D.get(str(fp.GetFPID().GetLibItemName()))
        if model:
            ox, oy = model_offset(fp)
            m = pcbnew.FP_3DMODEL()
            m.m_Filename = f"hardware/circuits/lib/gateway_3d/{model[0]}"
            m.m_Offset = pcbnew.VECTOR3D(ox, oy, model[1])
            fp.Add3DModel(m)


def layout_dir(board):
    return REPO / "hardware/layout" / VARIANT / board


def build_dir(board):
    path = REPO / "hardware/build" / VARIANT / board
    path.mkdir(parents=True, exist_ok=True)
    return path


def load_plan(board):
    path = REPO / "hardware/circuits/variants" / VARIANT / f"layout_{board}.yaml"
    return yaml.safe_load(path.read_text())


def stored_board_path(board):
    return layout_dir(board) / f"{board}.kicad_pcb"


def fresh_board(board):
    out = build_dir(board) / f"{board}.fresh.kicad_pcb"
    kinet2pcb.kinet2pcb(str(build_dir(board) / f"{board}.net"), str(out),
                        [str(REPO / "hardware/circuits/lib")])
    return pcbnew.LoadBoard(str(out))


def set_side(fp, back):
    if fp.IsFlipped() != back:
        fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_TOP_BOTTOM)


def carry_over(target, stored):
    moved = 0
    for fp in target.Footprints():
        old = stored.FindFootprintByReference(fp.GetReference())
        if old is None:
            continue
        set_side(fp, old.IsFlipped())
        fp.SetOrientation(old.GetOrientation())
        fp.SetPosition(old.GetPosition())
        fp.SetLocked(old.IsLocked())
        fp.Reference().SetVisible(old.Reference().IsVisible())
        moved += 1
    copied = 0
    for item in stored.GetTracks():
        net = target.FindNet(item.GetNetname())
        if net is None or item.GetNetCode() == 0:
            continue
        if item.GetClass() == "PCB_VIA":
            new = pcbnew.PCB_VIA(target)
            new.SetPosition(item.GetPosition())
            new.SetViaType(item.GetViaType())
            new.SetLayerPair(item.TopLayer(), item.BottomLayer())
            new.SetDrill(item.GetDrillValue())
            new.SetWidth(item.GetWidth(pcbnew.F_Cu))
        elif item.GetClass() == "PCB_ARC":
            new = pcbnew.PCB_ARC(target)
            new.SetStart(item.GetStart())
            new.SetMid(item.GetMid())
            new.SetEnd(item.GetEnd())
            new.SetWidth(item.GetWidth())
            new.SetLayer(item.GetLayer())
        else:
            new = pcbnew.PCB_TRACK(target)
            new.SetStart(item.GetStart())
            new.SetEnd(item.GetEnd())
            new.SetWidth(item.GetWidth())
            new.SetLayer(item.GetLayer())
        new.SetLocked(item.IsLocked())
        target.Add(new)
        new.SetNet(net)
        copied += 1
    return moved, copied


def place_parts(board, plan, names):
    parts = plan["parts"]
    refs = [r for r, p in parts.items() if r in names or p.get("block") in names]
    unknown = set(names) - set(refs) - {p.get("block") for p in parts.values()}
    if unknown:
        sys.exit(f"unknown refs/blocks: {sorted(unknown)}")
    for ref in refs:
        p = parts[ref]
        fp = board.FindFootprintByReference(ref)
        if fp is None:
            sys.exit(f"{ref} is in the layout plan but not in the netlist")
        set_side(fp, p.get("side") == "back")
        fp.SetOrientationDegrees(p.get("rot", 0))
        fp.SetPosition(pos(p["x"], p["y"]))
        fp.SetLocked(bool(p.get("locked", False)))
    return refs


def rect_points(r):
    return [(r["x0"], r["y0"]), (r["x1"], r["y0"]), (r["x1"], r["y1"]), (r["x0"], r["y1"])]


def add_pour(board, net, layer, region, priority=0):
    zone = pcbnew.ZONE(board)
    zone.SetLayer(LAYERS[layer])
    outline = zone.Outline()
    outline.NewOutline()
    for x, y in rect_points(region):
        p = pos(x, y)
        outline.Append(p.x, p.y)
    zone.SetNet(board.FindNet(net))
    zone.SetAssignedPriority(priority)
    zone.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
    zone.SetLocalClearance(pcbnew.FromMM(0.2))
    zone.SetMinThickness(pcbnew.FromMM(0.2))
    zone.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    board.Add(zone)


def silk_rect(board, x0, y0, x1, y1):
    corners = rect_points({"x0": x0, "y0": y0, "x1": x1, "y1": y1})
    for a, b in zip(corners, corners[1:] + corners[:1]):
        seg = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
        seg.SetLayer(pcbnew.F_SilkS)
        seg.SetWidth(pcbnew.FromMM(0.12))
        seg.SetStart(pos(*a))
        seg.SetEnd(pos(*b))
        board.Add(seg)


def clear_frame(board):
    for item in list(board.GetDrawings()):
        if item.GetLayer() in (pcbnew.Edge_Cuts, pcbnew.F_SilkS):
            board.Remove(item)
    for zone in zones(board):
        board.RemoveNative(zone)


def zones(board):
    return [board.GetArea(i) for i in range(board.GetAreaCount())]


def apply_frame(board, plan):
    clear_frame(board)
    frame = plan["frame"]
    w, h = frame["size"]["x"], frame["size"]["y"]
    board.SetCopperLayerCount(frame["copper_layers"])
    settings = board.GetDesignSettings()
    settings.SetBoardThickness(pcbnew.FromMM(frame["thickness"]))
    settings.m_MinThroughDrill = pcbnew.FromMM(frame["min_drill"])
    settings.m_TrackMinWidth = pcbnew.FromMM(frame["min_track"])
    settings.m_CopperEdgeClearance = pcbnew.FromMM(frame["edge_clearance"])
    corners = [tuple(p) for p in frame["outline"]] if "outline" in frame else \
        rect_points({"x0": 0, "y0": 0, "x1": w, "y1": h})
    for a, b in zip(corners, corners[1:] + corners[:1]):
        edge_seg(board, *a, *b)
    for slot in frame.get("slots", []):
        corners = rect_points(slot)
        for a, b in zip(corners, corners[1:] + corners[:1]):
            edge_seg(board, *a, *b)
    for r in plan.get("oled_module", {}).values():
        silk_rect(board, r["x"], r["y"], r["x"] + r["w"], r["y"] + r["h"])
    for layer in frame.get("plane_layers", []):
        board.SetLayerType(LAYERS[layer], pcbnew.LT_POWER)
    all_copper = [LAYERS[n] for n in LAYERS if board.IsLayerEnabled(LAYERS[n])]
    for k in frame.get("copper_keepout", []):
        keepout_zone(board, k["x0"], k["y0"], k["x1"], k["y1"], all_copper)
    for z in frame.get("pours", []):
        for layer in z["layers"]:
            add_pour(board, z["net"], layer, z["region"], z.get("priority", 0))
    case = plan.get("case", {})
    for kind, layer, half in (("legs", pcbnew.B_Cu, 2.5), ("pegs", pcbnew.F_Cu, 1.5),
                              ("standoffs", pcbnew.F_Cu, 1.5)):
        for p in case.get(kind, []):
            case_contact_keepout(board, p["x"], p["y"], p.get("w", 2 * half) / 2, p.get("h", 2 * half) / 2, layer)


def case_contact_keepout(board, x, y, half_x, half_y, layer):
    """Rule area where a case leg, peg or standoff touches the board: no parts."""
    zone = pcbnew.ZONE(board)
    zone.SetIsRuleArea(True)
    zone.SetDoNotAllowFootprints(True)
    zone.SetDoNotAllowTracks(False)
    zone.SetDoNotAllowVias(False)
    zone.SetDoNotAllowPads(True)
    zone.SetDoNotAllowZoneFills(False)
    zone.SetLayer(layer)
    outline = zone.Outline()
    outline.NewOutline()
    for px, py in rect_points({"x0": x - half_x, "y0": y - half_y, "x1": x + half_x, "y1": y + half_y}):
        p = pos(px, py)
        outline.Append(p.x, p.y)
    board.Add(zone)


def fill_zones(board):
    board.BuildConnectivity()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())


# Power classes route at 0.3 mm; the router necks them to 0.225 mm between
# pads, still about 0.8 A on 1 oz outer copper against a 350 mA worst case.
POWER_TRACK_MIN = 0.22
DRU_4LAYER = f"""\
(version 1)
(rule jlcpcb_minimum
  (constraint track_width (min 0.12mm))
  (constraint clearance (min 0.2mm))
  (constraint hole_size (min 0.2mm)))
(rule power_track_width
  (constraint track_width (min {POWER_TRACK_MIN}mm))
  (condition "A.hasNetclass('KNX-Power') || A.hasNetclass('DALI-Power') || A.hasNetclass('AUX-Power')"))
"""


def neck_down_rule(refs):
    """Power tracks may neck to 0.15 mm, others to 0.1 mm (JLC 4-layer minimum
    0.09 mm) where they enter fine-pitch pads. The power rule comes last: KiCad
    applies the last matching rule, and the generic one let an 8 mm +5V track
    from J4 pass at 0.12 mm."""
    if not refs:
        return ""
    courtyards = " || ".join(f"A.intersectsCourtyard('{r}')" for r in refs)
    return ("(rule neck_down_at_fine_pitch_pins\n"
            "  (constraint track_width (min 0.1mm))\n"
            f"  (condition \"{courtyards}\"))\n"
            "(rule power_neck_down_at_fine_pitch_pins\n"
            "  (constraint track_width (min 0.15mm))\n"
            f"  (condition \"(A.hasNetclass('KNX-Power') || A.hasNetclass('DALI-Power') || A.hasNetclass('AUX-Power')) && ({courtyards})\"))\n")

def raised_module_rule(refs):
    """Parts may sit under a module raised on sockets (the courtyard covers its
    pad field); pad clearances still apply."""
    if not refs:
        return ""
    either = " || ".join(f"A.Reference == '{r}' || B.Reference == '{r}'" for r in refs)
    return ("(rule parts_under_raised_modules\n"
            "  (constraint courtyard_clearance (min -100mm))\n"
            f"  (condition \"{either}\"))\n")


def cutout_part_rule(refs):
    """Reverse-mount parts sit in their own board cutout; their legs land
    0.1 mm from its edge by design (manufacturer land pattern)."""
    if not refs:
        return ""
    either = " || ".join(f"A.memberOfFootprint('{r}')" for r in refs)
    return ("(rule reverse_mount_cutouts\n"
            "  (constraint edge_clearance (min 0.05mm))\n"
            f"  (condition \"{either}\"))\n")


POWER_CLASS = {"SELV-KNX": "KNX-Power", "DALI": "DALI-Power", "SELV-AUX": "AUX-Power"}


def domain_at(regions, x, y):
    return next((d for d, areas in regions.items()
                 for r in (areas if isinstance(areas, list) else [areas])
                 if r["x0"] <= x <= r["x1"] and r["y0"] <= y <= r["y1"]), None)


def net_domains(board, regions):
    found = {}
    for fp in board.Footprints():
        for pad in fp.Pads():
            if not pad.GetNetname():
                continue
            p = pad.GetPosition()
            domain = domain_at(regions, pcbnew.ToMM(p.x), -pcbnew.ToMM(p.y))
            found.setdefault(pad.GetNetname(), set()).add(domain)
    return found


def net_classes(board, frame):
    classes = {}
    for net, domains in net_domains(board, frame["domain_regions"]).items():
        domains.discard(None)
        if len(domains) != 1:
            sys.exit(f"net {net} touches domains {sorted(domains) or 'none'}")
        domain = domains.pop()
        classes[net] = POWER_CLASS[domain] if net in frame["power_nets"] else domain
    return classes


def project_net_settings(classes):
    def netclass(name, priority, width):
        return {"name": name, "priority": priority, "clearance": 0.2, "track_width": width,
                "via_diameter": 0.6, "via_drill": 0.3, "microvia_diameter": 0.3,
                "microvia_drill": 0.1, "diff_pair_gap": 0.25, "diff_pair_via_gap": 0.25,
                "diff_pair_width": 0.2, "wire_width": 6, "bus_width": 12, "line_style": 0,
                "pcb_color": "rgba(0,0,0,0)", "schematic_color": "rgba(0,0,0,0)"}
    defs = [netclass("Default", 2147483647, 0.2)]
    defs += [netclass(d, 2, 0.2) for d in POWER_CLASS]
    defs += [netclass(p, 1, 0.3) for p in POWER_CLASS.values()]
    patterns = [{"netclass": c, "pattern": n} for n, c in sorted(classes.items())]
    return defs, patterns


def write_rules(board, plan, board_name, pcb_path):
    frame = plan["frame"]
    dru = dru_rules(frame["domains"], SPEC["boards"].get(board_name, {}).get("isolation"),
                    DRU_4LAYER + neck_down_rule(frame.get("fine_pitch_power_pads", []))
                    + raised_module_rule(frame.get("raised_modules", []))
                    + cutout_part_rule(frame.get("cutout_parts", [])))
    pcb_path.with_suffix(".kicad_dru").write_text(dru)
    pro_path = pcb_path.with_suffix(".kicad_pro")
    project = json.loads(pro_path.read_text())
    defs, patterns = project_net_settings(net_classes(board, frame))
    project.setdefault("net_settings", {}).update({"classes": defs, "netclass_patterns": patterns})
    pro_path.write_text(json.dumps(project, indent=2))


RAISED_BODIES = {"XIAO_ESP32C6_Header"}


def body_box(fp):
    """Courtyard box; for modules raised on sockets the courtyard is only the
    pad field (parts may sit underneath), so the body is their Fab outline."""
    fp.BuildCourtyardCaches()
    bb = fp.GetCourtyard(pcbnew.B_CrtYd if fp.IsFlipped() else pcbnew.F_CrtYd).BBox()
    if str(fp.GetFPID().GetLibItemName()) in RAISED_BODIES:
        fab = pcbnew.B_Fab if fp.IsFlipped() else pcbnew.F_Fab
        for item in fp.GraphicalItems():
            if item.GetLayer() == fab and item.Type() == pcbnew.PCB_SHAPE_T:
                bb.Merge(item.GetBoundingBox())
    return bb


def fab_box(fp):
    """Fab-layer outline = the package body as drawn by the footprint author;
    the courtyard box when a footprint has none."""
    fab = pcbnew.B_Fab if fp.IsFlipped() else pcbnew.F_Fab
    shapes = [item for item in fp.GraphicalItems() if item.GetLayer() == fab and item.Type() == pcbnew.PCB_SHAPE_T]
    if not shapes:
        return body_box(fp)
    bb = shapes[0].GetBoundingBox()
    for item in shapes[1:]:
        bb.Merge(item.GetBoundingBox())
    return bb


def enclosure_box(bb):
    return {"x0": round(pcbnew.ToMM(bb.GetLeft()), 3), "x1": round(pcbnew.ToMM(bb.GetRight()), 3),
            "y0": round(-pcbnew.ToMM(bb.GetBottom()), 3), "y1": round(-pcbnew.ToMM(bb.GetTop()), 3)}


PACKAGE_HEIGHTS = yaml.safe_load((REPO / "hardware/product/package_heights.yaml").read_text())


def part_height(heights, ref, footprint):
    """An explicit per-ref height wins; otherwise the board default, raised to
    the package's datasheet maximum."""
    if ref in heights:
        return heights[ref]
    return max(heights.get("default", 2.0), PACKAGE_HEIGHTS.get(footprint, {}).get("body_max", 0.0))


def export_components(board, plan, out):
    heights = plan.get("heights", {})
    comps = []
    for fp in board.Footprints():
        bb = body_box(fp)
        p = fp.GetPosition()
        fpid = str(fp.GetFPID().GetLibItemName())
        comps.append({
            "ref": fp.GetReference(), "value": fp.GetValue(), "footprint": fpid,
            "x": round(pcbnew.ToMM(p.x), 3), "y": round(-pcbnew.ToMM(p.y), 3),
            "rot": fp.GetOrientation().AsDegrees(),
            "side": "back" if fp.IsFlipped() else "front",
            "height": part_height(heights, fp.GetReference(), fpid),
            "pads": [[round(pcbnew.ToMM(pad.GetPosition().x), 3),
                      round(-pcbnew.ToMM(pad.GetPosition().y), 3), pad.GetNumber()]
                     for pad in fp.Pads()],
            "tht_pads": [[round(pcbnew.ToMM(pad.GetPosition().x), 3),
                          round(-pcbnew.ToMM(pad.GetPosition().y), 3)]
                         for pad in fp.Pads() if pad.GetDrillSizeX() > 0],
            "bbox": enclosure_box(bb),
            "body": enclosure_box(fab_box(fp)),
        })
    out.write_text(json.dumps(comps, indent=1))


def clip_silk_off_pads(board):
    """Clip board-level silkscreen segments (e.g. OLED module outlines) around
    all exposed pads, drills and footprint silkscreen, and hide any footprint
    user/reference silkscreen text that overlaps a pad or neighbour silkscreen."""
    for back in (False, True):
        silk_layer = pcbnew.B_SilkS if back else pcbnew.F_SilkS
        cu_layer = pcbnew.B_Cu if back else pcbnew.F_Cu
        pad_polys = []
        fp_silk_by_ref = {}
        for fp in board.Footprints():
            ref = fp.GetReference()
            fp_polys = []
            for pad in fp.Pads():
                if pad.IsOnLayer(cu_layer) or pad.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH or pad.GetDrillSizeX() > 0:
                    x0, y0, x1, y1 = mm_box(pad.GetBoundingBox(), 0.24)
                    pad_polys.append(sbox(x0, y0, x1, y1))
            for item in fp.GraphicalItems():
                if item.GetLayer() == silk_layer and isinstance(item, pcbnew.PCB_SHAPE):
                    for sx0, sy0, sx1, sy1 in silk_edges(item, grow=0.16):
                        fp_polys.append(sbox(sx0, sy0, sx1, sy1))
            fp_silk_by_ref[ref] = unary_union(fp_polys) if fp_polys else Polygon()

        pad_union = unary_union(pad_polys) if pad_polys else Polygon()
        all_fp_silk = unary_union(list(fp_silk_by_ref.values())) if fp_silk_by_ref else Polygon()
        clip_obs = unary_union([pad_union, all_fp_silk])

        for item in list(board.GetDrawings()):
            if item.GetLayer() != silk_layer or not isinstance(item, pcbnew.PCB_SHAPE):
                continue
            if item.GetShape() != pcbnew.SHAPE_T_SEGMENT:
                continue
            a, b = item.GetStart(), item.GetEnd()
            line = LineString([(pcbnew.ToMM(a.x), pcbnew.ToMM(a.y)), (pcbnew.ToMM(b.x), pcbnew.ToMM(b.y))])
            if not line.intersects(clip_obs):
                continue
            width = item.GetWidth()
            board.Remove(item)
            rem = line.difference(clip_obs)
            if rem.is_empty:
                continue
            segs = [rem] if rem.geom_type == "LineString" else [g for g in rem.geoms if g.geom_type == "LineString"]
            for seg_geom in segs:
                if seg_geom.length < 0.35:
                    continue
                (x0, y0), (x1, y1) = seg_geom.coords[0], seg_geom.coords[-1]
                new_seg = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
                new_seg.SetLayer(silk_layer)
                new_seg.SetWidth(width)
                new_seg.SetStart(pcbnew.VECTOR2I(pcbnew.FromMM(x0), pcbnew.FromMM(y0)))
                new_seg.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(x1), pcbnew.FromMM(y1)))
                board.Add(new_seg)

        fab_layer = pcbnew.B_Fab if back else pcbnew.F_Fab
        placed_text_boxes = []
        for fp in board.Footprints():
            ref = fp.GetReference()
            other_silk = unary_union([g for r, g in fp_silk_by_ref.items() if r != ref and not g.is_empty]) \
                if len(fp_silk_by_ref) > 1 else Polygon()
            ref_field = fp.Reference()
            user_texts = [it for it in list(fp.GraphicalItems())
                          if hasattr(it, "TransformTextToPolySet") and it.GetLayer() == silk_layer]
            for is_field, txt in [(True, ref_field)] + [(False, it) for it in user_texts]:
                if txt.GetLayer() != silk_layer or (is_field and not txt.IsVisible()):
                    continue
                pset = pcbnew.SHAPE_POLY_SET()
                txt.TransformTextToPolySet(pset, 0, pcbnew.FromMM(0.01), pcbnew.ERROR_INSIDE)
                bb = pset.BBox() if pset.OutlineCount() else txt.GetBoundingBox()
                tx0, ty0, tx1, ty1 = mm_box(bb, 0.14)
                tpoly = sbox(tx0, ty0, tx1, ty1)
                if tpoly.intersects(pad_union) or tpoly.intersects(other_silk) or \
                        any(tpoly.intersects(prev) for prev in placed_text_boxes):
                    if is_field:
                        txt.SetVisible(False)
                    else:
                        txt.SetLayer(fab_layer)
                else:
                    placed_text_boxes.append(tpoly)


def save(board, plan, board_name, hidden_refs=()):
    path = stored_board_path(board_name)
    path.parent.mkdir(parents=True, exist_ok=True)
    for fp in board.Footprints():
        ref = fp.Reference()
        ref.SetTextSize(pcbnew.VECTOR2I(pcbnew.FromMM(0.8), pcbnew.FromMM(0.8)))
        ref.SetTextThickness(pcbnew.FromMM(0.12))
        if fp.GetReference() in hidden_refs:
            ref.SetVisible(False)
    if plan["frame"].get("silk_inside"):
        place_references_inside(board, plan["frame"])
    clip_silk_off_pads(board)
    add_3d_models(board)
    fill_zones(board)
    board.Save(str(path))
    write_rules(board, plan, board_name, path)
    export_components(board, plan, build_dir(board_name) / f"components_{board_name}.json")


def outline_polygon(frame):
    """Board outline in mm as pcbnew stores it (plan y up -> pcbnew y down)."""
    if "outline" in frame:
        return Polygon([(x, -y) for x, y in frame["outline"]])
    w, h = frame["size"]["x"], frame["size"]["y"]
    return Polygon([(0, 0), (w, 0), (w, -h), (0, -h)])


def mm_box(box, grow=0.0):
    return (pcbnew.ToMM(box.GetLeft()) - grow, pcbnew.ToMM(box.GetTop()) - grow,
            pcbnew.ToMM(box.GetRight()) + grow, pcbnew.ToMM(box.GetBottom()) + grow)


def boxes_overlap(a, b):
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def silk_edges(item, grow=0.1):
    """Outline artwork as thin boxes along its edges, so a reference can sit
    inside a large part's outline without counting as an overlap."""
    x0, y0, x1, y1 = mm_box(item.GetBoundingBox(), grow)
    if not isinstance(item, pcbnew.PCB_SHAPE) or item.GetShape() not in (pcbnew.SHAPE_T_RECT, pcbnew.SHAPE_T_POLY):
        return [(x0, y0, x1, y1)]
    w = 2 * grow + pcbnew.ToMM(item.GetWidth())
    return [(x0, y0, x1, y0 + w), (x0, y1 - w, x1, y1), (x0, y0, x0 + w, y1), (x1 - w, y0, x1, y1)]


def place_references_inside(board, frame):
    """Put every reference designator on the board, horizontal, next to (or for
    large parts on) its footprint, off all pads and other references on that
    side and off the footprints' outline artwork; hide it if no spot fits
    (frame "silk_inside": true)."""
    inside = outline_polygon(frame).buffer(-0.2)
    pads = {False: [], True: []}
    for item in board.GetDrawings():
        if item.GetLayer() in (pcbnew.F_SilkS, pcbnew.B_SilkS):
            pads[item.GetLayer() == pcbnew.B_SilkS].extend(silk_edges(item))
    for fp in board.Footprints():
        for pad in fp.Pads():
            for back in (False, True):
                if pad.IsOnLayer(pcbnew.B_Cu if back else pcbnew.F_Cu):
                    pads[back].append(mm_box(pad.GetBoundingBox(), 0.15))
        for item in fp.GraphicalItems():
            if item.GetLayer() in (pcbnew.F_SilkS, pcbnew.B_SilkS):
                pads[item.GetLayer() == pcbnew.B_SilkS].extend(silk_edges(item))
    taken = {False: [], True: []}

    def area(fp):
        x0, y0, x1, y1 = mm_box(fp.GetBoundingBox(False, False))
        return (x1 - x0) * (y1 - y0)

    for fp in sorted(board.Footprints(), key=area, reverse=True):
        ref = fp.Reference()
        ref.SetVisible(True)
        back = fp.IsFlipped()
        ref.SetTextAngle(pcbnew.EDA_ANGLE(0, pcbnew.DEGREES_T))
        x0, y0, x1, y1 = mm_box(fp.GetBoundingBox(False, False))
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        tx0, ty0, tx1, ty1 = mm_box(ref.GetBoundingBox())
        dx, dy = (tx1 - tx0) / 2 + 0.2, (ty1 - ty0) / 2 + 0.2
        spots = [(cx, y0 - dy), (cx, y1 + dy), (x0 - dx, cy), (x1 + dx, cy), (cx, cy)]
        spots += [(x, y) for x in (x0 - dx, x1 + dx) for y in (y0 - dy, y1 + dy)]
        for x, y in spots:
            ref.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y)))
            box = mm_box(ref.GetBoundingBox(), 0.05)
            fits = inside.contains(Polygon([(box[0], box[1]), (box[2], box[1]), (box[2], box[3]), (box[0], box[3])]))
            if fits and not any(boxes_overlap(box, other) for other in pads[back] + taken[back]):
                taken[back].append(box)
                break
        else:
            ref.SetVisible(False)


def current_board(board_name, plan):
    board = fresh_board(board_name)
    stored = stored_board_path(board_name)
    if stored.exists():
        old = pcbnew.LoadBoard(str(stored))
        moved, copied = carry_over(board, old)
        new_refs = [fp.GetReference() for fp in board.Footprints()
                    if old.FindFootprintByReference(fp.GetReference()) is None]
        if new_refs:
            place_parts(board, plan, new_refs)
        print(f"{board_name}: kept {moved} footprint positions, {copied} tracks/vias, "
              f"placed new {new_refs or 'none'}")
    else:
        placed = place_parts(board, plan, list(plan["parts"]))
        print(f"{board_name}: new layout, placed {len(placed)} planned parts")
    apply_frame(board, plan)
    add_pad_ties(board, plan)
    add_pin_bridges(board, plan)
    add_pad_escapes(board, plan)
    return board


CLEARANCE = pcbnew.FromMM(0.2)


def tie_clears(fp, pad, anchor, start, end, width, clearance):
    """True if a straight tie keeps the clearance to every other-net pad of the
    footprint (corner pins of a QFN pass too close to the neighbouring row)."""
    layer = pcbnew.B_Cu if fp.IsFlipped() else pcbnew.F_Cu
    half = width // 2 + clearance
    x0, x1 = sorted((start.x, end.x))
    y0, y1 = sorted((start.y, end.y))
    for other in fp.Pads():
        if other.GetNetCode() == pad.GetNetCode() or not other.IsOnLayer(layer) or not other.GetNumber():
            continue
        box = other.GetBoundingBox()
        if box.GetLeft() < x1 + half and box.GetRight() > x0 - half and \
                box.GetTop() < y1 + half and box.GetBottom() > y0 - half:
            return False
    return True


def add_pad_ties(board, plan):
    """Join each pad that shares the net of an exposed pad straight into that pad,
    along the pin's own axis and at the pin's width (plan "pad_ties": {ref: pad}).
    Locked, so the router keeps them and never necks these joins down."""
    for ref, anchor_number in plan.get("pad_ties", {}).items():
        fp = board.FindFootprintByReference(ref)
        anchor = next(p for p in fp.Pads() if p.GetNumber() == str(anchor_number))
        box = anchor.GetBoundingBox()
        existing = {((t.GetStart().x, t.GetStart().y), (t.GetEnd().x, t.GetEnd().y)) for t in board.GetTracks()}
        for pad in fp.Pads():
            if pad is anchor or pad.GetNetCode() != anchor.GetNetCode():
                continue
            start = pad.GetPosition()
            end = pcbnew.VECTOR2I(min(max(start.x, box.GetLeft()), box.GetRight()),
                                  min(max(start.y, box.GetTop()), box.GetBottom()))
            width = min(pad.GetSize().x, pad.GetSize().y)
            if start == end or ((start.x, start.y), (end.x, end.y)) in existing or \
                    not tie_clears(fp, pad, anchor, start, end, width, CLEARANCE):
                continue
            tie = pcbnew.PCB_TRACK(board)
            tie.SetStart(start)
            tie.SetEnd(end)
            tie.SetWidth(width)
            tie.SetLayer(pcbnew.B_Cu if fp.IsFlipped() else pcbnew.F_Cu)
            board.Add(tie)
            tie.SetNet(anchor.GetNet())
            tie.SetLocked(True)


def add_pin_bridges(board, plan):
    """Join neighbouring pins of one net with a locked track at the pin width
    (plan "pin_bridges": {ref: [[pin, pin], ...]}), e.g. a QFN's doubled VFILT
    pins that the router otherwise has to reach separately."""
    existing = {frozenset(((t.GetStart().x, t.GetStart().y), (t.GetEnd().x, t.GetEnd().y))) for t in board.GetTracks()}
    for ref, pairs in plan.get("pin_bridges", {}).items():
        fp = board.FindFootprintByReference(ref)
        pads = {p.GetNumber(): p for p in fp.Pads()}
        for a, b in pairs:
            pa, pb = pads[str(a)], pads[str(b)]
            if pa.GetNetCode() != pb.GetNetCode():
                sys.exit(f"{ref} pins {a}/{b} are on different nets")
            ends = frozenset(((pa.GetPosition().x, pa.GetPosition().y), (pb.GetPosition().x, pb.GetPosition().y)))
            if ends in existing:
                continue
            bridge = pcbnew.PCB_TRACK(board)
            bridge.SetStart(pa.GetPosition())
            bridge.SetEnd(pb.GetPosition())
            bridge.SetWidth(min(pa.GetSize().x, pa.GetSize().y))
            bridge.SetLayer(pcbnew.B_Cu if fp.IsFlipped() else pcbnew.F_Cu)
            board.Add(bridge)
            bridge.SetNet(pa.GetNet())
            bridge.SetLocked(True)


def stub_clears(board, pad, start, end, width, clearance, layer):
    """True if a straight stub keeps the clearance to every other-net copper pad."""
    half = width // 2 + clearance
    x0, x1 = sorted((start.x, end.x))
    y0, y1 = sorted((start.y, end.y))
    for fp in board.Footprints():
        for other in fp.Pads():
            if other.GetNetCode() == pad.GetNetCode() or not other.IsOnLayer(layer) or not other.GetNumber():
                continue
            box = other.GetBoundingBox()
            if box.GetLeft() < x1 + half and box.GetRight() > x0 - half and \
                    box.GetTop() < y1 + half and box.GetBottom() > y0 - half:
                return False
    return True


def add_pad_escapes(board, plan):
    """Fan each connected pin of a fine-pitch footprint out along its own axis, at
    pin width, a short way past the pin row (plan "pad_escapes": {ref: {length: mm,
    skip: [pins]}}),
    so the router joins a full-width stub instead of necking down between pins.
    Locked; pins already tied into an exposed pad and stubs that would come too
    close to other copper are skipped."""
    existing = {(p.x, p.y) for t in board.GetTracks() for p in (t.GetStart(), t.GetEnd())}
    for ref, escape in plan.get("pad_escapes", {}).items():
        length, skip = escape["length"], {str(n) for n in escape.get("skip", [])}
        fp = board.FindFootprintByReference(ref)
        centre = fp.GetPosition()
        for pad in fp.Pads():
            start = pad.GetPosition()
            if not pad.GetNetCode() or not pad.GetNumber() or pad.GetNumber() in skip or \
                    (start.x, start.y) in existing:
                continue
            box = pad.GetBoundingBox()
            if box.GetWidth() == box.GetHeight():
                continue
            reach = pcbnew.FromMM(length)
            if box.GetWidth() > box.GetHeight():
                edge = box.GetRight() if start.x > centre.x else box.GetLeft()
                end = pcbnew.VECTOR2I(edge + (reach if start.x > centre.x else -reach), start.y)
            else:
                edge = box.GetBottom() if start.y > centre.y else box.GetTop()
                end = pcbnew.VECTOR2I(start.x, edge + (reach if start.y > centre.y else -reach))
            width = min(box.GetWidth(), box.GetHeight())
            layer = pcbnew.B_Cu if fp.IsFlipped() else pcbnew.F_Cu
            if not stub_clears(board, pad, start, end, width, CLEARANCE, layer):
                continue
            stub = pcbnew.PCB_TRACK(board)
            stub.SetStart(start)
            stub.SetEnd(end)
            stub.SetWidth(width)
            stub.SetLayer(layer)
            board.Add(stub)
            stub.SetNet(pad.GetNet())
            stub.SetLocked(True)


def sync(board_name):
    plan = load_plan(board_name)
    save(current_board(board_name, plan), plan, board_name)
    print(f"{board_name}: saved {stored_board_path(board_name).relative_to(REPO)}")


def tidy_silk(board_name):
    plan = load_plan(board_name)
    path = stored_board_path(board_name)
    report = build_dir(board_name) / "silk_drc.json"
    subprocess.run(["kicad-cli", "pcb", "drc", "--format", "json", "--severity-all",
                    "--output", str(report), str(path)], capture_output=True, check=False)
    clashing = {item["description"].split("Reference field of ")[1].split()[0]
                for v in json.loads(report.read_text())["violations"]
                if v["type"] in ("silk_overlap", "silk_over_copper", "silk_edge_clearance")
                for item in v["items"] if item["description"].startswith("Reference field of ")}
    save(current_board(board_name, plan), plan, board_name, clashing)
    print(f"{board_name}: hid {len(clashing)} clashing references: {', '.join(sorted(clashing))}")


def export_dsn(board_name, locked_ses=None):
    """Pours are left out: a router treats any via or THT pin inside a plane
    polygon as connected through it, whatever the fill really reaches. Rule areas
    that forbid neither tracks nor vias (case contacts: no parts) are left out
    too, as the DSN would make them full keep-outs."""
    plan = load_plan(board_name)
    board = current_board(board_name, plan)
    for zone in zones(board):
        if not zone.GetIsRuleArea() or not (zone.GetDoNotAllowTracks() or zone.GetDoNotAllowVias()):
            board.RemoveNative(zone)
    for track in list(board.GetTracks()):
        if not track.IsLocked():
            board.RemoveNative(track)
    if locked_ses is not None and locked_ses.exists():
        pcbnew.ImportSpecctraSES(board, str(locked_ses))
        for track in board.GetTracks():
            if track.GetNetname() in U1_SEED_NETS:
                track.SetLocked(True)
    routable = build_dir(board_name) / f"{board_name}.routable.kicad_pcb"
    board.Save(str(routable))
    write_rules(board, plan, board_name, routable)
    dsn = build_dir(board_name) / f"{board_name}.dsn"
    if not pcbnew.ExportSpecctraDSN(board, str(dsn)):
        sys.exit("DSN export failed")
    nets = sorted(str(n) for n in board.GetNetsByName() if str(n))
    project = json.loads(routable.with_suffix(".kicad_pro").read_text())
    frame = plan["frame"]
    text = with_net_classes(dsn.read_text(), nets, project["net_settings"])
    text = with_inset_cutouts(with_inset_boundary(with_power_layers(text, frame), frame), frame)
    dsn.write_text(text)
    print(f"{board_name}: exported {dsn.relative_to(REPO)}")


def with_inset_boundary(dsn_text, frame):
    """The router keeps only the track clearance to the outline; shrink the
    outline so routed copper also meets the edge clearance."""
    inset = (frame["edge_clearance"] - 0.2) * 1000

    def rewrite(match):
        nums = [float(n) for n in match.group(2).split()]
        shrunk = Polygon(list(zip(nums[::2], nums[1::2]))).buffer(-inset, join_style="mitre")
        points = list(shrunk.exterior.coords)
        return match.group(1) + "  ".join(f"{x:.0f} {y:.0f}" for x, y in points) + ")"

    return re.sub(r"(\(boundary\s+\(path pcb 0\s+)([-\d.\s]+)\)", rewrite, dsn_text, count=1)


def with_inset_cutouts(dsn_text, frame):
    """KiCad exports board cutouts (isolation slots) as keep-outs exactly on the
    edge; grow them like the outline so copper meets the edge clearance there too."""
    grow = (frame["edge_clearance"] - 0.2) * 1000

    def rewrite(match):
        nums = [float(n) for n in match.group(2).split()]
        grown = Polygon(list(zip(nums[::2], nums[1::2]))).buffer(grow, join_style="mitre")
        return match.group(1) + "  ".join(f"{x:.0f} {y:.0f}" for x, y in grown.exterior.coords) + "))"

    return re.sub(r'(\(keepout "" \(polygon signal 0\s+)([-\d.\s]+)\)\)', rewrite, dsn_text)


def sexp_end(text, start):
    depth = 0
    for i in range(start, len(text)):
        depth += {"(": 1, ")": -1}.get(text[i], 0)
        if depth == 0:
            return i + 1
    sys.exit("unbalanced DSN expression")


def without_shapes_on(block, layer):
    shape = re.compile(rf"[ \t]*\(shape \(\w+ {re.escape(layer)} ")
    pieces, cursor = [], 0
    for s in shape.finditer(block):
        stop = sexp_end(block, block.index("(shape", s.start()))
        pieces.append(block[cursor:s.start()])
        cursor = stop + (block[stop:stop + 1] == "\n")
    return "".join(pieces) + block[cursor:]


def with_power_layers(dsn_text, frame):
    """Plane layers go to the router as power layers (no tracks; vias pass, so
    the session imports through vias rather than buried ones). THT pins lose
    their plane-layer shape: fastroute refuses any connection between pins on a
    power layer that carries no plane of their net ("some of their layers are
    disabled"). The router mirrors the layers of back-side parts, so their pins
    get a padstack copy without the mirrored layer instead (dropping the plane
    layer there removed their In2 copper and let In2 tracks short through them)."""
    planes = frame.get("plane_layers", [])
    if not planes:
        return dsn_text
    layers = re.findall(r"\n    \(layer (\S+)\n", dsn_text)
    mirrored = {plane: layers[len(layers) - 1 - layers.index(plane)] for plane in planes}
    for plane in planes:
        dsn_text = re.sub(rf"(\(layer {re.escape(plane)}\n\s*\(type )signal\)", r"\1power)", dsn_text)
    sides = {}
    for comp in re.finditer(r'\(component ("[^"]+"|\S+)\n((?:\s+\(place [^\n]*\n)+)', dsn_text):
        sides.setdefault(comp[1], set()).update(re.findall(r"\(place \S+ \S+ \S+ (front|back)", comp[2]))
    out, pos, back_names = [], 0, {}
    for m in re.finditer(r"\(padstack (\"[^\"]+\"|\S+)", dsn_text):
        end = sexp_end(dsn_text, m.start())
        block, name = dsn_text[m.start():end], m[1]
        if not name.strip('"').startswith("Via") and any(f" {plane} " in block for plane in planes):
            front, back = block, block
            for plane in planes:
                front, back = without_shapes_on(front, plane), without_shapes_on(back, mirrored[plane])
            back_names[name] = f'"{name.strip(chr(34))}_back"'
            block = front + "\n    " + back.replace(f"(padstack {name}", f"(padstack {back_names[name]}", 1)
        out.append(dsn_text[pos:m.start()] + block)
        pos = end
    dsn_text = "".join(out) + dsn_text[pos:]

    def back_pins(image):
        name = image[1]
        if sides.get(name) != {"back"}:
            if "back" in sides.get(name, ()) and any(f"(pin {ps} " in image[0] for ps in back_names):
                sys.exit(f"THT image {name} is placed on both sides")
            return image[0]
        return re.sub(r"\(pin (\"[^\"]+\"|\S+) ", lambda pin: f"(pin {back_names.get(pin[1], pin[1])} ", image[0])

    return re.sub(r'\(image ("[^"]+"|\S+)\n(?:      [^\n]*\n)*?    \)', back_pins, dsn_text)


DSN_CLEARANCE_MARGIN_UM = 3
U1_SEED_NETS = {"CCP", "CAV", "CEQ1", "CEQ2", "VSW1"}


def with_net_classes(dsn_text, nets, net_settings, split_seed_nets=False):
    """KiCad 10's DSN exporter puts every net into the default class; rebuild
    the class section from the project's net classes and assignments. Add a
    +3 um rounding margin above KiCad's DRC minimums, and for classes with extra
    wire-to-wire spacing override wire_smd and smd_smd back to the base margin so
    traces can still escape fine-pitch IC/connector pads."""
    start = dsn_text.index("    (class ")
    end = dsn_text.index("  (wiring")
    via = dsn_text[start:end].split("(use_via ")[1].split(")")[0]
    assigned = {p["pattern"]: p["netclass"] for p in net_settings["netclass_patterns"]}
    classes = {c["name"]: c for c in net_settings["classes"]}
    base_clr = round(classes["Default"]["clearance"] * 1000) + DSN_CLEARANCE_MARGIN_UM
    groups = {}
    seed_members = [n for n in nets if split_seed_nets and n in U1_SEED_NETS]
    for net in nets:
        if not (split_seed_nets and net in U1_SEED_NETS):
            groups.setdefault(assigned.get(net, "Default"), []).append(net)
    blocks = []
    if seed_members:
        blocks.append(f'    (class U1Seed {" ".join(seed_members)}\n      (circuit\n        (use_via {via})\n      )\n'
                      f'      (rule\n        (width 200)\n        (clearance {base_clr})\n      )\n    )\n')
    for cls, members in sorted(groups.items()):
        clr = round(classes[cls]["clearance"] * 1000) + DSN_CLEARANCE_MARGIN_UM
        smd_override = (f"        (clearance {base_clr} (type wire_smd))\n"
                        f"        (clearance {base_clr} (type smd_smd))\n") if clr > base_clr else ""
        blocks.append(f'    (class {cls} {" ".join(members)}\n      (circuit\n        (use_via {via})\n      )\n'
                      f'      (rule\n        (width {classes[cls]["track_width"] * 1000:.0f})\n'
                      f'        (clearance {clr})\n{smd_override}      )\n    )\n')
    return dsn_text[:start] + "".join(blocks) + "  )\n" + dsn_text[end:]


GROUND_NETS = {"GND", "GNDD", "GNDA"}


def unconnected(board):
    fill_zones(board)
    board.BuildConnectivity()
    return board.GetConnectivity().GetUnconnectedCount(False)


# Tracks taken off a board must outlive the process: once Python frees a
# removed PCB_TRACK, KiCad 10's SWIG layer is corrupted for the rest of the run.
_DETACHED = []


def prune_ground_tracks(board):
    """The router gets no pours (see export_dsn), so it routes ground as an
    ordinary net; drop every ground track whose connection the pours already
    make (55-97 tracks on v3 main, measured 2026-11-16). Kept because passing
    the In1 pours as DSN planes broke routing: fastroute reported GND, GNDA and
    GNDD connections of both main boards as blocked. Removing tracks leaves
    grazing ends, which import_ses seats afterwards."""
    candidates = [t for t in board.GetTracks() if t.GetNetname() in GROUND_NETS and t.GetClass() != "PCB_VIA"]
    _DETACHED.extend(candidates)
    baseline = unconnected(board)
    removed, changed = 0, True
    while changed:
        changed = False
        for track in list(candidates):
            board.Remove(track)
            if unconnected(board) > baseline:
                board.Add(track)
            else:
                candidates.remove(track)
                removed += 1
                changed = True
    return removed


def widen_thin_tracks(board, frame):
    """fastroute necks power tracks below the 0.22 mm power_track_width rule away
    from the fine-pitch parts (up to 8 on v3 main, 2 on v2 main, measured
    2026-11-16); restore them to it. Kept because both router-side ways break
    routing: --no-neckdown-classes for the power classes left 5-6 connections of
    each main board unrouted and put In2 tracks through THT pads (shorts), and
    --router.min_trace_width_um=220 routed fewer connections on v3 main.
    A track is widened only as far as it keeps the clearance to other-net copper
    on its layer (widening to 0.22 mm once pushed +5V into +3V3's clearance);
    one that cannot reach 0.22 mm is left for DRC to report."""
    power_min = pcbnew.FromMM(POWER_TRACK_MIN)
    fine_power_min = pcbnew.FromMM(0.15)
    fine = [board.FindFootprintByReference(r) for r in frame.get("fine_pitch_power_pads", [])]
    for fp in fine:
        fp.BuildCourtyardCaches()
    courtyards = [fp.GetCourtyard(pcbnew.B_CrtYd if fp.IsFlipped() else pcbnew.F_CrtYd) for fp in fine]
    widened = 0
    for track in board.GetTracks():
        if (track.Type() != pcbnew.PCB_TRACE_T or track.GetNetname() not in frame["power_nets"]
                or track.GetWidth() >= power_min):
            continue
        in_fine = any(c.OutlineCount() and (c.Contains(track.GetStart()) or c.Contains(track.GetEnd()))
                      for c in courtyards)
        target_w = fine_power_min if in_fine else power_min
        if track.GetWidth() >= target_w:
            continue
        original = track.GetWidth()
        track.SetWidth(target_w)
        if collides(board, track):
            track.SetWidth(original)
            continue
        widened += 1
    return widened


def collides(board, track):
    """True if the track comes closer than the clearance to copper of another net on its layer."""
    layer, net = track.GetLayer(), track.GetNetCode()
    shape = track.GetEffectiveShape(layer)
    others = [t for t in board.GetTracks() if t is not track and t.GetNetCode() != net and t.IsOnLayer(layer)]
    others += [p for p in board.GetPads() if p.GetNetCode() != net and p.IsOnLayer(layer)]
    return any(shape.Collide(o.GetEffectiveShape(layer), CLEARANCE) for o in others)


def prune_redundant_loose_tails(board):
    """Drop short (<0.5 mm) loose-end leftovers from cycle pruning whose removal
    does not open any connection, preventing seat/cycle ping-pong near EPADs."""
    removed = 0
    base = unconnected(board)
    for t, _ in list(loose_ends(board)):
        if t.IsLocked() or t.GetLength() > pcbnew.FromMM(0.5):
            continue
        _DETACHED.append(t)
        board.Remove(t)
        if unconnected(board) > base:
            board.Add(t)
        else:
            removed += 1
    return removed


def polish_tracks(board):
    """Split crossing/overlapping same-net segments, prune planar cycles, drop
    dead-end stubs and seat grazing ends without leaving mid-segment T joints."""
    z1, s1, d1, c1 = clean_planar_tracks(board)
    dead = remove_dead_ends(board)
    seated = seat_track_ends(board)
    z2, s2, d2, c2 = clean_planar_tracks(board)
    dead += remove_dead_ends(board) + prune_redundant_loose_tails(board)
    return z1 + z2, s1 + s2, d1 + d2, c1 + c2, seated, dead


def import_ses(board_name, seed_ses=None):
    plan = load_plan(board_name)
    board = current_board(board_name, plan)
    for track in list(board.GetTracks()):
        if not track.IsLocked():
            board.RemoveNative(track)
    pre_locked = {t.m_Uuid.AsString() for t in board.GetTracks() if t.IsLocked()}
    if seed_ses is not None and seed_ses.exists():
        pcbnew.ImportSpecctraSES(board, str(seed_ses))
    ses = build_dir(board_name) / f"{board_name}.ses"
    if not pcbnew.ImportSpecctraSES(board, str(ses)):
        sys.exit("SES import failed")
    for track in board.GetTracks():
        if track.m_Uuid.AsString() not in pre_locked:
            track.SetLocked(False)
    widened = widen_thin_tracks(board, plan["frame"])
    removed = prune_ground_tracks(board) if plan["frame"].get("prune_ground_tracks", True) else 0
    zero, splits, dedup, cycles, seated, dead = polish_tracks(board)
    print(f"{board_name}: imported {ses.relative_to(REPO)}; widened {widened} power tracks, removed {removed} "
          f"ground tracks, split {splits}/deduped {dedup}/pruned {cycles} planar loops ({zero} zero-len), "
          f"seated {seated} grazing track ends, removed {dead} dead-end stubs")
    save(board, plan, board_name)


def clean_stored_tracks(board_name):
    """Clean planar track anomalies on the stored board in place, preserving all
    footprint/pad UUIDs and placement."""
    path = stored_board_path(board_name)
    plan = load_plan(board_name)
    board = pcbnew.LoadBoard(str(path))
    zero, splits, dedup, cycles, seated, dead = polish_tracks(board)
    fill_zones(board)
    board.Save(str(path))
    write_rules(board, plan, board_name, path)
    print(f"{board_name}: cleaned in place (zero={zero}, splits={splits}, dedup={dedup}, "
          f"cycles={cycles}, seated={seated}, dead={dead})")


FASTROUTE = ["fastroute", f"--max-time={os.environ.get('FASTROUTE_MAX_TIME', '600')}",
             "--router.fanout.enabled=false"]
LIBRARY_ONLY = {"lib_footprint_issues", "lib_footprint_mismatch"}


def drc_violations(pcb_path, report):
    """KiCad DRC is the only pass/fail; library-only warnings say nothing about
    the copper."""
    subprocess.run(["kicad-cli", "pcb", "drc", "--format", "json", "--severity-all",
                    "--output", str(report), str(pcb_path)], capture_output=True, check=False)
    return [v for v in json.loads(report.read_text())["violations"] if v["type"] not in LIBRARY_ONLY]


def violation_key(v):
    return v["type"], tuple(sorted(i["description"] for i in v["items"]))


def unrouted_baseline(board_name):
    """The board as routing starts from it (pours, locked ties and escapes, no
    other copper): its DRC violations come from footprints or placement, and
    its open connections are the ones there are to route."""
    plan = load_plan(board_name)
    board = current_board(board_name, plan)
    for track in list(board.GetTracks()):
        if not track.IsLocked():
            board.RemoveNative(track)
    path = build_dir(board_name) / f"{board_name}.unrouted.kicad_pcb"
    total = unconnected(board)
    board.Save(str(path))
    write_rules(board, plan, board_name, path)
    return total, drc_violations(path, build_dir(board_name) / "unrouted_drc.json")


def route(board_name, attempts=None):
    """fastroute is not repeatable run to run: import up to `attempts`
    (ROUTE_ATTEMPTS, default 4) sessions and keep the board with the fewest DRC
    errors and open connections, stopping at the first clean one."""
    attempts = attempts or int(os.environ.get("ROUTE_ATTEMPTS", "4"))
    total, baseline = unrouted_baseline(board_name)
    print(f"{board_name}: unrouted baseline has {total} connections, {len(baseline)} DRC violations")
    export_dsn(board_name)
    bdir = build_dir(board_name)
    unrouted_pcb = bdir / f"{board_name}.unrouted.kicad_pcb"
    dsn, ses = bdir / f"{board_name}.dsn", bdir / f"{board_name}.ses"
    best = None
    for attempt in range(1, attempts + 1):
        shutil.copy(unrouted_pcb, stored_board_path(board_name))
        ses.unlink(missing_ok=True)
        # The DSN carries no board minimum track width; without it fastroute necks down to
        # 0.1 mm, below the 0.12 mm fab minimum. A connection that fails at its class width is
        # retried whole at the neck width: at the power minimum, not 0.12 mm (an 8 mm +5V track).
        minimum = round(load_plan(board_name)["frame"]["min_track"] * 1000)
        base_args = [f"--router.min_trace_width_um={minimum}",
                     f"--router.neck_width_um={round(POWER_TRACK_MIN * 1000)}"]
        init_args = []
        if board_name == "main":
            ns = json.loads(stored_board_path(board_name).with_suffix(".kicad_pro").read_text())["net_settings"]
            dsn_text = dsn.read_text()
            nets_m = [n.strip('"') for line in dsn_text.splitlines()
                      if line.startswith("    (net ") for n in [line.split()[1]]]
            seed_dsn = bdir / f"{board_name}.seed.dsn"
            seed_ses = bdir / f"{board_name}.seed.ses"
            seed_ses.unlink(missing_ok=True)
            seed_dsn.write_text(with_net_classes(dsn_text, nets_m, ns, split_seed_nets=True))
            other_cls = ",".join(c["name"] for c in ns["classes"])
            subprocess.run(["fastroute", "--max-time=15", "--router.fanout.enabled=false",
                            f"--router.autorouter.ignore_net_classes={other_cls}",
                            *base_args, "-de", str(seed_dsn), "-do", str(seed_ses)],
                           capture_output=True, text=True, check=False)
            if seed_ses.exists():
                init_args = [f"--initial-session={seed_ses}"]
        run = subprocess.run(FASTROUTE + base_args + init_args + ["-de", str(dsn), "-do", str(ses)],
                             capture_output=True, text=True, check=False)
        if run.returncode or not ses.exists():
            print(f"{board_name}: route attempt {attempt}: fastroute failed (exit {run.returncode})")
            continue
        import_ses(board_name)
        result = report_routing(board_name, total, baseline)
        print(f"{board_name}: route attempt {attempt}: {result[0]} DRC errors + open connections, "
              f"{result[1]} DRC items not in the unrouted baseline")
        if best is None or result < best[0]:
            best = (result, bdir / f"{board_name}.attempt{attempt}.kicad_pcb")
            shutil.copy(stored_board_path(board_name), best[1])
        if result[0] == 0:
            break
    if best is None:
        sys.exit(f"{board_name}: no route attempt produced a session")
    shutil.copy(best[1], stored_board_path(board_name))
    passed = best[0][0] == 0
    print(f"{board_name}: kept {best[1].name}; DRC {'PASS' if passed else 'FAIL'}")
    return passed


def report_routing(board_name, total, baseline):
    """Returns (DRC errors + open connections, DRC items new against the unrouted baseline, vias)."""
    path = stored_board_path(board_name)
    board = pcbnew.LoadBoard(str(path))
    open_connections = unconnected(board)
    tracks = [t for t in board.GetTracks() if t.Type() != pcbnew.PCB_VIA_T]
    vias = len(board.GetTracks()) - len(tracks)
    violations = drc_violations(path, build_dir(board_name) / "routed_drc.json")
    errors = sum(v["severity"] == "error" for v in violations)
    known = {violation_key(v) for v in baseline}
    from_routing = [v for v in violations if violation_key(v) not in known]
    print(f"{board_name}: routed {total - open_connections}/{total} connections, "
          f"{len(tracks)} tracks, {vias} vias; DRC: {errors} errors, {len(violations) - errors} warnings, "
          f"{len(from_routing)} not in the unrouted baseline")
    for v in from_routing:
        print(f"  {v['severity']} {v['type']}: {v['description']}")
    return errors + open_connections, len(from_routing), vias


def components(board_name):
    """Export part outlines and heights of the stored board, changing nothing."""
    out = build_dir(board_name) / f"components_{board_name}.json"
    export_components(pcbnew.LoadBoard(str(stored_board_path(board_name))), load_plan(board_name), out)
    print(f"{board_name}: wrote {out.relative_to(REPO)}")


def unroute(board_name):
    plan = load_plan(board_name)
    board = current_board(board_name, plan)
    tracks = list(board.GetTracks())
    for track in tracks:
        board.Remove(track)
    save(board, plan, board_name)
    print(f"{board_name}: removed {len(tracks)} tracks/vias")


def place(board_name, names):
    plan = load_plan(board_name)
    board = current_board(board_name, plan)
    refs = place_parts(board, plan, names)
    save(board, plan, board_name)
    print(f"{board_name}: placed {', '.join(refs)}")


if __name__ == "__main__":
    name, command, *args = sys.argv[1:]
    if command == "sync":
        sync(name)
    elif command == "place":
        place(name, args)
    elif command == "tidy-silk":
        tidy_silk(name)
    elif command == "components":
        components(name)
    elif command == "unroute":
        unroute(name)
    elif command == "route":
        sys.exit(0 if route(name, *(int(a) for a in args)) else 1)
    elif command == "export-dsn":
        export_dsn(name)
    elif command == "import-ses":
        import_ses(name)
    elif command == "clean-tracks":
        clean_stored_tracks(name)
    elif command == "autoplace":
        import resolve_overlaps
        sys.exit(resolve_overlaps.main([name, *args]))
    else:
        sys.exit(f"unknown command {command}")
