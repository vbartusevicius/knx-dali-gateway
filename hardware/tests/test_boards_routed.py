"""The committed boards are fully routed and pass KiCad's DRC with the
project rules written next to them (layout.py sync/route)."""

import json
import subprocess
from pathlib import Path

import pytest
import yaml

REPO = Path(__file__).resolve().parents[2]
VARIANT = yaml.safe_load((REPO / "hardware/product/spec.yaml").read_text())["active_variant"]
BOARDS = ("main", "ui")


@pytest.fixture(scope="module", params=BOARDS)
def drc(request, tmp_path_factory):
    board = REPO / "hardware/layout" / VARIANT / request.param / f"{request.param}.kicad_pcb"
    report = tmp_path_factory.mktemp(request.param) / "drc.json"
    subprocess.run(["kicad-cli", "pcb", "drc", "--format", "json", "--severity-all",
                    "--output", str(report), str(board)],
                   capture_output=True, check=False)
    return json.loads(report.read_text())


def test_no_drc_errors(drc):
    errors = [f"{v['type']}: {v['description']}" for v in drc["violations"] if v["severity"] == "error"]
    assert not errors


def test_no_unrouted_connections(drc):
    assert not drc["unconnected_items"]
