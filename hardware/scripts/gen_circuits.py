"""Generate KiCad netlists and schematics for the active variant's boards.

Runs inside the `eda` container:
    python3 hardware/scripts/gen_circuits.py

Outputs under hardware/build/<variant>/<board>/:
    <board>.net        KiCad XML netlist (kinet2pcb input)
    <board>.kicad_sch  generated schematic for manual proof
    skidl_log.txt      SKiDL ERC report

Also rewrites the derived section of hardware/product/spec.yaml
(mcu_pins + net classes) so firmware/board data stays generated.
"""

import builtins
import io
import json
import os
import sys
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "circuits"))
# SKiDL writes <script>.erc/.log into the working directory when imported.
(Path(__file__).resolve().parents[1] / "build").mkdir(exist_ok=True)
os.chdir(Path(__file__).resolve().parents[1] / "build")

import common
from skidl import (
    ERC,
    KICAD9,
    generate_netlist,
    reset,
    set_default_tool,
)

BUILD = common.REPO_ROOT / "hardware" / "build"
SPEC = common.SPEC_PATH
DERIVED_BEGIN = "# --- BEGIN GENERATED (hardware/scripts/gen_circuits.py) ---"
DERIVED_END = "# --- END GENERATED ---"


def generate_board(variant, name, build_fn, net_classes):
    reset()
    set_default_tool(KICAD9)
    bdir = BUILD / variant / name
    bdir.mkdir(parents=True, exist_ok=True)
    common.load_spec()
    import skidl as skidl_mod
    skidl_mod.config.backup_lib_file_name = str(BUILD / "gen_circuits_sklib.py")
    import logging
    buf = io.StringIO()
    handler = logging.StreamHandler(buf)
    handler.setFormatter(logging.Formatter("%(levelname)s %(message)s"))
    skidl_logger = logging.getLogger("skidl")
    skidl_logger.addHandler(handler)
    with redirect_stdout(buf), redirect_stderr(buf):
        build_fn()
        ERC()
        generate_netlist(file_=str(bdir / f"{name}.net"))
    skidl_logger.removeHandler(handler)
    out = buf.getvalue()
    bdir.mkdir(parents=True, exist_ok=True)
    (bdir / "skidl_log.txt").write_text(out)
    parts = [
        {
            "ref": p.ref,
            "value": p.value,
            "footprint": p.footprint,
            "place": p.fields.get("Place", "misc"),
            "lcsc": p.fields.get("LCSC", ""),
            "mpn": p.fields.get("Manufacturer_Part_Number", ""),
            "mfr": p.fields.get("Manufacturer", ""),
            "assembly": p.fields.get("Assembly", "jlc"),
            "source": p.fields.get("Source", ""),
        }
        for p in builtins.default_circuit.parts if not p.ref.startswith("#")
    ]
    (bdir / "parts.json").write_text(json.dumps(parts, indent=1))
    return out


def write_derived(net_classes_by_board):
    from blocks import mcu_xiao
    text = SPEC.read_text()
    derived = {
        "mcu_pins": mcu_xiao.MCU_PINS,
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
    spec = common.load_spec()
    variant = spec["active_variant"]
    BUILD.mkdir(parents=True, exist_ok=True)

    # import the variant's board modules
    pkg = f"variants.{variant}"
    main_mod = __import__(f"{pkg}.main", fromlist=["main"])
    ui_mod = __import__(f"{pkg}.ui", fromlist=["ui"])

    logs = {}
    logs["main"] = generate_board(variant, "main", main_mod.build,
                                   main_mod.NET_CLASSES)
    logs["ui"] = generate_board(variant, "ui", ui_mod.build,
                                ui_mod.NET_CLASSES)
    write_derived({"main": main_mod.NET_CLASSES, "ui": ui_mod.NET_CLASSES})

    erc_errors = 0
    netlist_errors = 0
    for name, log in logs.items():
        print(f"=== {name} ===")
        for line in log.splitlines():
            if "ERC" in line or "error" in line.lower():
                print(" ", line)
        erc_errors += sum(
            1 for line in log.splitlines() if "ERC ERROR" in line)
        netlist_errors += sum(
            1 for line in log.splitlines()
            if line.startswith(("ERROR", "CRITICAL"))
            and "ERC" not in line
            and not ("No footprint for" in line and "/#" in line))
    (BUILD / "skidl_erc.txt").write_text(
        "\n\n".join(f"=== {n} ===\n{l}" for n, l in logs.items()))
    print(f"SKiDL ERC errors: {erc_errors}, "
          f"non-waived netlist errors: {netlist_errors}")
    return 1 if (erc_errors or netlist_errors) else 0


def draw_schematic(board, timeout_s=600):
    """Opt-in: draw one board's .kicad_sch under a hard timeout.

    Schematic auto-drawing is slow and can hang on dense circuits; the
    netlist + parts JSON are already produced by main(). This runs in a
    child process so a timeout kills it cleanly without leaving SKiDL
    state behind. Usage: python3 gen_circuits.py --schematic <board>
    """
    import subprocess
    spec = common.load_spec()
    variant = spec["active_variant"]
    bdir = BUILD / variant / board
    script = (
        f"import sys; sys.path.insert(0,'{common.REPO_ROOT}/hardware/circuits'); "
        f"from skidl import KICAD9, set_default_tool, generate_schematic, "
        f"reset; set_default_tool(KICAD9); "
        f"import variants.{variant}.{board} as m; m.build(); "
        f"generate_schematic(file_='{bdir}/{board}.kicad_sch')")
    try:
        subprocess.run(["python3", "-c", script], timeout=timeout_s,
                       check=False, cwd=str(BUILD))
        print(f"{board}: schematic drawn")
    except subprocess.TimeoutExpired:
        print(f"{board}: schematic timed out after {timeout_s}s "
              f"(netlist + parts JSON still valid); continuing")


if __name__ == "__main__":
    if "--schematic" in sys.argv:
        boards = [a for a in sys.argv[2:] if not a.startswith("-")]
        for b in boards:
            draw_schematic(b)
    else:
        sys.exit(main())
