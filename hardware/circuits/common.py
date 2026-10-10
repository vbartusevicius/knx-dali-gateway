# SPDX-FileCopyrightText: The knx-dali-gateway contributors
# SPDX-License-Identifier: CERN-OHL-P-2.0
"""Shared helpers for the SKiDL circuit sources.

Every part carries sourcing fields (Manufacturer / Manufacturer_Part_Number /
LCSC / Source) so the generated BOM keeps provenance. Values marked TBD are
explicit placeholders for the electrical review, not omissions.
"""

import os
from pathlib import Path

import yaml
from skidl import KICAD, Net, Part

REPO_ROOT = Path(__file__).resolve().parents[2]
SPEC_PATH = REPO_ROOT / "hardware" / "product" / "spec.yaml"
PROJECT_LIB = Path(__file__).resolve().parent / "lib" / "gateway.kicad_sym"

REVIEW = "draft — needs electrical review"


def load_spec():
    with open(SPEC_PATH, encoding="utf-8") as fh:
        return yaml.safe_load(fh)


PLACE_BLOCK = "misc"


def block(name):
    """Set the functional block recorded on subsequently created parts."""
    global PLACE_BLOCK
    PLACE_BLOCK = name


def part(lib, symbol, value="", footprint="", fields=None, ref=None):
    """Instantiate a stock or project-library part with sourcing fields."""
    lib_ref = str(PROJECT_LIB) if lib == "gateway" else lib
    p = Part(lib_ref, symbol, tool=KICAD, tag=PLACE_BLOCK)
    if ref:
        p.ref = ref
    if value:
        p.value = value
    if footprint:
        p.footprint = footprint
    p.fields["Review"] = REVIEW
    p.fields["Place"] = PLACE_BLOCK
    for key, val in (fields or {}).items():
        p.fields[key] = val
    return p


def src(mpn="TBD", mfr="TBD", lcsc="TBD", source="TBD"):
    return {
        "Manufacturer": mfr,
        "Manufacturer_Part_Number": mpn,
        "LCSC": lcsc,
        "Source": source,
    }


def power(name):
    """Attach a stock power symbol to (creating) a named net."""
    net = Net(name)
    sym = Part("power", name, tool=KICAD, tag="power")
    sym.footprint = ""  # power symbols have no footprint; silences netlist check
    sym["1"] += net
    return net


def power_flag(net):
    flag = Part("power", "PWR_FLAG", tool=KICAD, tag="power")
    flag.footprint = ""
    flag["1"] += net


def drive_pin(part_obj, pin, net):
    part_obj[pin] += net
