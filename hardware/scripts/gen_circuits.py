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
import importlib
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
from product_spec import load_spec
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
            "footprint_ref": p.fields.get("Footprint_Reference", ""),
            "pins": {str(pin.num): pin.name for pin in p.pins},
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




def collect_assembly_parts(variant, spec):
    """Automatically build the off-board + hand-fitted assembly_parts list from
    parts.json of every board in `variant` and `spec.yaml`."""
    boards = spec["variants"][variant]["boards"]
    hand_by_mpn = {}
    all_parts = []
    for b in boards:
        for p in json.loads((BUILD / variant / b / "parts.json").read_text()):
            all_parts.append((b, p))
            if p["assembly"] == "hand":
                entry = hand_by_mpn.setdefault(p["mpn"], {"p": p, "locs": []})
                entry["locs"].append(f"{b} {p['ref']}")

    items = []
    # 1. Hand-fitted board parts (XIAO, REM3, PST 1,0 pin strips, THT OLED)
    if "102010636" in hand_by_mpn:
        locs = ", ".join(hand_by_mpn["102010636"]["locs"])
        sock_h = "5.0 mm" if variant == "v3" else "8.5 mm"
        items.append({
            "item": "MCU module (hand-fitted into sockets)",
            "qty": len(hand_by_mpn["102010636"]["locs"]),
            "part": "Seeed Studio XIAO ESP32C6 Pre-Soldered (SKU 102010636)",
            "description": f"ESP32-C6 module with pre-soldered 2x7 pin headers; plugs into the two {sock_h} female sockets ({locs})",
            "source": "Seeed Studio / Digi-Key / Mouser",
        })
        items.append({
            "item": "XIAO sockets (hand-soldered)",
            "qty": 2 * len(hand_by_mpn["102010636"]["locs"]),
            "part": "1x7 2.54 mm female header, 5.0 mm tall (e.g. Hong Cheng HC-PM254-5.0H)" if variant == "v3"
                    else "PM2.54-1x7PZZ-H8.5 (LCSC C54876729)",
            "description": f"1x7 female header, 2.54 mm, {sock_h} tall, soldered into {locs}; the XIAO stays detachable",
            "source": "LCSC",
        })
    if "REM3-2415S/A/CTRL" in hand_by_mpn:
        locs = ", ".join(hand_by_mpn["REM3-2415S/A/CTRL"]["locs"])
        items.append({
            "item": "Isolated DALI DC-DC converter (hand-soldered)",
            "qty": len(hand_by_mpn["REM3-2415S/A/CTRL"]["locs"]),
            "part": "RECOM REM3-2415S/A/CTRL",
            "description": f"24 V -> 15 V 3 W reinforced-isolated medical DC-DC converter, DIP-24 A-pinning with pin 1 CTRL ({locs}; no minimum load required)",
            "source": "Digi-Key / Mouser / TME",
        })
    if "WEA012864DWPP3N00003" in hand_by_mpn:
        locs = ", ".join(hand_by_mpn["WEA012864DWPP3N00003"]["locs"])
        items.append({
            "item": "OLED display module (hand-soldered)",
            "qty": len(hand_by_mpn["WEA012864DWPP3N00003"]["locs"]),
            "part": "Winstar WEA012864DWPP3N00003",
            "description": f"0.96 in 128x64 SSD1306 I2C OLED module, 4-pin header (1 VCC, 2 GND, 3 SCL, 4 SDA); sits face-down in the cover pocket and solders into {locs}",
            "source": "Digi-Key / Mouser / TME",
        })
    if "1574200" in hand_by_mpn:
        locs = hand_by_mpn["1574200"]["locs"]
        items.append({
            "item": "KNX / AUX PCB pin strips (hand-soldered)" if len(locs) > 1 else "KNX PCB pin strip (hand-soldered)",
            "qty": len(locs),
            "part": "Phoenix Contact PST 1,0/ 2-5,75 R32 (1574200)",
            "description": f"2-pin 5.75 mm pitch Ø 1.0 mm pin strip soldered into {', '.join(locs)}",
            "source": "Phoenix Contact / Digi-Key / Mouser",
        })
        items.append({
            "item": "KNX bus plug (red/black)",
            "qty": 1,
            "part": "Phoenix Contact PTS 0,5/ 2-PH-5,75 BKRD (1574300)",
            "description": "red/black KNX bus push-in connector mating the PST 1,0/2-5,75 pin strip (main J1)",
            "source": "Phoenix Contact / Digi-Key / Mouser",
        })
        if any(loc.endswith("J2") for loc in locs):
            items.append({
                "item": "AUX 20-32 V plug (yellow/white)",
                "qty": 1,
                "part": "Phoenix Contact PTS 0,5/ 2-PH-5,75 GY35YE (1574299)",
                "description": "yellow/grey auxiliary-voltage connector mating the rear PST 1,0/2-5,75 pin strip (main J2; 20-32 V for the internal DALI supply only)",
                "source": "Phoenix Contact / Digi-Key / Mouser",
            })

    # 2. Plug-in ZIF OLED + mounting tape (when FPC-05F-14PH20 is on ui)
    if any(p["mpn"] == "FPC-05F-14PH20" for _, p in all_parts):
        items.append({
            "item": "OLED display (14-pin ZIF FPC, plug-in, no soldering)",
            "qty": 1,
            "part": "QG-6428TSWCG02 / JLX12864OLED-S096W (0.96 in 64x128 SSD1312 I2C)",
            "description": "0.96 in 64x128 portrait SSD1312 I2C OLED (14.0 x 28.0 x 1.22 mm); flex tail folds through the 1.0 mm top cutout on UI and plugs into JLC-assembled 14-pin 0.5 mm ZIF connector ui J2 (XUNPU FPC-05F-14PH20)",
            "source": "Panelook / LCSC / AliExpress / EastRising",
        })
        items.append({
            "item": "OLED mounting tape",
            "qty": 2,
            "part": "3M VHB / double-sided acrylic foam tape, 1.0 mm thick (2 strips 1.4 x 26 mm)",
            "description": "adheres the left/right back edges of the 14.0 x 28.0 mm 64x128 OLED onto the cover's two backing rails (1.5 mm gap; stack 1-2 layers of 1.0 mm tape)",
            "source": "any",
        })

    # 3. Board-to-board cables from spec["boards"]["interconnects"]
    ffc_links = [lk for lk in spec["boards"].get("interconnects", []) if lk.get("type") == "ffc"]
    cable_links = [lk for lk in spec["boards"].get("interconnects", []) if lk.get("type") == "cable"]
    if ffc_links:
        pairs = ", ".join(f"{lk['from']} {lk.get('refs', {}).get(lk['from'], 'J4')} <-> {lk['to']} {lk.get('refs', {}).get(lk['to'], 'J1')}" for lk in ffc_links)
        items.append({
            "item": "Main-to-UI FFC cable",
            "qty": len(ffc_links),
            "part": "SHOU HAN FFC-0.5-8P-50mm (LCSC C6936295)",
            "description": f"8-pin 0.5 mm same-side (Type A) FFC, 50 mm (0.3 mm insert thickness); {pairs}",
            "source": "LCSC",
        })
    if cable_links:
        pairs = ", ".join(f"{lk['from']} {lk.get('refs', {}).get(lk['from'], 'J4')} <-> {lk['to']} {lk.get('refs', {}).get(lk['to'], 'J1')}" for lk in cable_links)
        items.append({
            "item": "Main-to-UI cable",
            "qty": len(cable_links),
            "part": "JST A10SR10SR30K102A",
            "description": f"SH 1.0 mm 10-position jumper, 101.6 mm, 1:1 (pin 1 to pin 1); {pairs}",
            "source": "Digi-Key / Mouser; any 1:1 JST-SH 10P cable of 80-120 mm works",
        })

    # 4. DALI supply jumper caps
    if any(p["ref"] == "JP1" for _, p in all_parts):
        items.append({
            "item": "DALI supply jumper caps",
            "qty": 2,
            "part": "LCSC C100114",
            "description": "2.54 mm closed jumper cap, 3 A; both on main JP1 = internal DALI supply on",
            "source": "LCSC",
        })

    # 5. Case screws (when enclosure.screw_tabs is non-empty) and printed case parts
    screws = spec.get("enclosure", {}).get("screw_tabs", [])
    if screws:
        items.append({
            "item": "Case screws",
            "qty": len(screws),
            "part": "2.5 x 10 mm thread-forming screw for plastics, pan head (e.g. EJOT PT K25x10 or Wuerth WUEPLAST 2.5x10)",
            "description": "through the top and bottom walls (the wire sides) into the cover's screw tabs (2.2 mm pilot)",
            "source": "any",
        })
    snap_note = " (snap-fit, no screws)" if not screws else ""
    items.append({
        "item": f"Case base{snap_note}",
        "qty": 1,
        "part": "enclosure/base.stl",
        "description": "PETG or ASA, 0.2 mm layers, rail channel face down",
        "source": "printed",
    })
    items.append({
        "item": f"Case cover{snap_note}",
        "qty": 1,
        "part": "enclosure/cover.stl (or cover_embossed.stl / cover_color.3mf)",
        "description": "PETG or ASA, 0.2 mm layers, front face down (cover.stl plain smooth, cover_embossed.stl 0.3 mm recessed text, cover_color.3mf multi-colour inlay)",
        "source": "printed",
    })
    return items


def sync_assembly_parts(variant, spec):
    target = (common.REPO_ROOT / "hardware/product/assembly_parts.yaml"
              if variant == "v1" else common.REPO_ROOT / f"hardware/circuits/variants/{variant}/assembly_parts.yaml")
    header = ("# Automatically generated by hardware/scripts/gen_circuits.py from parts.json + spec.yaml.\n"
              "# Copied by hardware/scripts/release.py into the release as assembly_parts.csv.\n")
    target.write_text(header + yaml.safe_dump(collect_assembly_parts(variant, spec), sort_keys=False))


def main():
    spec = load_spec()
    variant = spec["active_variant"]
    BUILD.mkdir(parents=True, exist_ok=True)

    # import the variant's board modules
    pkg = f"variants.{variant}"
    modules = {name: importlib.import_module(f"{pkg}.{name}")
               for name in spec["variants"][variant]["boards"]}
    logs = {name: generate_board(variant, name, module.build, module.NET_CLASSES)
            for name, module in modules.items()}
    write_derived({name: module.NET_CLASSES for name, module in modules.items()})
    sync_assembly_parts(variant, spec)

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
    spec = load_spec()
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
