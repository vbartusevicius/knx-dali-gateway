"""Test the REM3 DC-DC footprint: pin count, positions, THT, creepage.

Reads the .kicad_mod file directly (no KiCad needed) and checks:
- Exactly 5 pads (pins 2, 14, 16, 22, 23)
- All pads are thru_hole
- Input-to-output pad edge distance >= 6mm
- Courtyard dimensions match body + 0.25mm
"""

import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
FP_PATH = (REPO / "hardware/circuits/lib/gateway_fp.pretty"
           / "Converter_DCDC_RECOM_REM3_DIP24.kicad_mod")


def _parse_footprint():
    """Parse the .kicad_mod file and return pad positions and courtyard."""
    text = FP_PATH.read_text()
    # Parse pads: (pad "N" thru_hole TYPE (at X Y) (size W H) (drill D))
    pads = {}
    for m in re.finditer(
        r'\(pad "(\d+)" (\w+)\s+\w+\s*\(at ([\d.\-]+) ([\d.\-]+)\)'
        r'.*?\(size ([\d.\-]+) ([\d.\-]+)\)\s*\(drill ([\d.\-]+)\)',
        text, re.DOTALL
    ):
        pin = int(m.group(1))
        pad_type = m.group(2)
        x, y = float(m.group(3)), float(m.group(4))
        w, h = float(m.group(5)), float(m.group(6))
        drill = float(m.group(7))
        pads[pin] = {
            "type": pad_type, "x": x, "y": y,
            "w": w, "h": h, "drill": drill,
        }
    # Parse courtyard lines
    crtyd_lines = re.findall(
        r'\(fp_line\s*\(start ([\d.\-]+) ([\d.\-]+)\)\s*'
        r'\(end ([\d.\-]+) ([\d.\-]+)\).*?F\.CrtYd',
        text, re.DOTALL
    )
    return pads, crtyd_lines


def test_footprint_has_5_pads():
    """The REM3-2415S/A has only 5 pins: 2, 14, 16, 22, 23."""
    pads, _ = _parse_footprint()
    assert set(pads.keys()) == {2, 14, 16, 22, 23}, \
        f"Expected pins {{2,14,16,22,23}}, got {set(pads.keys())}"


def test_all_pads_are_tht():
    """All pads must be thru_hole (THT module)."""
    pads, _ = _parse_footprint()
    for pin, pad in pads.items():
        assert pad["type"] == "thru_hole", \
            f"Pin {pin} is {pad['type']}, expected thru_hole"
        assert pad["drill"] > 0, f"Pin {pin} has no drill"


def test_input_output_creepage():
    """Input-to-output pad edge distance must be >= 6mm.

    Input pins: 2 (0, 2.54), 22 (15.24, 5.08), 23 (15.24, 2.54)
    Output pins: 14 (15.24, 25.4), 16 (15.24, 20.32)

    The closest input-output pair is pin 23 (15.24, 2.54) and
    pin 16 (15.24, 20.32): same x, y distance = 17.78mm.
    Pad size is 2.4x1.6mm, so edge distance = 17.78 - 1.6/2 - 1.6/2 = 16.18mm.
    But we also check pin 2 (0, 2.54) vs pin 16 (15.24, 20.32):
    distance = sqrt(15.24^2 + 17.78^2) = 23.4mm, edge = 23.4 - ~1.6 = 21.8mm.
    The minimum is pin 23 vs pin 16: 17.78 - 1.6 = 16.18mm >= 6mm.
    """
    pads, _ = _parse_footprint()
    input_pins = [2, 22, 23]
    output_pins = [14, 16]
    min_edge_dist = 1e9
    for ip in input_pins:
        for op in output_pins:
            dx = pads[ip]["x"] - pads[op]["x"]
            dy = pads[ip]["y"] - pads[op]["y"]
            center_dist = (dx**2 + dy**2) ** 0.5
            # Approximate edge distance: center distance minus half pad sizes
            # Pads are 2.4x1.6mm; use the smaller dimension for conservative estimate
            edge_dist = center_dist - 1.6  # subtract ~1 pad length
            min_edge_dist = min(min_edge_dist, edge_dist)
    assert min_edge_dist >= 6.0, \
        f"Min input-output pad edge distance {min_edge_dist:.2f}mm < 6mm"


def test_courtyard_dimensions():
    """Courtyard should be ~body + 0.25mm = 31.8+0.5 x 20.3+0.5mm."""
    _, crtyd_lines = _parse_footprint()
    assert len(crtyd_lines) >= 4, "Courtyard must have >= 4 lines"
    xs = []
    ys = []
    for x0, y0, x1, y1 in crtyd_lines:
        xs.extend([float(x0), float(x1)])
        ys.extend([float(y0), float(y1)])
    width = max(xs) - min(xs)
    height = max(ys) - min(ys)
    # Body is 31.8mm long (along pin rows = y axis) and 20.3mm wide (x axis)
    # Courtyard: 31.8+0.5=32.3 x 20.3+0.5=20.8 (approximately)
    # Our footprint: x spans -2.78 to 18.02 = 20.8mm, y spans -2.18 to 30.12 = 32.3mm
    assert 20.0 < width < 21.5, f"Courtyard width {width:.1f}mm, expected ~20.8"
    assert 31.5 < height < 33.0, f"Courtyard height {height:.1f}mm, expected ~32.3"
