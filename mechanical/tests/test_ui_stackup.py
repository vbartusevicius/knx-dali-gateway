"""UI board depth: flush OLED glass, protruding buttons, LED windows, and the
values in spec.yaml must match the computed stack-up."""

import sys
from dataclasses import replace
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "mechanical/scripts"))
from ui_stackup import FRONT_OUTER_Z, SPEC_DATA, compute_ui_stackup

SPEC = SPEC_DATA


def test_stackup_fits():
    ok, violations = compute_ui_stackup().fits()
    assert ok, violations


def test_oled_glass_flush_with_front():
    assert FRONT_OUTER_Z - 0.3 <= compute_ui_stackup().oled_glass_z <= FRONT_OUTER_Z


def test_buttons_protrude():
    s = compute_ui_stackup()
    assert s.prog_cap_z > FRONT_OUTER_Z and s.reset_cap_z > FRONT_OUTER_Z


def test_too_thick_led_skin_fails():
    ok, violations = replace(compute_ui_stackup(), led_skin_mm=1.2).fits()
    assert not ok and any("skin" in v for v in violations)


def test_spec_matches_stackup():
    s = compute_ui_stackup()
    assert abs(SPEC["front_ui"]["board_z"] - s.board_z) < 0.01
    assert abs(SPEC["front_ui"]["board_top_z"] - s.board_top_z) < 0.01


def test_board_moved_back_sinks_the_glass():
    s = compute_ui_stackup()
    bad = replace(s, board_z=s.board_z - 1, board_top_z=s.board_top_z - 1,
                  oled_glass_z=s.oled_glass_z - 1, prog_cap_z=s.prog_cap_z - 1,
                  reset_cap_z=s.reset_cap_z - 1)
    ok, violations = bad.fits()
    assert not ok and any("OLED" in v for v in violations)


def test_short_button_fails():
    s = compute_ui_stackup()
    ok, violations = replace(s, prog_actuator_mm=4.3, prog_cap_z=s.board_top_z + 4.3).fits()
    assert not ok and any("PRG/RST" in v for v in violations)
