# SPDX-FileCopyrightText: The knx-dali-gateway contributors
# SPDX-License-Identifier: CERN-OHL-P-2.0
"""Generate KiCad netlists and schematics for both boards with SKiDL.

Runs inside the `eda` container:
    python3 hardware/scripts/gen_circuits.py

Outputs under hardware/build/{lower,upper}/:
    <board>.net        KiCad XML netlist (kinet2pcb input)
    <board>.kicad_sch  generated schematic for manual proof
    skidl_erc.txt      SKiDL ERC report

Also rewrites the derived section of hardware/product/spec.yaml
(mcu_pins + net classes) so firmware/board data stays generated.
"""

import builtins
import io
import json
import sys
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "circuits"))

from skidl import (
    KICAD,
    generate_netlist,
    generate_schematic,
    reset,
)

import common

BUILD = common.REPO_ROOT / "hardware" / "build"
SPEC = common.SPEC_PATH
DERIVED_BEGIN = "# --- BEGIN GENERATED (hardware/scripts/gen_circuits.py) ---"
DERIVED_END = "# --- END GENERATED ---"


def generate_board(name, build_fn, net_classes):
    reset()
    (BUILD / name).mkdir(parents=True, exist_ok=True)
    common.load_spec()  # validate the spec loads before building
    import logging
    buf = io.StringIO()
    handler = logging.StreamHandler(buf)
    skidl_logger = logging.getLogger("skidl")
    skidl_logger.addHandler(handler)
    with redirect_stdout(buf), redirect_stderr(buf):
        build_fn()
        generate_netlist(file_=str(BUILD / name / f"{name}.net"))
        if "--no-schematic" not in sys.argv:
            generate_schematic(
                file_=str(BUILD / name / f"{name}.kicad_sch"),
            )
    skidl_logger.removeHandler(handler)
    out = buf.getvalue()
    (BUILD / name).mkdir(parents=True, exist_ok=True)
    (BUILD / name / "skidl_log.txt").write_text(out)
    parts = [
        {
            "ref": p.ref,
            "value": p.value,
            "footprint": p.footprint,
            "place": p.fields.get("Place", "misc"),
        }
        for p in builtins.default_circuit.parts if not p.ref.startswith("#")
    ]
    (BUILD / name / "parts.json").write_text(json.dumps(parts, indent=1))
    return out


def write_derived(net_classes_by_board):
    text = SPEC.read_text()
    derived = {
        "mcu_pins": __import__("upper_board").MCU_PINS,
        "net_classes": net_classes_by_board,
    }
    block = DERIVED_BEGIN + "\n" + yaml.safe_dump(
        {"derived": derived}, sort_keys=False) + DERIVED_END + "\n"
    if DERIVED_BEGIN in text and DERIVED_END in text:
        pre = text[: text.index(DERIVED_BEGIN)]
        text = pre + block
    else:
        if not text.endswith("\n"):
            text += "\n"
        text += "\n" + block
    SPEC.write_text(text)


def main():
    BUILD.mkdir(parents=True, exist_ok=True)
    import lower_board
    import upper_board

    logs = {}
    logs["lower"] = generate_board("lower", lower_board.build,
                                   lower_board.NET_CLASSES)
    logs["upper"] = generate_board("upper", upper_board.build,
                                   upper_board.NET_CLASSES)
    write_derived({"lower": lower_board.NET_CLASSES,
                   "upper": upper_board.NET_CLASSES})

    # power symbols and PWR_FLAGs legitimately carry no footprint; SKiDL
    # reports that as a netlist error. Those are documented waivers, not
    # electrical defects — every other error line fails the gate.
    waived = ("No footprint for ", "/#PWR", "/#FLG")
    erc_errors = 0
    netlist_errors = 0
    for name, log in logs.items():
        print(f"=== {name} ===")
        for line in log.splitlines():
            if "ERC" in line or "error" in line.lower():
                print(" ", line)
        n_err = sum(1 for line in log.splitlines() if "ERC ERROR" in line)
        erc_errors += n_err
        netlist_errors += sum(
            1 for line in log.splitlines()
            if line.startswith("ERROR")
            and not ("No footprint for" in line and
                     ("/#PWR" in line or "/#FLG" in line)))
    (BUILD / "skidl_erc.txt").write_text(
        "\n\n".join(f"=== {n} ===\n{l}" for n, l in logs.items()))
    print(f"SKiDL ERC errors: {erc_errors}, "
          f"non-waived netlist errors: {netlist_errors}")
    return 1 if (erc_errors or netlist_errors) else 0


if __name__ == "__main__":
    sys.exit(main())
