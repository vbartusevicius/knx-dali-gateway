"""JLCPCB assembly: every JLC part is what the circuit asks for and JLCPCB's
own footprint for it lands pin for pin on ours (hardware/scripts/jlc_parts.py).
The board checks fetch live JLCPCB/EasyEDA data (cached in hardware/build/jlc)."""

import json
import sys
from pathlib import Path

import pcbnew
import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "hardware/scripts"))
import jlc_parts as jp

SOT23 = [("1", -0.95, -1.0), ("2", 0.95, -1.0), ("3", 0.0, 1.0)]


def our_pads(pads, rotation=0, dx=10.0, dy=20.0, half=0.4):
    placed = []
    for n, x, y in pads:
        x, y = jp.rotate(x, y, rotation)
        placed.append(jp.Pad(n, dx + x - half, dy + y - half, dx + x + half, dy + y + half))
    return placed


@pytest.mark.parametrize("rotation", [0, 90, 180, 270])
def test_fit_finds_the_rotation_and_centre(rotation):
    placement = jp.fit(our_pads(SOT23, rotation), SOT23, bottom=False)
    assert placement.rotation == rotation
    assert (placement.x, placement.y) == pytest.approx((10.0, 20.0))


def test_fit_rejects_swapped_pins():
    swapped = [("2", -0.95, -1.0), ("1", 0.95, -1.0), ("3", 0.0, 1.0)]
    assert jp.fit(our_pads(swapped), SOT23, bottom=False) is None


def test_fit_rejects_a_different_package():
    sot89 = [("1", -1.5, -2.0), ("2", 0.0, -2.0), ("3", 1.5, -2.0)]
    assert jp.fit(our_pads(SOT23), sot89, bottom=False) is None


def test_fit_mirrors_bottom_parts_about_their_x_axis():
    mirrored = [(n, x, -y) for n, x, y in SOT23]
    assert jp.fit(our_pads(mirrored), SOT23, bottom=True).rotation == 0
    assert jp.fit(our_pads(mirrored), SOT23, bottom=False) is None


def test_pad_map_allows_different_numbering_but_not_wrong_pins():
    """A 6 x 6 mm tact switch: contacts 1-2 and 3-4 run along the long side."""
    theirs = [("1", -4.5, 2.25), ("2", 4.5, 2.25), ("3", -4.5, -2.25), ("4", 4.5, -2.25)]
    ours = our_pads([("1", -4.5, 2.25), ("1", 4.5, 2.25), ("2", -4.5, -2.25), ("2", 4.5, -2.25)])
    pad_map = {"1": ["1"], "2": ["1"], "3": ["2"], "4": ["2"]}
    assert jp.fit(ours, theirs, bottom=False, pad_map=pad_map).rotation in (0, 180)
    across = our_pads([("1", -4.5, 2.25), ("2", 4.5, 2.25), ("1", -4.5, -2.25), ("2", 4.5, -2.25)])
    assert jp.fit(across, theirs, bottom=False, pad_map=pad_map) is None


@pytest.mark.parametrize("text,value", [("4k7", 4700), ("330R", 330), ("100n", 1e-7), ("10kΩ", 1e4),
                                        ("100nF", 1e-7), ("2.2uF", 2.2e-6), ("470u", 4.7e-4), ("27R", 27)])
def test_si_values(text, value):
    assert jp.si(text) == pytest.approx(value)


def record(model, **attributes):
    return {"componentModelEn": model,
            "attributes": [{"attribute_name_en": k.replace("_", " "), "attribute_value_name": v}
                           for k, v in attributes.items()]}


def test_value_check_catches_wrong_capacitance_rating_and_part():
    part = {"mpn": "CC0603KRX7R9BB104", "value": "100n 50V", "footprint": "Capacitor_SMD:C_0603_1608Metric"}
    assert not jp.value_problems(part, record("CC0603KRX7R9BB104", Capacitance="100nF", Voltage_Rating="50V"))
    assert jp.value_problems(part, record("CC0603KRX7R9BB104", Capacitance="10nF", Voltage_Rating="50V"))
    assert jp.value_problems(part, record("CC0603KRX7R9BB104", Capacitance="100nF", Voltage_Rating="25V"))
    assert jp.value_problems(part, record("CL10B104KB8NNNC", Capacitance="100nF", Voltage_Rating="50V"))


@pytest.fixture(scope="module", params=("main", "ui"))
def board_checks(request):
    board = request.param
    pcb = pcbnew.LoadBoard(str(REPO / f"hardware/layout/poc/{board}/{board}.kicad_pcb"))
    parts = json.loads((REPO / f"hardware/build/poc/{board}/parts.json").read_text())
    return parts, jp.check_board(pcb, parts)


def test_every_jlc_part_is_checked_and_lands_pin_for_pin(board_checks):
    parts, checks = board_checks
    assert {c.ref for c in checks} == {p["ref"] for p in parts if p["assembly"] == "jlc"}
    assert not [f"{c.ref} {c.lcsc}: {p}" for c in checks for p in c.problems]
