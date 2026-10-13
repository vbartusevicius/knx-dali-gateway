"""CAD-side STEP geometry gate tests.

Reads the exported board STEP and courtyard JSON (produced by EDA-side
test_step_export.py) and verifies that every custom 3D model's world XY
center matches its footprint's courtyard center ±0.5 mm.

Also checks the negative case: a deliberately-offset board must fail.
"""

import json
from pathlib import Path

import pytest
from board_variant import BOARDS

REPO = Path("/work")


def _read_step_solids(step_path):
    """Read STEP file and return list of (cx, cy, cz, sx, sy, sz) per solid."""
    from OCP.Bnd import Bnd_Box
    from OCP.BRepBndLib import BRepBndLib
    from OCP.STEPControl import STEPControl_Reader
    from OCP.TopAbs import TopAbs_SOLID
    from OCP.TopExp import TopExp_Explorer

    reader = STEPControl_Reader()
    reader.ReadFile(str(step_path))
    reader.TransferRoots()
    shape = reader.OneShape()

    explorer = TopExp_Explorer(shape, TopAbs_SOLID)
    solids = []
    while explorer.More():
        solid = explorer.Current()
        bbox = Bnd_Box()
        BRepBndLib.AddClose_s(solid, bbox)
        xmin, ymin, zmin, xmax, ymax, zmax = bbox.Get()
        cx = (xmax + xmin) / 2
        cy = (ymax + ymin) / 2
        cz = (zmax + zmin) / 2
        solids.append((cx, cy, cz, xmax - xmin, ymax - ymin, zmax - zmin))
        explorer.Next()
    return solids


def _find_solid_for_model(solids, model_size, expected_center, tol=5.0):
    """Find a solid matching the model size and near the expected center."""
    mw, md, _mh = model_size
    for cx, cy, cz, sx, sy, sz in solids:
        size_match = (
            (abs(sx - mw) < 1.5 and abs(sy - md) < 1.5) or
            (abs(sx - md) < 1.5 and abs(sy - mw) < 1.5))
        if not size_match:
            continue
        ex, ey = expected_center
        if abs(cx - ex) < tol and abs(cy - ey) < tol:
            return (cx, cy, cz, sx, sy, sz)
    return None


@pytest.mark.parametrize("board", BOARDS)
def test_custom_models_aligned_to_courtyard(board):
    """Every custom 3D model's world XY center must match its footprint's
    courtyard center ±0.5 mm, measured from the exported STEP geometry."""
    step_path = REPO / "hardware/build/test" / f"{board}_test.step"
    json_path = REPO / "hardware/build/test" / f"{board}_courtyards.json"

    if not step_path.exists():
        pytest.skip(f"{step_path} not found — run EDA tests first")
    if not json_path.exists():
        pytest.skip(f"{json_path} not found — run EDA tests first")

    solids = _read_step_solids(step_path)
    data = json.loads(json_path.read_text())

    deviations = []
    max_deviations = []
    for model in data["models"]:
        ref = model["ref"]
        fpid = model["fpid"]
        crt_cx = model["crt_cx"]
        crt_cy = model["crt_cy"]
        size = (model["size_w"], model["size_d"], model["size_h"])
        solid = _find_solid_for_model(solids, size, (crt_cx, crt_cy), tol=5.0)
        if solid is None:
            deviations.append(f"{ref} ({fpid}): no matching solid found")
            continue
        cx, cy, _cz, _sx, _sy, _sz = solid
        dx = abs(cx - crt_cx)
        dy = abs(cy - crt_cy)
        max_dev = max(dx, dy)
        max_deviations.append((ref, max_dev))
        if max_dev > 0.5:
            deviations.append(
                f"{ref} ({fpid}): center=({cx:.2f},{cy:.2f}) vs "
                f"courtyard=({crt_cx:.2f},{crt_cy:.2f}) "
                f"deviation=({dx:.2f},{dy:.2f})mm")
        else:
            print(f"  {ref} ({fpid}): deviation=({dx:.2f},{dy:.2f})mm ✓")

    # Print per-part max deviation
    print("\n  Per-part max deviation (custom models):")
    for ref, dev in max_deviations:
        print(f"    {ref}: {dev:.3f} mm")

    assert not deviations, (
        f"{board}: {len(deviations)} model alignment violations:\n"
        + "\n".join(deviations))


def test_negative_deliberate_offset():
    """A model with a deliberate 5 mm offset must fail the alignment check."""
    step_path = REPO / "hardware/build/test/negative_test.step"
    json_path = REPO / "hardware/build/test/negative_courtyards.json"

    if not step_path.exists():
        pytest.skip(f"{step_path} not found — run EDA negative test first")
    if not json_path.exists():
        pytest.skip(f"{json_path} not found — run EDA negative test first")

    solids = _read_step_solids(step_path)
    data = json.loads(json_path.read_text())

    # Find J2 in the courtyard data
    j2_model = next((m for m in data["models"] if m["ref"] == "J2"), None)
    assert j2_model is not None, "J2 not in courtyard JSON"

    size = (j2_model["size_w"], j2_model["size_d"], j2_model["size_h"])
    crt_cx = j2_model["crt_cx"]
    crt_cy = j2_model["crt_cy"]

    # Find the J2 solid — it should be offset by 5mm from courtyard
    solid = _find_solid_for_model(solids, size, (crt_cx, crt_cy), tol=10.0)
    assert solid is not None, "J2 solid not found in negative STEP"
    cx, cy, _cz, _sx, _sy, _sz = solid
    dx = abs(cx - crt_cx)
    dy = abs(cy - crt_cy)
    max_dev = max(dx, dy)
    assert max_dev > 0.5, (
        f"Negative test failed: deliberate 5mm offset should cause "
        f"deviation > 0.5mm, got {max_dev:.2f}mm "
        f"(center=({cx:.2f},{cy:.2f}) vs courtyard=({crt_cx:.2f},{crt_cy:.2f}))")
