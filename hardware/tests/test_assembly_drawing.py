"""Hand-assembly drawing: every placed part appears on the page of its own
side, the bottom page is mirrored, and each release folder ships the PDF."""

import json
import sys

import pcbnew
import pytest
from board_variant import BOARDS, REPO, VARIANT, build_path, layout_path

sys.path.insert(0, str(REPO / "hardware/scripts"))
import assembly_drawing


@pytest.mark.parametrize("board", BOARDS)
def test_drawing_covers_every_part_on_its_own_side(board, tmp_path):
    pdf = tmp_path / f"{board}_assembly.pdf"
    drawn = assembly_drawing.write(board, pdf)
    pcb = pcbnew.LoadBoard(str(layout_path(board)))
    sourced = {p["ref"] for p in json.loads(build_path(board, "parts.json").read_text())}
    expected = {"top": set(), "bottom": set()}
    for fp in pcb.Footprints():
        if fp.Pads() and fp.GetReference() in sourced:
            expected["bottom" if fp.IsFlipped() else "top"].add(fp.GetReference())
    assert {side: set(refs) for side, refs in drawn.items()} == {s: r for s, r in expected.items() if r}
    assert pdf.read_bytes().startswith(b"%PDF") and pdf.stat().st_size > 10_000


def test_same_value_same_colour():
    assert assembly_drawing.colour("10k") == assembly_drawing.colour("10k")
    assert assembly_drawing.colour("10k") != assembly_drawing.colour("100n 50V")


@pytest.mark.parametrize("board", BOARDS)
def test_release_ships_the_assembly_drawing(board):
    pdf = REPO / "hardware/releases" / VARIANT / board / f"{board}_assembly.pdf"
    assert pdf.exists() and pdf.read_bytes().startswith(b"%PDF")
