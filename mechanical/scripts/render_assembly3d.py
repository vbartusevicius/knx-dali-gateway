"""Renders and assembly STEP of case, boards and semi-realistic 3D components (VTK, offscreen).

    python3 mechanical/scripts/render_assembly3d.py

Component volumes come from enclosure_features.part_boxes (the same boxes the
collision tests check), rendered with realistic package geometry and materials
(aluminium electrolytic cans, XIAO sockets + PCB + RF shield + USB-C, green
KF142R push-in terminal with orange push-buttons, white SO-6L optocouplers with
leads, DPAK with heatsink tab, ceramic caps with silver end caps, resistors,
diodes with cathode bands, ZIF FFC connectors with latches, etc.).
Writes mechanical/build/<variant>/assembly.step and renders/*.png.
"""

import sys
from pathlib import Path

import cadquery as cq
import vtk

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "mechanical/scripts"))
import build_enclosure as be
import enclosure_features as ef
import legend_color_model as lcm
import oled_page1_bitmap as oled_bmp
from build_enclosure import box, cylinder

OUT = REPO / "mechanical/build" / ef.VARIANT
RENDERS = OUT / "renders"

CASE = (0.82, 0.82, 0.84)
MAIN = (0.10, 0.48, 0.20)
UI = (0.12, 0.30, 0.68)
PART = (0.18, 0.18, 0.20)
SUPPORT = (0.60, 0.20, 0.80)
PLUG = (0.85, 0.15, 0.15)
SILVER = (0.78, 0.80, 0.82)
ALU = (0.82, 0.84, 0.87)
GOLD = (0.85, 0.72, 0.25)
COPPER = (0.78, 0.48, 0.25)
CERAMIC = (0.72, 0.54, 0.32)
IVORY = (0.92, 0.90, 0.84)
GREEN_TERM = (0.12, 0.58, 0.28)
ORANGE_BTN = (0.95, 0.45, 0.10)
BLUE_CAP = (0.15, 0.38, 0.78)


def board_plates():
    plates = []
    for board in ef.SPEC["variants"][ef.VARIANT]["boards"]:
        slabs = ef.board_slabs(board)
        plate = box(slabs[0]["x0"], slabs[0]["x1"], slabs[0]["y0"], slabs[0]["y1"], slabs[0]["z0"], slabs[0]["z1"])
        for s in slabs[1:]:
            plate = plate.union(box(s["x0"], s["x1"], s["y0"], s["y1"], s["z0"], s["z1"]))
        plates.append((plate, UI if board == "ui" else MAIN))
    return plates


def two_terminal_smd(x0, x1, y0, y1, z0, z1, body_rgb, band_rgb=None):
    """SMD two-terminal passive or diode: silver end caps + coloured middle body
    (plus an optional cathode band on one side)."""
    dx, dy, dz = x1 - x0, y1 - y0, z1 - z0
    items = []
    # Long axis is whichever of y or z (on perpendicular main) or x or y (on parallel boards) is longest
    if dy >= max(dx, dz):
        cap = min(0.45, dy * 0.24)
        items.append((box(x0, x1, y0, y0 + cap, z0, z1), SILVER))
        items.append((box(x0, x1, y1 - cap, y1, z0, z1), SILVER))
        items.append((box(x0, x1, y0 + cap, y1 - cap, z0, z1), body_rgb))
        if band_rgb:
            bw = (y1 - y0) * 0.14
            items.append((box(x0 - 0.02, x1 + 0.02, y0 + cap + 0.15, y0 + cap + 0.15 + bw, z0 - 0.02, z1 + 0.02), band_rgb))
    elif dz >= max(dx, dy):
        cap = min(0.45, dz * 0.24)
        items.append((box(x0, x1, y0, y1, z0, z0 + cap), SILVER))
        items.append((box(x0, x1, y0, y1, z1 - cap, z1), SILVER))
        items.append((box(x0, x1, y0, y1, z0 + cap, z1 - cap), body_rgb))
        if band_rgb:
            bw = (z1 - z0) * 0.14
            items.append((box(x0 - 0.02, x1 + 0.02, y0 - 0.02, y1 + 0.02, z0 + cap + 0.15, z0 + cap + 0.15 + bw), band_rgb))
    else:
        cap = min(0.45, dx * 0.24)
        items.append((box(x0, x0 + cap, y0, y1, z0, z1), SILVER))
        items.append((box(x1 - cap, x1, y0, y1, z0, z1), SILVER))
        items.append((box(x0 + cap, x1 - cap, y0, y1, z0, z1), body_rgb))
        if band_rgb:
            bw = (x1 - x0) * 0.14
            items.append((box(x0 + cap + 0.15, x0 + cap + 0.15 + bw, y0 - 0.02, y1 + 0.02, z0 - 0.02, z1 + 0.02), band_rgb))
    return items


def realistic_part(b, comp):
    """Build semi-realistic multi-colour 3D solids for a component box `b`."""
    board, ref = b["board"], b["ref"]
    x0, x1, y0, y1, z0, z1 = b["x0"], b["x1"], b["y0"], b["y1"], b["z0"], b["z1"]
    cx, cy, cz = (x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2
    fp = comp["footprint"] if comp else ""
    perp = ef.SPEC["boards"][board].get("plane") == "perpendicular-to-rail"

    if b.get("shape") == "horizontal_capacitor":
        return [(be.capacitor_body(b), ALU)]
    if ref == "OLED glass":
        return [(box(x0, x1, y0, y1, z0, z1), (0.04, 0.06, 0.10))]
    if ref == "OLED board":
        return [(box(x0, x1, y0, y1, z0, z1), (0.15, 0.35, 0.22))]
    if board == "ui" and ref in ("D1", "D2", "D3"):
        # White PLCC-4 body + glowing green RGB lens
        return [(box(x0 + 0.2, x1 - 0.2, y0 + 0.2, y1 - 0.2, z0, z0 + 0.55), IVORY),
                (cylinder(cx, cy, z0 + 0.50, z0 + 0.68, 1.1), (0.10, 0.90, 0.25))]
    if board == "ui" and ref in ("SW1", "SW2"):
        base_h = 1.4 if ef.VARIANT in ("v3", "v4") else 3.5
        cap_d = 2.2 if ef.VARIANT in ("v3", "v4") else 3.5
        return [(box(x0 + 0.2, x1 - 0.2, y0 + 0.2, y1 - 0.2, z0, z0 + base_h * 0.75), PART),
                (box(x0 + 0.3, x1 - 0.3, y0 + 0.3, y1 - 0.3, z0 + base_h * 0.75, z0 + base_h), SILVER),
                (cylinder(cx, cy, z0 + base_h, be.STACK.prog_cap_z, cap_d), (0.10, 0.10, 0.12))]
    if (board, ref) in ef.PLUG_REFS:
        items = []
        for px, py, _ in ef.components(board)[ref]["pads"]:
            ex, ey, ez = ef.board_point(board, px, py)
            pin = cq.Workplane("YZ", origin=(ex, 0, 0)).center(ey, ez).circle(0.5).extrude(9.9) if perp                 else cylinder(ex, ey, ez, ez + 9.9, 1.0)
            items.append((pin, GOLD))
        neg_col = (0.15, 0.15, 0.17) if ref == "J1" else (0.90, 0.90, 0.92)
        pos_col = PLUG if ref == "J1" else (0.95, 0.80, 0.10)
        if perp:
            mz = (z0 + z1) / 2
            pos_body = box(x0, x1, y0, y1, z0, mz).union(
                be.dovetail_zy(z0, y1 - 3.15, x0, x1, neck=2.4, flare=3.2, depth=0.95, sign=-1))
            neg_body = box(x0, x1, y0, y1, mz, z1)
        else:
            mx = (x0 + x1) / 2
            neg_body = box(x0, mx, y0, y1, z0, z1)
            pos_body = box(mx, x1, y0, y1, z0, z1).union(
                be.dovetail_xy(x1, y1 - 3.15, z0, z1, neck=2.4, flare=3.2, depth=0.95))
        return [*items, (neg_body, neg_col), (pos_body, pos_col)]

    # KF142R-5.08-2P / MX126 terminal block
    if "KF142R" in fp or "MX126" in fp:
        if perp:
            housing = box(x0, x1, y0, y1 - 0.8, z0 + 0.5, z1 - 0.5)
            btns = [box(x0 + 1.5, x0 + 4.5, y1 - 1.8, y1, z - 0.9, z + 0.9) for z in (cz - 2.54, cz + 2.54)]
            holes = [cq.Workplane("XZ").center((x0 + x1) / 2 + 1.5, z).circle(1.3).extrude(4.0).translate((0, y1, 0))
                     for z in (cz - 2.54, cz + 2.54)]
            for h in holes:
                housing = housing.cut(h)
            return [(housing, GREEN_TERM)] + [(bt, ORANGE_BTN) for bt in btns]
        return [(box(x0, x1, y0, y1, z0, z1), GREEN_TERM)]

    # SMD 470 uF electrolytic capacitor (CP_Elec_10x10): black base + silver cylindrical can
    if "CP_Elec" in fp:
        if perp:
            base_plate = box(x0, x0 + 1.8, y0 + 0.6, y1 - 0.6, z0 + 0.6, z1 - 0.6)
            can = cq.Workplane("YZ", origin=(x0 + 1.8, 0, 0)).center(cy, cz).circle(4.9).extrude(x1 - x0 - 2.0)
            mark = box(x1 - 0.3, x1 - 0.1, cy - 3.5, cy + 3.5, z0 + 1.2, cz - 1.5)
            return [(base_plate, PART), (can, ALU), (mark, PART)]
        base_plate = box(x0 + 0.6, x1 - 0.6, y0 + 0.6, y1 - 0.6, z0, z0 + 1.8)
        can = cylinder(cx, cy, z0 + 1.8, z1 - 0.2, 9.8)
        return [(base_plate, PART), (can, ALU)]

    # XIAO ESP32-C6 on female sockets: black socket strips + green XIAO PCB + silver RF shield + USB-C
    if "XIAO_ESP32C6" in fp:
        if perp:
            sock1 = box(x0, x0 + 7.5, y0 + 0.6, y0 + 3.1, z0 + 1.8, z1 - 1.8)
            sock2 = box(x0, x0 + 7.5, y1 - 3.1, y1 - 0.6, z0 + 1.8, z1 - 1.8)
            pcb = box(x0 + 7.5, x0 + 8.9, y0 + 0.4, y1 - 0.4, z0 + 0.8, z1 - 0.8)
            shield = box(x0 + 8.9, x0 + 10.6, cy - 5.2, cy + 5.2, cz - 4.5, cz + 5.5)
            usbc = box(x0 + 8.5, x1, cy - 4.5, cy + 4.5, z1 - 7.5, z1 - 0.2)
            return [(sock1, PART), (sock2, PART), (pcb, (0.08, 0.32, 0.16)), (shield, SILVER), (usbc, ALU)]
        pcb = box(x0 + 0.5, x1 - 0.5, y0 + 0.5, y1 - 0.5, z1 - 3.5, z1 - 2.1)
        shield = box(cx - 5.2, cx + 5.2, cy - 5.2, cy + 5.2, z1 - 2.1, z1 - 0.3)
        return [(box(x0 + 0.5, x0 + 3.0, y0 + 1.5, y1 - 1.5, z0, z1 - 3.5), PART),
                (box(x1 - 3.0, x1 - 0.5, y0 + 1.5, y1 - 1.5, z0, z1 - 3.5), PART),
                (pcb, (0.08, 0.32, 0.16)), (shield, SILVER)]

    # REM3 DIP-24 isolated DC-DC converter: matte black module + silver pins + top label
    if "REM3" in fp:
        if perp:
            body = box(x0 + 0.4, x1, y0 + 0.4, y1 - 0.4, z0 + 0.4, z1 - 0.4)
            lbl = box(x1 - 0.05, x1 + 0.02, y0 + 3.0, y1 - 3.0, z0 + 3.0, z1 - 3.0)
            return [(body, (0.12, 0.12, 0.14)), (lbl, (0.28, 0.28, 0.32))]
        return [(box(x0 + 0.4, x1 - 0.4, y0 + 0.4, y1 - 0.4, z0, z1), (0.12, 0.12, 0.14))]

    # 2x2 supply jumper JP1: black base + gold pins + blue jumper caps
    if "PinHeader_2x02" in fp:
        if perp:
            base_h = box(x0, x0 + 2.5, y0 + 0.5, y1 - 0.5, z0 + 0.5, z1 - 0.5)
            cap1 = box(x0 + 2.5, x1 - 0.5, cy + 0.2, y1 - 0.6, z0 + 0.6, z1 - 0.6)
            cap2 = box(x0 + 2.5, x1 - 0.5, y0 + 0.6, cy - 0.2, z0 + 0.6, z1 - 0.6)
            return [(base_h, PART), (cap1, BLUE_CAP), (cap2, BLUE_CAP)]
        return [(box(x0 + 0.5, x1 - 0.5, y0 + 0.5, y1 - 0.5, z0, z0 + 2.5), PART),
                (box(x0 + 0.6, x1 - 0.6, y0 + 0.6, cy - 0.2, z0 + 2.5, z1 - 0.5), BLUE_CAP),
                (box(x0 + 0.6, x1 - 0.6, cy + 0.2, y1 - 0.6, z0 + 2.5, z1 - 0.5), BLUE_CAP)]

    # SO-6L optocouplers (TLP383): ivory body + silver gull-wing leads
    if "SO-6L" in fp:
        if perp:
            if y1 - y0 > z1 - z0:
                leads = box(x0, x0 + 0.5, y0 + 0.4, y1 - 0.4, z0 + 0.3, z1 - 0.3)
                body = box(x0 + 0.2, x1, y0 + 2.0, y1 - 2.0, z0 + 0.3, z1 - 0.3)
            else:
                leads = box(x0, x0 + 0.5, y0 + 0.3, y1 - 0.3, z0 + 0.4, z1 - 0.4)
                body = box(x0 + 0.2, x1, y0 + 0.3, y1 - 0.3, z0 + 2.0, z1 - 2.0)
            return [(leads, SILVER), (body, IVORY)]
        return [(box(x0 + 0.4, x1 - 0.4, y0 + 0.4, y1 - 0.4, z0, z0 + 0.4), SILVER),
                (box(x0 + 1.5, x1 - 1.5, y0 + 0.4, y1 - 0.4, z0 + 0.2, z1), IVORY)]

    # TO-252 DPAK pass transistor (MJD45H11): copper heatsink tab + black body + silver leads
    if "TO-252" in fp:
        if perp:
            tab = box(x0, x0 + 0.5, y0 + 0.5, y1 - 0.5, z0 + 0.5, cz + 1.5)
            body = box(x0 + 0.3, x1, y0 + 0.7, y1 - 0.7, z0 + 1.5, cz + 2.5)
            leads = box(x0, x0 + 0.6, y0 + 1.0, y1 - 1.0, cz + 2.5, z1 - 0.5)
            return [(tab, COPPER), (body, PART), (leads, SILVER)]
        return [(box(x0 + 0.4, x1 - 0.4, y0 + 0.4, y1 - 0.4, z0, z0 + 0.5), COPPER),
                (box(x0 + 1.2, x1 - 1.2, y0 + 0.6, y1 - 0.6, z0 + 0.3, z1), PART)]

    # ZIF / FFC / JST connectors: ivory body + dark flip latch
    if "FPC_" in fp or "JST_SH" in fp:
        return [(box(x0 + 0.2, x1 - 0.2, y0 + 0.2, y1 - 0.2, z0, z1), IVORY),
                (box(x0 + 0.3, x1 - 0.3, cy - 0.6, cy + 0.6, z0 - 0.05, z1 + 0.05), (0.28, 0.22, 0.18))]

    # Shielded power inductors (L1, L2): dark ferrite core + silver base pads
    if fp.startswith("L_"):
        return [(box(x0 + 0.2, x1 - 0.2, y0 + 0.2, y1 - 0.2, z0 + 0.2, z1 - 0.2), (0.25, 0.26, 0.28)),
                (box(x0 + 0.1, x1 - 0.1, y0 + 0.3, y1 - 0.3, z0, z0 + 0.3), SILVER)]

    # Crystal (Y1, 3225): gold ceramic base + silver metal lid
    if "Crystal" in fp:
        return [(box(x0 + 0.1, x1 - 0.1, y0 + 0.2, y1 - 0.2, z0 + 0.2, z1 - 0.2), GOLD),
                (box(x0, x1, y0 + 0.35, y1 - 0.35, z0 + 0.35, z1 - 0.35), SILVER)]

    # QFN-40 (NCN5130) / SOT / TSOT / SC-70 / Bridge ICs
    if any(k in fp for k in ("QFN", "SOT", "TSOT", "Diode_Bridge")):
        return [(box(x0, x1, y0 + 0.15, y1 - 0.15, z0 + 0.15, z1 - 0.15), SILVER),
                (box(x0, x1, y0 + 0.45, y1 - 0.45, z0 + 0.45, z1 - 0.45), (0.14, 0.14, 0.16))]

    # PTC Fuse 1812 (F1): gold end caps + dark green body
    if "Fuse_" in fp:
        return two_terminal_smd(x0, x1, y0, y1, z0, z1, (0.12, 0.38, 0.22))

    # SMD Diodes (SMA / SOD-123): black body + silver cathode band + silver terminals
    if fp.startswith("D_"):
        return two_terminal_smd(x0, x1, y0, y1, z0, z1, (0.14, 0.14, 0.16), band_rgb=SILVER)

    # SMD Resistors: silver end caps + black resistive film
    if fp.startswith("R_"):
        return two_terminal_smd(x0, x1, y0, y1, z0, z1, (0.15, 0.15, 0.18))

    # SMD Ceramic Capacitors: silver end caps + tan ceramic body
    if fp.startswith("C_"):
        return two_terminal_smd(x0, x1, y0, y1, z0, z1, CERAMIC)

    return [(box(x0, x1, y0, y1, z0, z1), PART)]


def part_solids():
    by_board = {b: ef.components(b) for b in ef.SPEC["variants"][ef.VARIANT]["boards"]}
    solids = []
    for b in ef.part_boxes():
        comp = by_board.get(b["board"], {}).get(b["ref"])
        for shape, rgb in realistic_part(b, comp):
            solids.append((shape, rgb, b["board"], b["ref"]))
    return solids


def support_solids():
    bodies = (be.leg_bodies() + be.screw_tab_bodies() + be.peg_bodies() + be.standoff_bodies()
              + be.snap_bodies() + be.baffle_bodies() + be.capacitor_cradles())
    return [(b, SUPPORT) for b in bodies]


def actor(shape, rgb, opacity):
    vertices, faces = shape.val().tessellate(0.1, 0.2)
    points = vtk.vtkPoints()
    for v in vertices:
        points.InsertNextPoint(v.x, v.y, v.z)
    polys = vtk.vtkCellArray()
    for face in faces:
        polys.InsertNextCell(3, face)
    data = vtk.vtkPolyData()
    data.SetPoints(points)
    data.SetPolys(polys)
    normals = vtk.vtkPolyDataNormals()
    normals.SetInputData(data)
    normals.SetFeatureAngle(35)
    mapper = vtk.vtkPolyDataMapper()
    mapper.SetInputConnection(normals.GetOutputPort())
    a = vtk.vtkActor()
    a.SetMapper(mapper)
    a.GetProperty().SetColor(*rgb)
    a.GetProperty().SetOpacity(opacity)
    a.GetProperty().SetSpecular(0.15)
    return a


def label(text, position):
    a = vtk.vtkBillboardTextActor3D()
    a.SetInput(text)
    a.SetPosition(*position)
    a.GetTextProperty().SetFontSize(15)
    a.GetTextProperty().SetColor(0.85, 0.1, 0.1)
    a.GetTextProperty().SetBold(True)
    return a


def oled_pixels_actor(z_offset=0.0):
    """Build a crisp 3D VTK quad-mesh actor for the lit Page 1 OLED pixels inside oled_window()."""
    win = ef.oled_window()
    glass = next(b for b in ef.part_boxes() if b["ref"] == "OLED glass")
    rows = oled_bmp.PAGE1_64X128 if ef.VARIANT in ("v3", "v4") else oled_bmp.PAGE1_128X64
    h = len(rows)
    w = len(rows[0])
    x0 = win["cx"] - win["w"] / 2
    y1 = win["cy"] + win["h"] / 2
    pw = win["w"] / w
    ph = win["h"] / h
    z = glass["z1"] + 0.08 + z_offset

    points = vtk.vtkPoints()
    polys = vtk.vtkCellArray()
    idx = 0
    eps = 0.003
    for py, row in enumerate(rows):
        py1 = y1 - py * ph + eps
        py0 = y1 - (py + 1) * ph - eps
        px = 0
        while px < w:
            if row[px] != "#":
                px += 1
                continue
            end_px = px + 1
            while end_px < w and row[end_px] == "#":
                end_px += 1
            px0 = x0 + px * pw - eps
            px1 = x0 + end_px * pw + eps
            points.InsertNextPoint(px0, py0, z)
            points.InsertNextPoint(px1, py0, z)
            points.InsertNextPoint(px1, py1, z)
            points.InsertNextPoint(px0, py1, z)
            polys.InsertNextCell(3, (idx, idx + 1, idx + 2))
            polys.InsertNextCell(3, (idx, idx + 2, idx + 3))
            idx += 4
            px = end_px

    data = vtk.vtkPolyData()
    data.SetPoints(points)
    data.SetPolys(polys)
    mapper = vtk.vtkPolyDataMapper()
    mapper.SetInputData(data)
    a = vtk.vtkActor()
    a.SetMapper(mapper)
    rgb = (0.29, 0.87, 0.50) if ef.VARIANT in ("v3", "v4") else (0.22, 0.74, 0.97)
    a.GetProperty().SetColor(*rgb)
    a.GetProperty().SetAmbient(1.0)
    a.GetProperty().SetDiffuse(0.0)
    return a


def render(name, items, position, focal=(0, 40.5, 25), up=(0, 1, 0), zoom=1.0, labels=(), extra_actors=()):
    renderer = vtk.vtkRenderer()
    renderer.SetBackground(1, 1, 1)
    renderer.SetUseDepthPeeling(1)
    renderer.SetMaximumNumberOfPeels(30)
    for shape, rgb, opacity in items:
        renderer.AddActor(actor(shape, rgb, opacity))
    for a in extra_actors:
        renderer.AddActor(a)
    for text, pos in labels:
        renderer.AddActor(label(text, pos))
    window = vtk.vtkRenderWindow()
    window.SetOffScreenRendering(1)
    window.SetAlphaBitPlanes(1)
    window.SetMultiSamples(0)
    window.SetSize(1400, 1400)
    window.AddRenderer(renderer)
    camera = renderer.GetActiveCamera()
    camera.SetPosition(*position)
    camera.SetFocalPoint(*focal)
    camera.SetViewUp(*up)
    renderer.ResetCamera()
    camera.Zoom(zoom)
    window.Render()
    grab = vtk.vtkWindowToImageFilter()
    grab.SetInput(window)
    grab.Update()
    writer = vtk.vtkPNGWriter()
    writer.SetFileName(str(RENDERS / f"{name}.png"))
    writer.SetInputConnection(grab.GetOutputPort())
    writer.Write()
    print(f"{name}.png saved")


def board_views(plates, full_parts):
    """Top and bottom of each board with its realistic 3D parts, every part labelled."""
    boards_list = ef.SPEC["variants"][ef.VARIANT]["boards"]
    for board, plate in zip(boards_list, plates):
        boxes = [b for b in ef.part_boxes() if b["board"] == board]
        mine = [(s, c, 1.0) for s, c, b_name, _ in full_parts if b_name == board]
        bb = plate[0].val().BoundingBox()
        cx, cy, cz = (bb.xmin + bb.xmax) / 2, (bb.ymin + bb.ymax) / 2, (bb.zmin + bb.zmax) / 2
        perp = ef.SPEC["boards"][board].get("plane") == "perpendicular-to-rail"
        for side, d in (("top", 1), ("bottom", -1)):
            if perp:
                on_side = [b for b in boxes if (b["x0"] >= bb.xmax - 0.01) == (side == "top")]
                labels = [(b["ref"], (b["x1"] + 0.5 if side == "top" else b["x0"] - 0.5,
                                      (b["y0"] + b["y1"]) / 2, (b["z0"] + b["z1"]) / 2)) for b in on_side]
                cam = ((bb.xmax if side == "top" else bb.xmin) + d * 250, cy, cz)
                foc = (cx, cy, cz)
            else:
                on_side = [b for b in boxes if (b["z0"] >= bb.zmax - 0.01) == (side == "top")]
                labels = [(b["ref"], ((b["x0"] + b["x1"]) / 2, (b["y0"] + b["y1"]) / 2,
                                      b["z1"] + 0.5 if side == "top" else b["z0"] - 0.5)) for b in on_side]
                cam = (cx, cy, (bb.zmax if side == "top" else bb.zmin) + d * 250)
                foc = (cx, cy, bb.zmin)
            render(f"{board}_{side}", [(plate[0], plate[1], 1.0)] + mine, cam, focal=foc, zoom=1.0, labels=labels)


def shifted(items, dz):
    return [(s.translate((0, 0, dz)), rgb, op) for s, rgb, op in items]


def main():
    RENDERS.mkdir(parents=True, exist_ok=True)
    base, cover = be.build_base(), be.build_cover()
    plates = board_plates()
    assembly = base.union(cover)
    for plate, _ in plates:
        assembly = assembly.union(plate)
    cq.exporters.export(assembly, str(OUT / "assembly.step"), cq.exporters.ExportTypes.STEP)
    print("assembly.step saved")

    full_parts = part_solids()
    parts = [(s, c, 1.0) for s, c, _, _ in full_parts]
    supports = [(s, c, 1.0) for s, c in support_solids()]
    boards = [(s, c, 1.0) for s, c in plates]
    led_lenses = [
        (cylinder(w["x"], w["y"], ef.TOTAL_D - 0.6, ef.TOTAL_D + 0.02, be.LED_WINDOW), (0.10, 0.88, 0.32), 1.0)
        for w in ef.led_windows()
    ]
    inside = boards + parts + supports
    legend = lcm.legend_bodies()
    cover_silk = cover.cut(legend)
    SILK = (0.10, 0.10, 0.12)
    iso = (150, 150, 190)
    oled_act = oled_pixels_actor(0.0)
    render("front", [(base, CASE, 1.0), (cover_silk, CASE, 1.0), (legend, SILK, 1.0)] + inside + led_lenses,
           (0, 40.5, 300), zoom=1.1, extra_actors=(oled_act,))
    render("iso", [(base, CASE, 1.0), (cover_silk, CASE, 1.0), (legend, SILK, 1.0)] + inside + led_lenses,
           iso, extra_actors=(oled_act,))
    render("inside", [(base, CASE, 0.12), (cover, CASE, 0.12)] + inside, iso, extra_actors=(oled_act,))
    keep = box(-40, 0, -20, 100, -20, 80)
    section = [(base.intersect(keep), CASE, 1.0), (cover.intersect(keep), CASE, 1.0)] + inside
    render("section", section, (220, 40.5, 25))
    render("back", [(base, CASE, 1.0)], (60, -60, -200), focal=(0, 40.5, 0))
    main_layer = [b for b in boards if b[1] == MAIN] + [p for p in parts if p[0].val().BoundingBox().zmax < 45]         + [s for s in supports if s[0].val().BoundingBox().zmax < 45]
    ui_layer = [b for b in boards if b[1] == UI] + [p for p in parts if p[0].val().BoundingBox().zmax >= 45]         + [s for s in supports if s[0].val().BoundingBox().zmax >= 45]
    exploded = (shifted([(base, CASE, 1.0)], -35) + main_layer + shifted(ui_layer, 25)
                + shifted([(cover_silk, CASE, 0.35), (legend, SILK, 0.85)], 55))
    render("exploded", exploded, (190, 140, 220), focal=(0, 40.5, 25), extra_actors=(oled_pixels_actor(25.0),))
    board_views(plates, full_parts)


if __name__ == "__main__":
    main()
