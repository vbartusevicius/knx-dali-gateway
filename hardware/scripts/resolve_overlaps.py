"""Move unlocked parts off each other after a footprint change (`eda` container).

    python3 hardware/scripts/resolve_overlaps.py <board> [--write]

Places every part from layout_<board>.yaml on a fresh board, then for each
unlocked part whose courtyard overlaps a part placed before it (locked parts
first, then plan order) or leaves the board, searches outward on a 0.1 mm grid
(then with the rotation turned 90 degrees) for the nearest free spot inside the
board outline. Through-hole parts and reverse-mount parts with a board cutout
block both sides. With --write the new positions go back into the plan. DRC,
routing and the case tests still decide whether the result is good.
"""

import math
import re
import sys

import pcbnew

import layout

STEP = 0.1
MAX_RADIUS = 10.0
EDGE = 0.0   # courtyard inside the outline; DRC checks the pads' edge clearance


def mm_box(bb, grow=0.0):
    return (pcbnew.ToMM(bb.GetLeft()) - grow, pcbnew.ToMM(bb.GetTop()) - grow,
            pcbnew.ToMM(bb.GetRight()) + grow, pcbnew.ToMM(bb.GetBottom()) + grow)


def boxes_of(fp):
    """(box, side) pairs a part occupies: its courtyard on its own side; on the
    other side only its through-hole pins and any board cutout it sits in."""
    own = "back" if fp.IsFlipped() else "front"
    other = "front" if own == "back" else "back"
    items = [(mm_box(layout.body_box(fp)), own)]
    items += [(mm_box(p.GetBoundingBox(), 0.3), other) for p in fp.Pads() if p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH]
    items += [(mm_box(g.GetBoundingBox(), 0.3), other) for g in fp.GraphicalItems() if g.GetLayer() == pcbnew.Edge_Cuts]
    return items


def clash(a, side_a, b, side_b):
    return side_a == side_b and a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def hits(items, placed):
    return [r for box, side in items for c, t, r in placed if clash(box, side, c, t)]


def inside(outline, box):
    pts = [(box[0] - EDGE, box[1] - EDGE), (box[2] + EDGE, box[1] - EDGE), (box[0] - EDGE, box[3] + EDGE),
           (box[2] + EDGE, box[3] + EDGE), ((box[0] + box[2]) / 2, box[1] - EDGE), ((box[0] + box[2]) / 2, box[3] + EDGE)]
    return all(outline.Contains(pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y))) for x, y in pts)


def pad_domains(fp, regions):
    return [layout.domain_at(regions, pcbnew.ToMM(p.GetPosition().x), -pcbnew.ToMM(p.GetPosition().y))
            for p in fp.Pads()]


def keepouts(board):
    """Rule areas (copper keep-outs, case-contact keep-outs) as (box, side)."""
    out = []
    for zone in layout.zones(board):
        if not zone.GetIsRuleArea():
            continue
        box = mm_box(zone.GetBoundingBox())
        for layer, side in ((pcbnew.F_Cu, "front"), (pcbnew.B_Cu, "back")):
            if zone.IsOnLayer(layer):
                out.append((box, side, "keepout"))
    return out


def resolve(name):
    plan = layout.load_plan(name)
    board = layout.fresh_board(name)
    layout.place_parts(board, plan, list(plan["parts"]))
    layout.apply_frame(board, plan)
    outline = pcbnew.SHAPE_POLY_SET()
    board.GetBoardPolygonOutlines(outline, False)
    parts = plan["parts"]
    raised = set(plan["frame"].get("raised_modules", []))
    regions = plan["frame"].get("domain_regions", {})
    order = [r for r, p in parts.items() if p.get("locked")] + [r for r, p in parts.items() if not p.get("locked")]
    placed, moved, stuck = keepouts(board), {}, []
    for ref in order:
        fp = board.FindFootprintByReference(ref)
        if fp is None:
            continue
        items = boxes_of(fp)
        home = pad_domains(fp, regions)
        if ref in raised:   # parts may sit under a module raised on sockets, not on its pins
            placed += [(mm_box(p.GetBoundingBox(), 0.3), side, ref) for p in fp.Pads()
                       if p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH for side in ("front", "back")]
            continue
        free = inside(outline, items[0][0]) and not hits(items, placed)
        if free or parts[ref].get("locked"):
            placed += [(b, s, ref) for b, s in items]
            continue
        origin, rot0 = fp.GetPosition(), fp.GetOrientationDegrees()
        best = None
        for rot in (rot0, rot0 + 90):
            fp.SetOrientationDegrees(rot)
            for r in [STEP * i for i in range(1, int(MAX_RADIUS / STEP))]:
                steps = max(8, int(2 * math.pi * r / STEP))
                for k in range(steps):
                    a = 2 * math.pi * k / steps
                    fp.SetPosition(origin + pcbnew.VECTOR2I(pcbnew.FromMM(r * math.cos(a)), pcbnew.FromMM(r * math.sin(a))))
                    items = boxes_of(fp)
                    if inside(outline, items[0][0]) and not hits(items, placed) and \
                            pad_domains(fp, regions) == home:
                        best = (r, fp.GetPosition(), rot, items)
                        break
                if best:
                    break
            if best:
                break
        if best is None:
            fp.SetPosition(origin)
            fp.SetOrientationDegrees(rot0)
            items = boxes_of(fp)
            why = sorted(set(hits(items, placed))) or ["outline"]
            stuck.append(f"{ref} ({'/'.join(why)})")
            placed += [(b, s, ref) for b, s in items]
            continue
        _, where, rot, items = best
        fp.SetPosition(where)
        fp.SetOrientationDegrees(rot)
        placed += [(b, s, ref) for b, s in items]
        moved[ref] = (round(pcbnew.ToMM(where.x), 2), round(-pcbnew.ToMM(where.y), 2), round(rot % 360, 1))
    return plan, moved, stuck


def write_back(name, moved):
    path = layout.REPO / "hardware/circuits/variants" / layout.VARIANT / f"layout_{name}.yaml"
    text = path.read_text()
    for ref, (x, y, rot) in moved.items():
        pattern = re.compile(rf"^(\s+{re.escape(ref)}:\s*\{{\s*)x:\s*[-\d.]+,\s*y:\s*[-\d.]+,\s*rot:\s*[-\d.]+", re.MULTILINE)
        text, n = pattern.subn(rf"\g<1>x: {x}, y: {y}, rot: {rot:g}", text)
        if n != 1:
            sys.exit(f"{ref}: cannot update its line in {path.name}")
    path.write_text(text)


if __name__ == "__main__":
    name = sys.argv[1]
    _, moved, stuck = resolve(name)
    for ref, where in sorted(moved.items()):
        print(f"{name}: {ref} -> x {where[0]}, y {where[1]}, rot {where[2]}")
    print(f"{name}: moved {len(moved)}, no free spot for {stuck or 'none'}")
    if "--write" in sys.argv and moved:
        write_back(name, moved)
