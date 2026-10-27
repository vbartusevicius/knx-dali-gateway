"""Fabrication files any board house can take (`eda` container, KiCad 10).

    gerbers(pcb_path, out, name)            -> <name>_gerbers.zip
    odb(pcb_path, out, name)                -> <name>_odb.zip
    generic_assembly(pcb_path, out, name, parts_by_ref) -> <name>_bom.csv, <name>_cpl.csv

<name>_gerbers.zip holds Gerber X2 per layer, Excellon drill files (plated and
non-plated separately) and an IPC-D-356 netlist for the bare-board electrical
test (PCBWay, Eurocircuits, JLCPCB, Aisler). <name>_odb.zip is ODB++: copper,
drills, outline, netlist and component placement in one file (Aisler's
preferred upload; PCBWay and Eurocircuits accept it too). The generic BOM and
placement files carry manufacturer part numbers and KiCad's own coordinates
and rotations for assembly houses other than JLCPCB, whose files stay separate
(*_jlc.csv, with JLC-fitted rotations).
"""

import csv
import shutil
import subprocess
from pathlib import Path

import pcbnew

FAB_LAYERS = ["F.Mask", "B.Mask", "F.Paste", "B.Paste", "F.SilkS", "B.SilkS", "Edge.Cuts"]


def run(*cmd):
    subprocess.run(cmd, check=True, capture_output=True, text=True)


def gerbers(pcb_path: Path, out: Path, name: str) -> None:
    pcb = pcbnew.LoadBoard(str(pcb_path))
    copper = [pcbnew.LayerName(layer) for layer in pcb.GetEnabledLayers().CuStack()]
    target = out / "gerbers"
    target.mkdir()
    run("kicad-cli", "pcb", "export", "gerbers", "--output", f"{target}/",
        "--layers", ",".join(copper + FAB_LAYERS), "--no-protel-ext", str(pcb_path))
    run("kicad-cli", "pcb", "export", "drill", "--output", f"{target}/",
        "--format", "excellon", "--excellon-separate-th", str(pcb_path))
    run("kicad-cli", "pcb", "export", "ipcd356", "--output", str(target / f"{name}.d356"), str(pcb_path))
    shutil.make_archive(str(out / f"{name}_gerbers"), "zip", target)
    shutil.rmtree(target)


def odb(pcb_path: Path, out: Path, name: str) -> None:
    run("kicad-cli", "pcb", "export", "odb", "--compression", "zip", "--units", "mm",
        "--output", str(out / f"{name}_odb.zip"), str(pcb_path))


def generic_assembly(pcb_path: Path, out: Path, name: str, parts_by_ref: dict) -> None:
    """BOM grouped by part (every part, with who fits it) and a placement file
    for the assembled parts, in KiCad's coordinates and rotations."""
    pcb = pcbnew.LoadBoard(str(pcb_path))
    groups = {}
    for ref, p in parts_by_ref.items():
        fp = pcb.FindFootprintByReference(ref)
        mount = "SMD" if fp is not None and fp.GetAttributes() & pcbnew.FP_SMD else "THT"
        fitted = "assembler" if p["assembly"] == "jlc" else "hand-fit (not assembled)"
        key = (p["value"], p["footprint"].split(":")[-1], p["mpn"], p.get("lcsc", ""), mount, fitted)
        groups.setdefault(key, []).append(ref)
    with open(out / f"{name}_bom.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Quantity", "Value", "Footprint", "Manufacturer Part Number",
                    "LCSC Part #", "Mount", "Fitted by"])
        for (value, footprint, mpn, lcsc, mount, fitted), refs in sorted(groups.items(), key=lambda g: (g[0][5], g[0])):
            w.writerow([",".join(sorted(refs)), len(refs), value, footprint, mpn, lcsc, mount, fitted])
    assembled = {ref for ref, p in parts_by_ref.items() if p["assembly"] == "jlc"}
    pos = out / f"{name}_pos_all.csv"
    run("kicad-cli", "pcb", "export", "pos", "--format", "csv", "--units", "mm", "--side", "both",
        "--output", str(pos), str(pcb_path))
    with open(pos, newline="") as f:
        rows = list(csv.DictReader(f))
    pos.unlink()
    with open(out / f"{name}_cpl.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Value", "Package", "Mid X (mm)", "Mid Y (mm)", "Rotation", "Layer"])
        for row in rows:
            if row["Ref"] in assembled:
                layer = "Bottom" if row["Side"].lower().startswith("b") else "Top"
                w.writerow([row["Ref"], row["Val"], row["Package"], row["PosX"], row["PosY"], row["Rot"], layer])
