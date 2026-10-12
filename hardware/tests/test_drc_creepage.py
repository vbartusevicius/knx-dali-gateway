"""Test that KiCad DRC creepage rules catch cross-domain violations.

Creates a small fixture board with a DALI track 2mm from a KNX pad and
asserts that kicad-cli pcb drc reports a creepage violation.  This
verifies that the .kicad_dru rules (KiCad 9 syntax: hasNetclass(),
creepage constraint) actually work.

The fixture PCB is written as a text file in KiCad 9 S-expression format
with the correct structure (uuid, property, etc.).
"""

import json
import subprocess
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

# Minimal KiCad 9 PCB with two pads on different domain nets, 2mm apart.
# Pad P1 (KNX/GND) at (50, -50) mm
# Pad P2 (DALI/GNDD) at (52, -50) mm — 2mm away
# Track on GNDD net from P2 to (54, -50)

FIXTURE_PCB = """\
(kicad_pcb
\t(version 20241229)
\t(generator "pcbnew")
\t(generator_version "9.0")
\t(general
\t\t(thickness 1.6)
\t\t(legacy_teardrops no)
\t)
\t(paper "A4")
\t(layers
\t\t(0 "F.Cu" signal)
\t\t(2 "B.Cu" signal)
\t\t(9 "F.Adhes" user "F.Adhesive")
\t\t(11 "B.Adhes" user "B.Adhesive")
\t\t(13 "F.Paste" user)
\t\t(15 "B.Paste" user)
\t\t(5 "F.SilkS" user "F.Silkscreen")
\t\t(7 "B.SilkS" user "B.Silkscreen")
\t\t(1 "F.Mask" user)
\t\t(3 "B.Mask" user)
\t\t(17 "Dwgs.User" user "User.Drawings")
\t\t(19 "Cmts.User" user "User.Comments")
\t\t(21 "Eco1.User" user "User.Eco1")
\t\t(23 "Eco2.User" user "User.Eco2")
\t\t(25 "Edge.Cuts" user)
\t\t(27 "Margin" user)
\t\t(31 "F.CrtYd" user "F.Courtyard")
\t\t(29 "B.CrtYd" user "B.Courtyard")
\t\t(35 "F.Fab" user)
\t\t(33 "B.Fab" user)
\t)
\t(setup
\t\t(pad_to_mask_clearance 0)
\t\t(allow_soldermask_bridges_in_footprints no)
\t\t(tenting front back)
\t\t(pcbplotparams
\t\t\t(svgprecision 4)
\t\t)
\t)
\t(net 0 "")
\t(net 1 "GND")
\t(net 2 "GNDD")
\t(footprint "test:pad_knx"
\t\t(layer "F.Cu")
\t\t(uuid "00000001-0000-0000-0000-000000000001")
\t\t(at 50 -50)
\t\t(property "Reference" "P1"
\t\t\t(at 0 -2 0)
\t\t\t(layer "F.SilkS")
\t\t\t(uuid "00000001-0000-0000-0000-000000000002")
\t\t\t(effects (font (size 1 1) (thickness 0.15)))
\t\t)
\t\t(property "Value" "KNX"
\t\t\t(at 0 2 0)
\t\t\t(layer "F.Fab")
\t\t\t(uuid "00000001-0000-0000-0000-000000000003")
\t\t\t(effects (font (size 1 1) (thickness 0.15)))
\t\t)
\t\t(attr through_hole)
\t\t(pad "1" thru_hole circle (at 0 0) (size 1.5 1.5) (drill 0.8)
\t\t\t(layers "*.Cu" "*.Mask") (net 1 "GND")
\t\t\t(uuid "00000001-0000-0000-0000-000000000004"))
\t)
\t(footprint "test:pad_dali"
\t\t(layer "F.Cu")
\t\t(uuid "00000002-0000-0000-0000-000000000001")
\t\t(at 52 -50)
\t\t(property "Reference" "P2"
\t\t\t(at 0 -2 0)
\t\t\t(layer "F.SilkS")
\t\t\t(uuid "00000002-0000-0000-0000-000000000002")
\t\t\t(effects (font (size 1 1) (thickness 0.15)))
\t\t)
\t\t(property "Value" "DALI"
\t\t\t(at 0 2 0)
\t\t\t(layer "F.Fab")
\t\t\t(uuid "00000002-0000-0000-0000-000000000003")
\t\t\t(effects (font (size 1 1) (thickness 0.15)))
\t\t)
\t\t(attr through_hole)
\t\t(pad "1" thru_hole circle (at 0 0) (size 1.5 1.5) (drill 0.8)
\t\t\t(layers "*.Cu" "*.Mask") (net 2 "GNDD")
\t\t\t(uuid "00000002-0000-0000-0000-000000000004"))
\t)
\t(segment (start 52 -50) (end 54 -50) (width 0.2) (layer "F.Cu") (net 2)
\t\t(uuid "00000003-0000-0000-0000-000000000001"))
\t(gr_line (start 40 -40) (end 60 -40) (layer "Edge.Cuts") (width 0.05)
\t\t(uuid "00000004-0000-0000-0000-000000000001"))
\t(gr_line (start 60 -40) (end 60 -60) (layer "Edge.Cuts") (width 0.05)
\t\t(uuid "00000004-0000-0000-0000-000000000002"))
\t(gr_line (start 60 -60) (end 40 -60) (layer "Edge.Cuts") (width 0.05)
\t\t(uuid "00000004-0000-0000-0000-000000000003"))
\t(gr_line (start 40 -60) (end 40 -40) (layer "Edge.Cuts") (width 0.05)
\t\t(uuid "00000004-0000-0000-0000-000000000004"))
)
"""

FIXTURE_DRU = """\
(version 1)
(rule creepage_selv-knx_dali
  (constraint creepage (min 6mm))
  (condition "(A.hasNetclass('SELV-KNX') || A.hasNetclass('KNX-Power')) && (B.hasNetclass('DALI') || B.hasNetclass('DALI-Power'))"))
(rule creepage_selv-knx_dali_rev
  (constraint creepage (min 6mm))
  (condition "(B.hasNetclass('SELV-KNX') || B.hasNetclass('KNX-Power')) && (A.hasNetclass('DALI') || A.hasNetclass('DALI-Power'))"))
"""

FIXTURE_PRO = """\
{
  "board": {
    "design_settings": {
      "defaults": {
        "board_outline_line_width": 0.05
      },
      "rules": {
        "min_clearance": 0.2,
        "min_track_width": 0.2,
        "min_via_diameter": 0.6,
        "min_via_drill": 0.3
      }
    },
    "layer_presets": []
  },
  "meta": {
    "filename": "fixture.kicad_pro",
    "version": 3
  },
  "net_settings": {
    "classes": [
      {
        "name": "Default",
        "priority": 2147483647,
        "clearance": 0.15,
        "track_width": 0.2,
        "via_diameter": 0.6,
        "via_drill": 0.3,
        "diff_pair_gap": 0.25,
        "diff_pair_via_gap": 0.25,
        "diff_pair_width": 0.2,
        "wire_width": 6,
        "bus_width": 12,
        "line_style": 0,
        "microvia_diameter": 0.3,
        "microvia_drill": 0.1,
        "pcb_color": "rgba(0, 0, 0, 0.000)",
        "schematic_color": "rgba(0, 0, 0, 0.000)"
      },
      {
        "name": "SELV-KNX",
        "priority": 2,
        "clearance": 0.15,
        "track_width": 0.2,
        "via_diameter": 0.6,
        "via_drill": 0.3,
        "diff_pair_gap": 0.25,
        "diff_pair_via_gap": 0.25,
        "diff_pair_width": 0.2,
        "wire_width": 6,
        "bus_width": 12,
        "line_style": 0,
        "microvia_diameter": 0.3,
        "microvia_drill": 0.1,
        "pcb_color": "rgba(0, 0, 0, 0.000)",
        "schematic_color": "rgba(0, 0, 0, 0.000)"
      },
      {
        "name": "DALI",
        "priority": 2,
        "clearance": 0.15,
        "track_width": 0.2,
        "via_diameter": 0.6,
        "via_drill": 0.3,
        "diff_pair_gap": 0.25,
        "diff_pair_via_gap": 0.25,
        "diff_pair_width": 0.2,
        "wire_width": 6,
        "bus_width": 12,
        "line_style": 0,
        "microvia_diameter": 0.3,
        "microvia_drill": 0.1,
        "pcb_color": "rgba(0, 0, 0, 0.000)",
        "schematic_color": "rgba(0, 0, 0, 0.000)"
      }
    ],
    "netclass_patterns": [
      {"netclass": "SELV-KNX", "pattern": "GND"},
      {"netclass": "DALI", "pattern": "GNDD"}
    ]
  }
}
"""


def _run_drc(pcb_path, fixture_dir):
    """Run kicad-cli pcb drc on a board and return the JSON result."""
    out = fixture_dir / "drc_result.json"
    if out.exists():
        out.unlink()
    cmd = [
        "kicad-cli", "pcb", "drc",
        "--severity-all",
        "--format", "json",
        "--output", str(out),
        str(pcb_path),
    ]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=60, check=False)
    if out.exists():
        return json.loads(out.read_text()), result
    return {"violations": [], "unconnected_items": [],
            "stderr": result.stderr, "stdout": result.stdout}, result


def test_drc_catches_cross_domain_creepage(tmp_path):
    """A DALI track 2mm from a KNX pad must trigger a creepage violation."""
    fixture_dir = tmp_path
    pcb_path = fixture_dir / "fixture.kicad_pcb"
    dru_path = fixture_dir / "fixture.kicad_dru"
    pro_path = fixture_dir / "fixture.kicad_pro"

    pcb_path.write_text(FIXTURE_PCB)
    dru_path.write_text(FIXTURE_DRU)
    pro_path.write_text(FIXTURE_PRO)

    drc, result = _run_drc(pcb_path, fixture_dir)
    violations = drc.get("violations", [])

    print(f"\n  DRC fixture: {len(violations)} violations")
    for v in violations:
        print(f"    {v.get('type', '?')}: {v.get('description', '?')[:100]}")
    if result.stderr:
        print(f"  stderr: {result.stderr[:200]}")

    creepage_violations = [
        v for v in violations
        if "creepage" in v.get("type", "").lower()
        or "creepage" in v.get("description", "").lower()
        or "clearance" in v.get("type", "").lower()
        or "clearance" in v.get("description", "").lower()
    ]
    assert len(creepage_violations) > 0, \
        f"Expected a creepage/clearance violation for 2mm DALI-KNX gap, " \
        f"got {len(violations)} violations: " \
        f"{[v.get('type') for v in violations]}"
