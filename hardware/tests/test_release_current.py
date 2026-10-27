"""The newest committed release is what the current boards produce: its
KiCad files are the stored layouts, its JLCPCB and generic BOM and placement
files match a fresh, fully checked export, and every board and the panel ship
Gerbers with drills and netlist plus ODB++ for any board house. A board change therefore needs a new
release before anything is ordered."""

import filecmp
import sys
import zipfile
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "hardware/scripts"))
import fab_outputs
import release

LATEST = REPO / "hardware/releases" / release.RELEASE


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


@pytest.mark.parametrize("board", release.BOARDS)
def test_release_generic_assembly_files_match_a_fresh_export(board, tmp_path):
    parts = {p["ref"]: p for p in release.parts(board)}
    fab_outputs.generic_assembly(release.layout_path(board), tmp_path, board, parts)
    for name in (f"{board}_bom.csv", f"{board}_cpl.csv"):
        assert (tmp_path / name).read_text() == (LATEST / board / name).read_text(), f"{LATEST.name}/{board}/{name}"


@pytest.mark.parametrize("folder", (*release.BOARDS, "panel"))
def test_release_has_files_for_any_board_house(folder):
    with zipfile.ZipFile(LATEST / folder / f"{folder}_gerbers.zip") as z:
        names = z.namelist()
    assert any(n.endswith("-Edge_Cuts.gbr") for n in names)
    assert any(n.endswith("-PTH.drl") for n in names) and any(n.endswith("-NPTH.drl") for n in names)
    assert f"{folder}.d356" in names
    with zipfile.ZipFile(LATEST / folder / f"{folder}_odb.zip") as z:
        odb = z.namelist()
    assert any(n.endswith("matrix/matrix") for n in odb), odb[:10]
    assert any("steps/" in n and n.endswith("/profile") for n in odb), odb[:10]
    for name in (f"{folder}_bom.csv", f"{folder}_cpl.csv"):
        assert (LATEST / folder / name).stat().st_size > 0
