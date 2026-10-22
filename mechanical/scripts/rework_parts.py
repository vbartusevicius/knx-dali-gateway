"""Retrofit parts for enclosures printed from the v3 rev C release (before the
2026-10-22 review), written to hardware/releases/v3/rework/:

    docker compose --profile hardware run --rm -T -e HARDWARE_VARIANT=v3 cad \
        "python3 mechanical/scripts/rework_parts.py"

button_pin.stl: stepped pin that bridges the gap between a B3U-1000P actuator
and the rev C cover's plain 2.6 mm button hole. The collar rests on the
actuator inside the case, the stem passes through the hole and stands proud
of the front. Print two, collar down, PETG, 0.1 mm layers.
"""

import sys
from pathlib import Path

import cadquery as cq

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "mechanical/scripts"))
import build_enclosure as be

REV_C_HOLE = 2.6
STEM_CLEARANCE = 0.4
COLLAR = 3.0
COLLAR_TOP_CLEARANCE = 0.16
PROUD = 1.0


def button_pin():
    collar = be.FRONT_INNER_Z - COLLAR_TOP_CLEARANCE - be.button_top_z("SW1")
    stem = be.WALL + PROUD
    return (cq.Workplane("XY").circle(COLLAR / 2).extrude(collar)
            .faces(">Z").workplane().circle((REV_C_HOLE - STEM_CLEARANCE) / 2).extrude(stem))


def main():
    out = REPO / "hardware/releases/v3/rework"
    out.mkdir(parents=True, exist_ok=True)
    cq.exporters.export(button_pin(), str(out / "button_pin.stl"), cq.exporters.ExportTypes.STL)
    cq.exporters.export(button_pin(), str(out / "button_pin.step"), cq.exporters.ExportTypes.STEP)
    print(f"button_pin: collar {be.FRONT_INNER_Z - COLLAR_TOP_CLEARANCE - be.button_top_z('SW1'):.2f} mm, "
          f"stem {be.WALL + PROUD:.2f} mm -> {out.relative_to(REPO)}")


if __name__ == "__main__":
    main()
