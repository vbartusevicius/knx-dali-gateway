"""The newest committed release is what the current boards produce: its
KiCad files are the stored layouts and its JLCPCB BOM and placement files
match a fresh, fully checked export. A board change therefore needs a new
release before anything is ordered."""

import filecmp
import sys
from pathlib import Path

import pytest
import yaml

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "hardware/scripts"))
import release

VARIANT = yaml.safe_load((REPO / "hardware/product/spec.yaml").read_text())["active_variant"]
LATEST = max((p for p in (REPO / "hardware/releases").iterdir() if p.is_dir()), key=lambda p: p.name)


def test_release_checks_live_supplier_data_by_default(monkeypatch):
    modes = []

    def check(pcb, parts, *, live):
        modes.append(live)
        return []

    monkeypatch.setattr(release.jlc_parts, "check_board", check)
    release.assembly_checks("ui")
    assert modes == [True]


@pytest.mark.parametrize("board", release.BOARDS)
def test_release_holds_the_stored_layout(board):
    for suffix in (".kicad_pcb", ".kicad_pro", ".kicad_dru"):
        stored = release.layout_path(board).with_suffix(suffix)
        assert filecmp.cmp(stored, LATEST / board / stored.name, shallow=False), f"{LATEST.name}/{board}/{stored.name}"


@pytest.mark.parametrize("board", release.BOARDS)
def test_release_assembly_files_match_a_fresh_export(board, tmp_path):
    release.assembly_files(board, tmp_path, release.assembly_checks(board, live=False))
    for name in (f"{board}_bom_jlc.csv", f"{board}_cpl_jlc.csv", f"{board}_hand_fit.csv"):
        assert (tmp_path / name).read_text() == (LATEST / board / name).read_text(), f"{LATEST.name}/{board}/{name}"
