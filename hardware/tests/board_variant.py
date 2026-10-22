"""The hardware variant under test (HARDWARE_VARIANT, default v3) and the
paths of its stored boards and generated netlists."""

import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "hardware/circuits"))
from product_spec import load_spec

SPEC = load_spec()
VARIANT = SPEC["active_variant"]
BOARDS = tuple(SPEC["variants"][VARIANT]["boards"])


def layout_path(board, suffix=".kicad_pcb"):
    return REPO / "hardware/layout" / VARIANT / board / f"{board}{suffix}"


def build_path(board, name):
    return REPO / "hardware/build" / VARIANT / board / name


def layout_plan(board):
    import yaml

    return yaml.safe_load((REPO / "hardware/circuits/variants" / VARIANT / f"layout_{board}.yaml").read_text())


def parse_netlist(board="main"):
    """Parse the SKiDL netlist and return net -> list of (ref, pin)."""
    net_path = REPO / "hardware/build" / VARIANT / board / f"{board}.net"
    text = net_path.read_text()
    nets = {}
    nets_start = text.find("(nets")
    if nets_start == -1:
        return nets
    pos = nets_start
    while pos < len(text):
        idx = text.find("(net\n", pos)
        if idx == -1:
            idx = text.find("(net\r", pos)
        if idx == -1:
            break
        name_match = re.search(r'\(name\s+"([^"]+)"\)', text[idx:idx+200])
        if not name_match:
            pos = idx + 5
            continue
        net_name = name_match.group(1)
        block_end = len(text)
        next_net = text.find("(net\n", idx + 5)
        if next_net == -1:
            next_net = text.find("(net\r", idx + 5)
        if next_net != -1:
            block_end = next_net
        block = text[idx:block_end]
        nodes = []
        for nm in re.finditer(
            r'\(node\s*\(ref\s+"([^"]+)"\)\s*\(pin\s+"([^"]+)"\)',
            block
        ):
            nodes.append((nm.group(1), nm.group(2)))
        if nodes:
            nets[net_name] = nodes
        pos = block_end
    return nets


def parse_parts(board="main"):
    """Parse parts.json and return ref -> part info."""
    parts_path = REPO / "hardware/build" / VARIANT / board / "parts.json"
    parts = json.loads(parts_path.read_text())
    return {p["ref"]: p for p in parts}


def merged_netlists():
    merged = {}
    for b in SPEC["variants"][VARIANT]["boards"]:
        for net, nodes in parse_netlist(b).items():
            merged.setdefault(net, []).extend(nodes)
    return merged


def merged_parts():
    merged = {}
    for b in SPEC["variants"][VARIANT]["boards"]:
        for ref, info in parse_parts(b).items():
            merged.setdefault(ref, info)
    return merged
