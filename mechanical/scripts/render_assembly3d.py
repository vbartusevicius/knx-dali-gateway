"""Renders and assembly STEP of case, boards and parts (VTK, offscreen).

    python3 mechanical/scripts/render_assembly3d.py

Parts are the same volumes the collision tests use (enclosure_features
.part_boxes), so the pictures show what the tests check. Supports (legs,
screw tabs, pegs, standoffs, UI snap tongues) are purple, the KNX plug red.
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
from build_enclosure import box, cylinder

OUT = REPO / "mechanical/build" / ef.VARIANT
RENDERS = OUT / "renders"
CASE, MAIN, UI, PART, SUPPORT, PLUG = ((0.82, 0.82, 0.84), (0.1, 0.5, 0.2), (0.12, 0.3, 0.7),
                                       (0.22, 0.22, 0.24), (0.6, 0.2, 0.8), (0.85, 0.15, 0.15))
LED_COLOURS = {"D1": (0.1, 0.85, 0.2), "D2": (0.1, 0.85, 0.2), "D3": (0.1, 0.85, 0.2)}
COLOURS = {("ui", "OLED glass"): (0.02, 0.02, 0.05), ("ui", "OLED board"): (0.1, 0.2, 0.5),
           ("main", "J2"): (0.15, 0.6, 0.3), ("main", "J3"): (0.15, 0.6, 0.3), ("main", "U7"): (0.05, 0.05, 0.05)}


def board_plates():
    plates = []
    for board in ef.SPEC["variants"][ef.VARIANT]["boards"]:
        config = ef.SPEC["boards"][board]
        w, h = config["max_outline"].values()
        z0 = be.UI_BOARD_Z if board == "ui" else config["z"] - config["thickness"]
        (x0, y0), (x1, y1) = ef.board_to_enc(board, 0, 0), ef.board_to_enc(board, w, h)
        plate = box(x0, x1, y0, y1, z0, z0 + config["thickness"])
        plates.append((plate, UI if board == "ui" else MAIN))
    return plates


def part_solids():
    solids = []
    for b in ef.part_boxes():
        ref = b["ref"]
        cx, cy = (b["x0"] + b["x1"]) / 2, (b["y0"] + b["y1"]) / 2
        if b.get("shape") == "horizontal_capacitor":
            solids.append((be.capacitor_body(b), (0.35, 0.35, 0.38)))
        elif b["board"] == "ui" and ref in LED_COLOURS:
            solids.append((box(b["x0"], b["x1"], b["y0"], b["y1"], b["z0"], b["z1"]), LED_COLOURS[ref]))
        elif b["board"] == "ui" and ref in ("SW1", "SW2"):
            solids.append((box(cx - 3, cx + 3, cy - 3, cy + 3, b["z0"], b["z0"] + 3.5), PART))
            solids.append((cylinder(cx, cy, b["z0"] + 3.5, b["z1"], 3.5), (0.1, 0.1, 0.1)))
        elif b["board"] == "main" and ref == "J1":
            solids.append((box(b["x0"], b["x1"], b["y0"], b["y1"], b["z0"], b["z1"]), PLUG))
        else:
            colour = COLOURS.get((b["board"], ref), PART)
            solids.append((box(b["x0"], b["x1"], b["y0"], b["y1"], b["z0"], b["z1"]), colour))
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


def render(name, items, position, focal=(0, 40.5, 25), up=(0, 1, 0), zoom=1.0, labels=()):
    renderer = vtk.vtkRenderer()
    renderer.SetBackground(1, 1, 1)
    renderer.SetUseDepthPeeling(1)
    renderer.SetMaximumNumberOfPeels(30)
    for shape, rgb, opacity in items:
        renderer.AddActor(actor(shape, rgb, opacity))
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


def board_views(plates, parts):
    """Top and bottom of each board with its parts, every part labelled."""
    for board, plate in zip(("main", "ui"), plates):
        boxes = [b for b in ef.part_boxes() if b["board"] == board]
        mine = [(s, c, 1.0) for (s, c), b in zip(parts, part_boxes_matching(boxes, parts)) if b]
        bb = plate[0].val().BoundingBox()
        cx, cy = (bb.xmin + bb.xmax) / 2, (bb.ymin + bb.ymax) / 2
        for side, dz in (("top", 1), ("bottom", -1)):
            on_side = [b for b in boxes if (b["z0"] >= bb.zmax - 0.01) == (side == "top")]
            labels = [(b["ref"], ((b["x0"] + b["x1"]) / 2, (b["y0"] + b["y1"]) / 2,
                                  b["z1"] + 0.5 if side == "top" else b["z0"] - 0.5)) for b in on_side]
            render(f"{board}_{side}", [(plate[0], plate[1], 1.0)] + mine,
                   (cx, cy, (bb.zmax if side == "top" else bb.zmin) + dz * 250), focal=(cx, cy, bb.zmin),
                   zoom=1.0, labels=labels)


def part_boxes_matching(boxes, parts):
    """Which entries of `parts` (built from all part boxes in order) belong to
    the given boxes; LEDs and buttons produce more than one solid."""
    refs = {(b["board"], b["ref"]) for b in boxes}
    owners = []
    for b in ef.part_boxes():
        count = 2 if b["board"] == "ui" and b["ref"] in ("SW1", "SW2") else 1
        owners += [(b["board"], b["ref"]) in refs] * count
    return owners


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

    parts = [(s, c, 1.0) for s, c in part_solids()]
    supports = [(s, c, 1.0) for s, c in support_solids()]
    boards = [(s, c, 1.0) for s, c in plates]
    inside = boards + parts + supports
    iso = (150, 150, 190)
    render("front", [(base, CASE, 1.0), (cover, CASE, 1.0)] + inside, (0, 40.5, 300), zoom=1.1)
    render("iso", [(base, CASE, 1.0), (cover, CASE, 1.0)] + inside, iso)
    render("inside", [(base, CASE, 0.12), (cover, CASE, 0.12)] + inside, iso)
    render("supports", [(base, CASE, 0.08), (cover, CASE, 0.08)] + [(s, c, 0.35) for s, c, _ in boards]
           + supports, iso)
    keep = box(-40, 0, -20, 100, -20, 80)
    section = [(s.intersect(keep), c, op) for s, c, op in [(base, CASE, 1.0), (cover, CASE, 1.0)] + inside]
    render("section", section, (220, 40.5, 25))
    render("back", [(base, CASE, 1.0)], (60, -60, -200), focal=(0, 40.5, 0))
    latch = base.intersect(box(-25, 25, -10, 30, -10, 4))
    render("latch", [(latch, CASE, 1.0)], (40, -70, 80), focal=(0, 8, -2), zoom=1.3)
    recess = [(s.intersect(box(5, 30, -2, 22, 15, 40)), c, op)
              for s, c, op in [(base, CASE, 1.0), (cover, CASE, 1.0)] + boards + parts]
    render("knx_recess", recess, (60, -50, 90), focal=(18, 8, 28), zoom=1.2)
    main_layer = [b for b in boards if b[1] == MAIN] + [p for p in parts if p[0].val().BoundingBox().zmax < 45] \
        + [(s, SUPPORT, 1.0) for s in be.leg_bodies()]
    ui_layer = [b for b in boards if b[1] == UI] + [p for p in parts if p[0].val().BoundingBox().zmin > 45]
    exploded = ([(base, CASE, 1.0)] + shifted(main_layer, 0) + shifted(ui_layer, 40)
                + shifted([(cover, CASE, 1.0)], 80))
    render("exploded", exploded, (230, 160, 260), focal=(0, 40.5, 60))
    board_views(plates, part_solids())


if __name__ == "__main__":
    main()
