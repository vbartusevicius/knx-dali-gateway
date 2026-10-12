"""Box models (STEP) for the project parts KiCad ships no 3D model for.

The STEP files next to this script are library assets referenced by
hardware/scripts/layout.py; rerun after changing a body size (`cad` container):

    python3 hardware/circuits/lib/gateway_3d/generate.py
"""

from pathlib import Path

import cadquery as cq

OUT_DIR = Path(__file__).resolve().parent

# Part dimensions (body size + height) from datasheets / height table.
# Format: (name, width_mm, depth_mm, height_mm)
# Bodies are centered at origin in X/Y, sitting on the PCB (bottom at z=0).
PARTS = {
    "XIAO_ESP32C6": (21.0, 17.8, 3.5),
    "REM3_DIP24": (31.8, 20.3, 10.7),
    "TerminalBlock_MaiXu_MX126-5.0-02P": (11.6, 9.1, 8.0),
    "Fuse_1812_4532Metric": (4.5, 3.2, 1.2),
    "Phoenix_PST_1,0-2-5,75_R32": (8.55, 2.8, 9.9),
}


def make_box_model(name, width, depth, height):
    """Create a simple box model centered at origin, sitting on the PCB
    (bottom face at z=0, top face at z=height)."""
    result = cq.Workplane("XY").box(width, depth, height, centered=(True, True, False))
    step_path = OUT_DIR / f"{name}.step"
    cq.exporters.export(result, str(step_path))
    print(f"  {name}: {width}x{depth}x{height} mm -> {step_path.name}")
    return step_path


def main():
    print("Generating 3D box models:")
    for name, (w, d, h) in PARTS.items():
        make_box_model(name, w, d, h)
    print(f"\nAll models saved to {OUT_DIR}")


if __name__ == "__main__":
    main()
