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


def test_only_polarised_parts_carry_a_mark():
    assert assembly_drawing.polarity("R_0805_2012Metric") == "none"
    assert assembly_drawing.polarity("C_0805_2012Metric") == "none"
    assert assembly_drawing.polarity("Crystal_SMD_5032-2Pin_5.0x3.2mm") == "none"
    assert assembly_drawing.polarity("D_SMA") == "cathode"
    assert assembly_drawing.polarity("D_SOD-123") == "cathode"
    assert assembly_drawing.polarity("D_SMA", "SMAJ40CA") == "none"   # bidirectional TVS
    assert assembly_drawing.polarity("D_SMA", "SMAJ33A") == "cathode"
    assert assembly_drawing.polarity("CP_Elec_10x10") == "plus"
    assert assembly_drawing.polarity("SOT-23") == "pin1"
    assert assembly_drawing.polarity("LED_WS2812B-MINI-V3J_3.5x3.5mm") == "pin1"


@pytest.mark.parametrize("board", BOARDS)
def test_every_polarised_part_has_a_pin_1_to_mark(board):
    _, items = assembly_drawing.parts(board)
    assert not [p["ref"] for p in items if p["polarity"] in ("cathode", "plus") and not p["pin1"]]
