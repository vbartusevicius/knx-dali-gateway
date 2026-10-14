"""EDA-side STEP export gate tests.

1. No unresolved-model warnings when exporting STEP via kicad-cli.
2. The XIAO body bottom is ≥ 8 mm above the board (raised on headers).
3. Exports courtyard centers to JSON for CAD-side geometry checks.
4. Creates a deliberately-offset board for the negative case.
"""

import json
import subprocess
from pathlib import Path

import pcbnew
import pytest
from board_variant import BOARDS, layout_path

REPO = Path("/work")
_board_path = layout_path


CUSTOM_MODELS = {
    "XIAO_ESP32C6_Header",
    "Converter_DCDC_RECOM_REM3_DIP24",
    "Converter_DCDC_RECOM_REM3_DIP24_CTRL",
    "TerminalBlock_MaiXu_MX126-5.0-02P_1x02_P5.00mm",
    "Fuse_1812_4532Metric",
    "CP_Radial_D10.0mm_P5.00mm_Horizontal_Bent",
}

MODEL_SIZES = {
    "XIAO_ESP32C6_Header": (17.8, 22.0, 3.5),
    "Converter_DCDC_RECOM_REM3_DIP24": (31.8, 20.3, 10.7),
    "Converter_DCDC_RECOM_REM3_DIP24_CTRL": (31.8, 20.3, 10.7),
    "TerminalBlock_MaiXu_MX126-5.0-02P_1x02_P5.00mm": (11.6, 9.1, 8.0),
    "Fuse_1812_4532Metric": (4.5, 3.2, 1.2),
    "Phoenix_PST_1,0-2-5,75_R32_1x02_P5.75mm": (8.55, 2.8, 9.9),
    "CP_Radial_D10.0mm_P5.00mm_Horizontal_Bent": (10.0, 19.3, 12.0),
}


def _export_step(pcb_path, step_path):
    result = subprocess.run(
        ["kicad-cli", "pcb", "export", "step", "-o", str(step_path),
         str(pcb_path)],
        capture_output=True, text=True, timeout=120, check=False)
    output = result.stdout + result.stderr
    warnings = [line for line in output.splitlines()
                if "Could not" in line or "not found" in line.lower()]
    return warnings


def _courtyard_center_3d(fp):
    """Get courtyard center in STEP 3D coords (Y up) from footprint."""
    fp.BuildCourtyardCaches()
    side_layer = pcbnew.F_CrtYd if fp.GetLayer() == pcbnew.F_Cu else pcbnew.B_CrtYd
    poly = fp.GetCourtyard(side_layer)
    if poly is None or poly.OutlineCount() == 0:
        return None
    bb = poly.BBox()
    cx = pcbnew.ToMM(bb.GetLeft()) + pcbnew.ToMM(bb.GetWidth()) / 2
    cy = -(pcbnew.ToMM(bb.GetTop()) + pcbnew.ToMM(bb.GetHeight()) / 2)
    return cx, cy


def _export_courtyard_json(board):
    """Export courtyard centers for custom models to JSON."""
    pcb = _board_path(board)
    b = pcbnew.LoadBoard(str(pcb))
    data = {"models": []}
    for fp in b.Footprints():
        fpid = str(fp.GetFPID().GetLibItemName())
        if fpid not in CUSTOM_MODELS:
            continue
        ref = fp.GetReference()
        crt = _courtyard_center_3d(fp)
        if crt is None:
            continue
        size = MODEL_SIZES[fpid]
        data["models"].append({
            "ref": ref,
            "fpid": fpid,
            "crt_cx": round(crt[0], 3),
            "crt_cy": round(crt[1], 3),
            "size_w": size[0],
            "size_d": size[1],
            "size_h": size[2],
        })
    out = REPO / "hardware/build" / "test" / f"{board}_courtyards.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(data, indent=2))
    return out


@pytest.mark.parametrize("board", BOARDS)
def test_no_unresolved_3d_models(board):
    """kicad-cli pcb export step must not report any unresolved 3D models."""
    pcb = _board_path(board)
    step = REPO / "hardware/build" / "test" / f"{board}_test.step"
    step.parent.mkdir(parents=True, exist_ok=True)
    warnings = _export_step(pcb, step)
    assert not warnings, (
        f"{board}: {len(warnings)} unresolved 3D model warnings:\n"
        + "\n".join(warnings))
    # Also export courtyard JSON for CAD-side tests
    _export_courtyard_json(board)


def test_xiao_body_raised():
    """The XIAO body bottom must be ≥ 8 mm above the board (raised on headers)."""
    xiao = [fp for board in BOARDS for fp in pcbnew.LoadBoard(str(_board_path(board))).Footprints()
            if str(fp.GetFPID().GetLibItemName()) == "XIAO_ESP32C6_Header"]
    assert len(xiao) == 1, "exactly one XIAO expected"
    models = xiao[0].Models()
    assert len(models) > 0, "the XIAO has no 3D model"
    for m in models:
        assert m.m_Offset.z == pytest.approx(11.0), \
            f"XIAO Z offset {m.m_Offset.z:.1f} mm; socket 8.5 + header plastic 2.5 = 11.0"


def test_negative_offset_board():
    """Create a deliberately-offset board for the CAD-side negative test."""
    pcb = _board_path("main")
    b = pcbnew.LoadBoard(str(pcb))
    fp = next((f for f in sorted(b.Footprints(), key=lambda f: f.GetReference()) if len(f.Models()) > 0), None)
    assert fp is not None, "no footprint on main has a 3D model"
    models = fp.Models()
    orig = models[0].m_Offset
    models[0].m_Offset = pcbnew.VECTOR3D(orig.x + 5.0, orig.y, orig.z)
    tmp_pcb = REPO / "hardware/build/test/negative_test.kicad_pcb"
    tmp_pcb.parent.mkdir(parents=True, exist_ok=True)
    b.Save(str(tmp_pcb))
    # Export STEP
    step = REPO / "hardware/build/test/negative_test.step"
    warnings = _export_step(tmp_pcb, step)
    assert not warnings, f"Unresolved models: {warnings}"
    # Export courtyard JSON (same courtyards, since we only changed model offset)
    _export_courtyard_json("main")
    # Copy to negative JSON
    import shutil
    src = REPO / "hardware/build/test/main_courtyards.json"
    dst = REPO / "hardware/build/test/negative_courtyards.json"
    shutil.copyfile(src, dst)
