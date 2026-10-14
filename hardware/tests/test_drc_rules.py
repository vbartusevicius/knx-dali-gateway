"""KiCad silently ignores a whole .kicad_dru file when one rule fails to
parse, which once switched off every custom rule. Plant a too-thin power
track on a copy of each stored board and require DRC to flag it through the
project rules."""

import json
import shutil
import subprocess

import pcbnew
import pytest
from board_variant import BOARDS, layout_path, layout_plan


def free_spot(pcb, fine_pitch=(), length=2.0, margin=0.8):
    """Start of a horizontal segment inside the board that no pad or fine-pitch
    neck-down courtyard comes near."""
    box = pcb.GetBoardEdgesBoundingBox()
    for fp in pcb.Footprints():
        fp.BuildCourtyardCaches()
    pads = [p.GetBoundingBox() for p in pcb.GetPads()] + [
        fp.GetCourtyard(pcbnew.B_CrtYd if fp.IsFlipped() else pcbnew.F_CrtYd).BBox()
        for fp in pcb.Footprints() if fp.GetReference() in fine_pitch]
    step = pcbnew.FromMM(0.5)
    grow = pcbnew.FromMM(margin)
    for y in range(box.GetY() + grow, box.GetBottom() - grow, step):
        for x in range(box.GetX() + grow, box.GetRight() - grow - pcbnew.FromMM(length), step):
            seg = pcbnew.BOX2I(pcbnew.VECTOR2I(x, y), pcbnew.VECTOR2L(pcbnew.FromMM(length), 0))
            seg.Inflate(grow)
            if not any(seg.Intersects(p) for p in pads):
                return x, y
    pytest.fail("no pad-free spot for the probe track")


@pytest.mark.parametrize("board", BOARDS)
def test_project_rules_are_enforced(tmp_path, board):
    nets = layout_plan(board)["frame"]["power_nets"]
    if not nets:
        pytest.skip("board has no power-class nets")
    net = nets[0]
    src = layout_path(board).parent
    for suffix in (".kicad_pcb", ".kicad_pro", ".kicad_dru"):
        shutil.copy(src / f"{board}{suffix}", tmp_path / f"{board}{suffix}")
    pcb = pcbnew.LoadBoard(str(tmp_path / f"{board}.kicad_pcb"))
    x, y = free_spot(pcb, set(layout_plan(board)["frame"].get("fine_pitch_power_pads", [])))
    track = pcbnew.PCB_TRACK(pcb)
    track.SetStart(pcbnew.VECTOR2I(x, y))
    track.SetEnd(pcbnew.VECTOR2I(x + pcbnew.FromMM(2), y))
    track.SetWidth(pcbnew.FromMM(0.2))
    track.SetLayer(pcbnew.F_Cu)
    pcb.Add(track)
    track.SetNet(pcb.FindNet(net))
    pcb.Save(str(tmp_path / f"{board}.kicad_pcb"))
    report = tmp_path / "drc.json"
    subprocess.run(["kicad-cli", "pcb", "drc", "--format", "json", "--output", str(report),
                    str(tmp_path / f"{board}.kicad_pcb")], capture_output=True, check=False)
    widths = [v["description"] for v in json.loads(report.read_text())["violations"]
              if v["type"] == "track_width"]
    assert any("power_track_width" in d for d in widths), widths
