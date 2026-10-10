# SPDX-FileCopyrightText: The knx-dali-gateway contributors
# SPDX-License-Identifier: CERN-OHL-P-2.0
"""Deterministic PCB placement inside the `eda` container.

    python3 hardware/scripts/place_board.py <board>

Reads the kinet2pcb-generated board in hardware/build/<board>/, plus
hardware/product/spec.yaml, hardware/circuits/placement_<board>.yaml and
parts.json (functional-block tags from the SKiDL step), then writes
<board>.placed.kicad_pcb with:

  * board outline (Edge.Cuts) from the spec
  * fixed placements (terminals, UI, straddling parts, interconnect)
  * deterministic functional-block packing for the rest
  * mounting holes
  * isolation slot + copper keepout (lower) / antenna keepout (upper)
  * net classes + JLCPCB-compatible design rules + .kicad_dru rules file
  * component position JSON for the enclosure/fit check

Coordinate mapping: spec/board-local y-up -> pcbnew y = -y.
"""

import json
import sys
from pathlib import Path

import pcbnew
import yaml

REPO = Path("/work")
SPEC = yaml.safe_load((REPO / "hardware/product/spec.yaml").read_text())

DRU_RULES = """\
(version 1)
(rule jlcpcb_2layer
  (constraint track_width (min 0.2mm))
  (constraint clearance (min 0.2mm))
  (constraint hole_size (min 0.3mm)))
(rule dali_vs_selv
  (constraint clearance (min 6.0mm))
  (condition "A.NetClass == 'DALI' && B.NetClass == 'SELV'"))
(rule selv_vs_dali
  (constraint clearance (min 6.0mm))
  (condition "A.NetClass == 'SELV' && B.NetClass == 'DALI'"))
(rule dali_vs_aux
  (constraint clearance (min 6.0mm))
  (condition "A.NetClass == 'DALI' && B.NetClass == 'AUX'"))
(rule aux_vs_dali
  (constraint clearance (min 6.0mm))
  (condition "A.NetClass == 'AUX' && B.NetClass == 'DALI'"))
"""

NET_CLASSES = {
    "lower": {
        "SELV": ["GND", "+3V3_TRX", "+3V3_MCU", "+12V_STORE", "KNX_P",
                 "KNX_P_PROT", "KNX_TXD", "KNX_RXD", "DALI_TX", "DALI_RX",
                 "INT_SPARE1", "INT_SPARE2", "VFILT", "V20V", "CCP",
                 "CCP_B", "CAV", "CEQ1", "CEQ2", "VSW1", "VSW2", "VDD2MV"],
        "DALI": ["GNDD", "DA1", "DA2", "DA_P", "DALI_ISO24V", "DALI_20V",
                 "DALI_SUPPLY_SW", "DALI_GATE", "DALI_SENSE", "DALI_FB6"],
        "AUX": ["GNDA", "AUX_P", "AUX_FUSED"],
    },
    "upper": {
        "SELV": ["GND", "+3V3", "+3V3_MCU", "+3V3_USB", "+5V_USB", "EN",
                 "KNX_TXD", "KNX_RXD", "DALI_TX", "DALI_RX", "INT_SPARE1",
                 "INT_SPARE2", "PROG_BTN", "LED_PROG", "LED_KNX",
                 "LED_DALI", "I2C_SDA", "I2C_SCL", "USB_DP", "USB_DM",
                 "USB_DP_MCU", "USB_DM_MCU"],
    },
}


def mm(v):
    return pcbnew.FromMM(v)


def pos(x_spec, y_spec):
    """spec/board coords (x right, y up) -> pcbnew coords."""
    return pcbnew.VECTOR2I(mm(x_spec), mm(-y_spec))


def edge_seg(board, x0, y0, x1, y1, width=0.05):
    seg = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
    seg.SetLayer(pcbnew.Edge_Cuts)
    seg.SetWidth(mm(width))
    seg.SetStart(pos(x0, y0))
    seg.SetEnd(pos(x1, y1))
    board.Add(seg)


def keepout_zone(board, x0, y0, x1, y1, layers, allow_footprints=True):
    z = pcbnew.ZONE(board)
    z.SetIsRuleArea(True)
    z.SetDoNotAllowTracks(True)
    z.SetDoNotAllowVias(True)
    z.SetDoNotAllowPads(False)
    z.SetDoNotAllowFootprints(not allow_footprints)
    z.SetDoNotAllowZoneFills(True)
    lset = pcbnew.LSET()
    for layer in layers:
        lset.AddLayer(layer)
    z.SetLayerSet(lset)
    outline = z.Outline()
    outline.NewOutline()
    for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
        p = pos(x, y)
        outline.Append(p.x, p.y)
    board.Add(z)


def main():
    board_name = sys.argv[1]
    bdir = REPO / "hardware/build" / board_name
    spec_board = SPEC["boards"][board_name]
    W = spec_board["outline"]["width"]
    H = spec_board["outline"]["height"]
    plan = yaml.safe_load(
        (REPO / f"hardware/circuits/placement_{board_name}.yaml").read_text())
    parts = json.loads((bdir / "parts.json").read_text())

    board = pcbnew.LoadBoard(str(bdir / f"{board_name}.kicad_pcb"))

    # --- outline -----------------------------------------------------------
    edge_seg(board, 0, 0, W, 0)
    edge_seg(board, W, 0, W, H)
    edge_seg(board, W, H, 0, H)
    edge_seg(board, 0, H, 0, 0)

    # isolation slots (routed slots drawn as thin Edge.Cuts rectangles)
    slots = ([plan["isolation_slot"]] if "isolation_slot" in plan else []) \
        + list(plan.get("extra_slots", []))
    for slot in slots:
        edge_seg(board, slot["x0"], slot["y0"], slot["x1"], slot["y0"])
        edge_seg(board, slot["x1"], slot["y0"], slot["x1"], slot["y1"])
        edge_seg(board, slot["x1"], slot["y1"], slot["x0"], slot["y1"])
        edge_seg(board, slot["x0"], slot["y1"], slot["x0"], slot["y0"])

    # --- fixed placements --------------------------------------------------
    placed = set()
    for ref, p in plan.get("fixed", {}).items():
        fp = board.FindFootprintByReference(ref)
        if fp is None:
            print(f"WARN: fixed ref {ref} not on board")
            continue
        fp.SetPosition(pos(0, 0))
        fp.SetOrientationDegrees(p.get("rot", 0))
        bb = fp.GetBoundingBox(False, False)
        dx = p["x"] - pcbnew.ToMM((bb.GetLeft() + bb.GetRight()) / 2)
        dy = -p["y"] - pcbnew.ToMM((bb.GetTop() + bb.GetBottom()) / 2)
        fp.Move(pcbnew.VECTOR2I(mm(dx), mm(dy)))
        fp.SetLocked(True)
        placed.add(ref)

    # --- deterministic block packing ---------------------------------------
    # spec-coord bboxes of everything already placed, per board side
    occupied = {"front": [], "back": []}

    def spec_bbox(fp):
        bb = fp.GetBoundingBox(False, False)
        return (pcbnew.ToMM(bb.GetLeft()), pcbnew.ToMM(bb.GetRight()),
                -pcbnew.ToMM(bb.GetBottom()), -pcbnew.ToMM(bb.GetTop()))

    def record(fp):
        side = "back" if fp.GetLayer() == pcbnew.B_Cu else "front"
        occupied[side].append(spec_bbox(fp))

    def free(x0, y0, x1, y1, back):
        for fx0, fx1, fy0, fy1 in occupied["back" if back else "front"]:
            if x0 < fx1 - 0.3 and fx0 < x1 - 0.3 and \
               y0 < fy1 - 0.3 and fy0 < y1 - 0.3:
                return False
        return True

    def set_cell(fp, x0, y0, back):
        bb = fp.GetBoundingBox(False, False)
        w = pcbnew.ToMM(bb.GetWidth())
        h = pcbnew.ToMM(bb.GetHeight())
        dx = (x0 + w / 2) - pcbnew.ToMM((bb.GetLeft() + bb.GetRight()) / 2)
        dy = -(y0 + h / 2) - pcbnew.ToMM((bb.GetTop() + bb.GetBottom()) / 2)
        fp.Move(pcbnew.VECTOR2I(mm(dx), mm(dy)))
        fp.SetOrientationDegrees(0)
        if back and fp.GetLayer() == pcbnew.F_Cu:
            fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_TOP_BOTTOM)
        record(fp)

    def find_cell(rect, w, h, back):
        # deterministic: top row first, left to right, 0.5 mm grid
        cy = rect["y"] + rect["h"] - h
        while cy >= rect["y"] - 1e-9:
            cx = rect["x"]
            while cx + w <= rect["x"] + rect["w"] + 1e-9:
                if free(cx, cy, cx + w, cy + h, back):
                    return cx, cy
                cx += 0.5
            cy -= 0.5
        return None

    def pack(rect, refs, back=False):
        for ref in refs:
            fp = board.FindFootprintByReference(ref)
            if fp is None:
                print(f"WARN: packed ref {ref} not on board")
                continue
            bb = fp.GetBoundingBox(False, False)
            w, h = pcbnew.ToMM(bb.GetWidth()), pcbnew.ToMM(bb.GetHeight())
            cell = find_cell(rect, w, h, back) or find_cell(
                {"x": 0.5, "y": 0.5, "w": W - 1.0, "h": H - 1.0}, w, h, back)
            if cell is None:
                print(f"WARN: no free space for {ref}")
                continue
            if find_cell(rect, w, h, back) is None:
                print(f"WARN: {ref} spilled outside block rect")
            set_cell(fp, cell[0], cell[1], back)

    back_blocks = set(plan.get("back_blocks", []))
    for ref in placed:
        fp = board.FindFootprintByReference(ref)
        if fp is not None:
            record(fp)

    by_block = {}
    for part in parts:
        if part["ref"] not in placed:
            by_block.setdefault(part["place"], []).append(part["ref"])

    for block_name, rect in plan.get("blocks", {}).items():
        pack(rect, sorted(by_block.pop(block_name, [])),
             back=block_name in back_blocks)

    for block_name, refs in sorted(by_block.items()):
        pack({"x": 0.5, "y": 0.5, "w": W - 1.0, "h": H - 1.0}, sorted(refs))

    # --- mounting holes ------------------------------------------------------
    for i, h in enumerate(plan.get("mounting_holes", []), start=1):
        fp = pcbnew.FootprintLoad(
            "/usr/share/kicad/footprints/MountingHole.pretty",
            "MountingHole_3.2mm_M3")
        if fp is None:
            print("WARN: mounting-hole footprint missing")
            continue
        fp.SetParent(board)
        fp.SetReference(f"H{i}")
        fp.SetValue("MountingHole_3.2mm_M3")
        fp.SetPosition(pos(h["x"], h["y"]))
        board.Add(fp)

    # --- keepouts ------------------------------------------------------------
    ck_list = plan.get("copper_keepout", [])
    if isinstance(ck_list, dict):
        ck_list = [ck_list]
    for ck in ck_list:
        keepout_zone(board, ck["x0"], ck["y0"], ck["x1"], ck["y1"],
                     [pcbnew.F_Cu, pcbnew.B_Cu])
    ak = plan.get("antenna_keepout")
    if ak:
        keepout_zone(board, ak["x0"], ak["y0"], ak["x1"], ak["y1"],
                     [pcbnew.F_Cu, pcbnew.B_Cu])

    # --- net classes ----------------------------------------------------------
    netclasses = board.GetNetClasses()
    for cls, nets in NET_CLASSES.get(board_name, {}).items():
        nc = pcbnew.NETCLASS(cls)
        nc.SetClearance(mm(0.2))
        nc.SetTrackWidth(mm(0.35))
        nc.SetViaDiameter(mm(0.6))
        nc.SetViaDrill(mm(0.3))
        for net_name in nets:
            ni = board.FindNet(net_name)
            if ni is not None:
                ni.SetNetClass(nc)
        netclasses[cls] = nc
    if netclasses.has_key("Default"):
        default_nc = netclasses["Default"]
    else:
        default_nc = pcbnew.NETCLASS("Default")
        netclasses["Default"] = default_nc
    default_nc.SetClearance(mm(0.2))
    default_nc.SetTrackWidth(mm(0.35))
    default_nc.SetViaDiameter(mm(0.6))
    default_nc.SetViaDrill(mm(0.3))

    # custom rules file next to the board
    (bdir / f"{board_name}.kicad_dru").write_text(DRU_RULES)

    # --- component export for enclosure/fit check -----------------------------
    comps = []
    for fp in board.Footprints():
        ref = fp.GetReference()
        if ref.startswith("H"):
            continue
        p = fp.GetPosition()
        bb = fp.GetBoundingBox(False, False)
        pads = [[round(pcbnew.ToMM(pad.GetPosition().x), 3),
                 round(-pcbnew.ToMM(pad.GetPosition().y), 3),
                 pad.GetNumber()]
                for pad in fp.Pads()]
        comps.append({
            "ref": ref,
            "value": fp.GetValue(),
            "footprint": str(fp.GetFPID().GetLibItemName()),
            "x": round(pcbnew.ToMM(p.x), 3),
            "y": round(-pcbnew.ToMM(p.y), 3),
            "rot": fp.GetOrientation().AsDegrees(),
            "side": "front" if fp.GetLayer() == pcbnew.F_Cu else "back",
            "pads": pads,
            "bbox": {
                "x0": round(pcbnew.ToMM(bb.GetLeft()), 3),
                "x1": round(pcbnew.ToMM(bb.GetRight()), 3),
                "y0": round(-pcbnew.ToMM(bb.GetBottom()), 3),
                "y1": round(-pcbnew.ToMM(bb.GetTop()), 3),
            },
        })
    (bdir / f"components_{board_name}.json").write_text(
        json.dumps(comps, indent=1))

    board.Save(str(bdir / f"{board_name}.placed.kicad_pcb"))
    print(f"placed {len(comps)} components on {board_name} "
          f"({W}x{H} mm, {len(plan.get('fixed', {}))} fixed)")


if __name__ == "__main__":
    main()
