"""KiCad silently ignores a whole .kicad_dru file when one rule fails to
parse, which once switched off every custom rule. Plant a too-thin power
track on a copy of each stored board and require DRC to flag it through the
project rules."""

import json
import shutil
import subprocess
from pathlib import Path

import pcbnew
import pytest

REPO = Path(__file__).resolve().parents[2]


@pytest.mark.parametrize("board,net", [("main", "VFILT"), ("ui", "+3V3")])
def test_project_rules_are_enforced(tmp_path, board, net):
    src = REPO / f"hardware/layout/poc/{board}"
    for suffix in (".kicad_pcb", ".kicad_pro", ".kicad_dru"):
        shutil.copy(src / f"{board}{suffix}", tmp_path / f"{board}{suffix}")
    pcb = pcbnew.LoadBoard(str(tmp_path / f"{board}.kicad_pcb"))
    track = pcbnew.PCB_TRACK(pcb)
    track.SetStart(pcbnew.VECTOR2I(pcbnew.FromMM(20), pcbnew.FromMM(-20)))
    track.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(22), pcbnew.FromMM(-20)))
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
