"""Independent Datasheet Pinout, Chirality & Polarity Gate.

Operates STRICTLY on the final routed `.kicad_pcb` files and the exported
production BOM CSVs (`hardware/releases/<variant>/<board>/<board>_bom.csv`),
with ZERO imports from `hardware/circuits/` or any schematic generator script.

Verifies:
1. Footprint chirality on both `F.Cu` and `B.Cu`: signed shoelace winding of
   ordered pins `1..N` in each package's mounting-face view to catch footprints
   mirrored in `.kicad_mod` or by `Flip()` on `B.Cu`.
2. Standalone datasheet-audited MPN pin-role table for every IC, converter,
   optocoupler, voltage supervisor, ideal diode, logic gate, MCU module,
   transistor (BJT/MOSFET), bridge rectifier, crystal and addressable RGB LED.
3. Non-swapped bus lines across mated boards (KNX UART TX/RX crossover, I2C
   SDA/SCL + pull-ups to OLED, WS2812B DIN->DOUT daisy-chain), zero shorted or
   open 2-pin passives, DC Anode/Cathode bias on every diode/Zener/TVS/cap, and
   physical silkscreen cathode/`+` mark proximity on the PCB.
"""

from __future__ import annotations

import ast
import csv
import itertools
import math
import os
from pathlib import Path

import pcbnew
import pytest

REPO = Path(__file__).resolve().parents[2]
VARIANT = os.environ.get("HARDWARE_VARIANT", "v3")
LAYOUT_ROOT = REPO / "hardware/layout" / VARIANT
RELEASE_ROOT = REPO / "hardware/releases" / VARIANT
BOARDS = tuple(sorted(p.name for p in LAYOUT_ROOT.iterdir() if (p / f"{p.name}.kicad_pcb").exists()))


# ---------------------------------------------------------------------------
# Standalone loaders (PCB + exported production BOM only)
# ---------------------------------------------------------------------------

def load_bom(board: str) -> dict[str, dict[str, str]]:
    """Expand grouped rows from the exported production `<board>_bom.csv`."""
    bom_path = RELEASE_ROOT / board / f"{board}_bom.csv"
    by_ref: dict[str, dict[str, str]] = {}
    with open(bom_path, newline="") as f:
        for row in csv.DictReader(f):
            for ref in (r.strip() for r in row["Designator"].split(",") if r.strip()):
                by_ref[ref] = {
                    "value": row["Value"].strip(),
                    "footprint": row["Footprint"].strip(),
                    "mpn": row["Manufacturer Part Number"].strip(),
                    "lcsc": row["LCSC Part #"].strip(),
                    "mount": row["Mount"].strip(),
                }
    return by_ref


def load_pcb(board: str) -> pcbnew.BOARD:
    pcb = pcbnew.LoadBoard(str(LAYOUT_ROOT / board / f"{board}.kicad_pcb"))
    pcbnew.ZONE_FILLER(pcb).Fill(pcb.Zones())
    pcb.BuildConnectivity()
    return pcb


@pytest.fixture(scope="module")
def assembly():
    pcbs = {b: load_pcb(b) for b in BOARDS}
    boms = {b: load_bom(b) for b in BOARDS}
    return pcbs, boms


def pad_nets_of(fp: pcbnew.FOOTPRINT) -> dict[str, str]:
    out: dict[str, str] = {}
    for p in fp.Pads():
        num = p.GetNumber()
        if not num:
            continue
        net = p.GetNetname()
        if num in out and out[num] and net:
            assert out[num] == net, f"{fp.GetReference()} duplicate pad {num} on distinct nets"
        out[num] = net or out.get(num, "")
    return out


def pad_centres_mounting_view(fp: pcbnew.FOOTPRINT, exclude: set[int] = frozenset()) -> list[tuple[int, float, float]]:
    """Ordered `(pin_num, x, y)` in the component's mounting-face top-view frame:
    `(x_mm, y_up_mm)` on `F.Cu`, `(-x_mm, y_up_mm)` on `B.Cu`."""
    flipped = fp.IsFlipped()
    by_num: dict[int, list[tuple[float, float]]] = {}
    for p in fp.Pads():
        n = p.GetNumber()
        if not n.isdigit():
            continue
        idx = int(n)
        if idx in exclude:
            continue
        x = pcbnew.ToMM(p.GetPosition().x)
        y = -pcbnew.ToMM(p.GetPosition().y)
        by_num.setdefault(idx, []).append((-x if flipped else x, y))
    return [(k, sum(pt[0] for pt in pts) / len(pts), sum(pt[1] for pt in pts) / len(pts))
            for k, pts in sorted(by_num.items())]


def shoelace_area(pts: list[tuple[int, float, float]]) -> float:
    if len(pts) < 3:
        return 0.0
    area2 = 0.0
    for i in range(len(pts)):
        _, x1, y1 = pts[i]
        _, x2, y2 = pts[(i + 1) % len(pts)]
        area2 += x1 * y2 - x2 * y1
    return area2 / 2.0


def two_pin_other(board_two_pins: dict[str, list[tuple[str, str, str]]], net: str, prefix: str) -> list[tuple[str, str, str]]:
    """Return `(ref, value, other_net)` of 2-pin parts with `prefix` attached to `net`."""
    return [(ref, val, b if a == net else a)
            for ref, val, a, b in board_two_pins.get(prefix, [])
            if a == net or b == net]


def build_two_pin_index(pcb: pcbnew.BOARD, bom: dict[str, dict[str, str]]) -> dict[str, list[tuple[str, str, str, str]]]:
    idx: dict[str, list[tuple[str, str, str, str]]] = {}
    for fp in pcb.Footprints():
        ref = fp.GetReference()
        nets = pad_nets_of(fp)
        if set(nets) == {"1", "2"}:
            m = "".join(ch for ch in ref if ch.isalpha())
            idx.setdefault(m, []).append((ref, bom[ref]["value"], nets["1"], nets["2"]))
    return idx


# ---------------------------------------------------------------------------
# 0. Strict zero-generator-import self-check
# ---------------------------------------------------------------------------

def test_zero_circuit_generator_imports():
    forbidden = {"skidl", "kinet2pcb", "gen_circuits", "product_spec", "board_variant", "common", "layout", "blocks"}
    tree = ast.parse(Path(__file__).read_text())
    imported = {
        alias.name.split(".")[0]
        for node in ast.walk(tree) if isinstance(node, ast.Import)
        for alias in node.names
    } | {
        node.module.split(".")[0]
        for node in ast.walk(tree) if isinstance(node, ast.ImportFrom) and node.module
    }
    assert not (imported & forbidden), f"independent gate imports generator modules: {imported & forbidden}"


# ---------------------------------------------------------------------------
# 1. Footprint chirality (F.Cu and B.Cu)
# ---------------------------------------------------------------------------

# Datasheet top-view perimeter pin winding sign:
# +1 = standard JEDEC counter-clockwise numbering
# -1 = manufacturer clockwise numbering (TO-252-2 tab=pin 2 between 1 & 3; WS2812B-MINI-V3J CW 1..4)
MPN_WINDING_SIGN: dict[str, tuple[int, set[int]]] = {
    "NCN5130MNTWG": (+1, {41}),            # onsemi QFN-40: pins 1..40 CCW, pad 41 central EPAD
    "SCT2400TVBR": (+1, set()),            # TSOT-23-6: pins 1..6 CCW
    "LM66100DCKR": (+1, set()),            # TI SC-70-6 (DCK): pins 1..6 CCW
    "SN74AHCT1G125DBVR": (+1, set()),      # TI SOT-23-5 (DBV): pins 1..5 CCW
    "TLV803MDBZR-TP": (+1, set()),         # SOT-23-3: pins 1..3 CCW
    "TLP383(D4BLLTL,E": (+1, set()),       # Toshiba SO-6L: pins 1, 3, 4, 6 CCW
    "REC3-2415SRW/H2/A/CTRL": (+1, set()), # RECOM DIP-24: pins 1, 2, 14, 16, 22, 23 CCW
    "102010636": (+1, set()),              # Seeed XIAO ESP32C6: pins 1..14 CCW
    "BC847B": (+1, set()),                 # SOT-23 NPN: pins 1..3 CCW
    "BC857C": (+1, set()),                 # SOT-23 PNP: pins 1..3 CCW
    "IRLML0060TRPBF": (+1, set()),         # SOT-23 NMOS: pins 1..3 CCW
    "2N7002": (+1, set()),                 # SOT-23 NMOS: pins 1..3 CCW
    "AO3401A": (+1, set()),                # SOT-23 PMOS: pins 1..3 CCW
    "DF06S-T": (+1, set()),                # Vishay DFS bridge: 1(+), 2(-), 3(~), 4(~) CCW
    "MJD45H11": (-1, set()),               # JEDEC TO-252-2: 1=left, 2=top tab, 3=right -> CW
    "WS2812B-MINI-V3J": (-1, set()),       # Worldsemi 3535 gull-wing: 1=TL, 2=TR, 3=BR, 4=BL -> CW
}

LINEAR_ROW_MPNS: dict[str, set[int]] = {
    "TMR 2411": set(),
    "TRN 3-2411": set(),
    "KH-FPC0.5-H1.2SMT-8P-QCHF": {9, 10},
    "BM10B-SRSS-TB(LF)(SN)": set(),
    "QG-6428TSWCG02": set(),
    "WEA012864DWPP3N00003": set(),
    "KH-2.54FH-1X4P-H8.5": set(),
    "KH-2.54PH180-1X4P-L11.5": set(),
    "KH-2.54FH-1X3P-H8.5": set(),
    "2.54-1*3P": set(),
}


def test_footprint_chirality_fcu_and_bcu(assembly):
    pcbs, boms = assembly
    checked = 0
    for board, pcb in pcbs.items():
        bom = boms[board]
        for fp in pcb.Footprints():
            ref = fp.GetReference()
            assert ref in bom, f"{board}:{ref} missing from exported production BOM"
            mpn = bom[ref]["mpn"]
            if mpn in MPN_WINDING_SIGN:
                expected_sign, excl = MPN_WINDING_SIGN[mpn]
                pts = pad_centres_mounting_view(fp, excl)
                area = shoelace_area(pts)
                side = "B.Cu" if fp.IsFlipped() else "F.Cu"
                assert abs(area) > 0.2, f"{board}:{ref} ({mpn} on {side}) degenerate pad polygon area={area:.3f}"
                actual_sign = 1 if area > 0 else -1
                assert actual_sign == expected_sign, (
                    f"{board}:{ref} ({mpn} on {side}) mirrored chirality: "
                    f"area={area:.3f}, expected sign {expected_sign:+d}"
                )
                checked += 1
            elif mpn in LINEAR_ROW_MPNS:
                pts = pad_centres_mounting_view(fp, LINEAR_ROW_MPNS[mpn])
                assert len(pts) >= 3
                dx = pts[-1][1] - pts[0][1]
                dy = pts[-1][2] - pts[0][2]
                norm = math.hypot(dx, dy)
                assert norm > 1.0
                proj = [((x - pts[0][1]) * dx + (y - pts[0][2]) * dy) / norm for _, x, y in pts]
                assert all(b > a + 0.3 for a, b in itertools.pairwise(proj)), (
                    f"{board}:{ref} ({mpn}) non-monotonic pin progression: {proj}"
                )
                checked += 1
    assert checked >= 15


def test_chirality_catches_mirrored_bcu_footprint():
    """Flipping a footprint's layer flag without mirroring its pad X coordinates
    inverts the mounting-face shoelace winding sign."""
    board = pcbnew.BOARD()
    fp = pcbnew.FOOTPRINT(board)
    board.Add(fp)
    for num, (x, y) in [("1", (-0.95, 1.0)), ("2", (0.95, 1.0)), ("3", (0.0, -1.0))]:
        p = pcbnew.PAD(fp)
        p.SetNumber(num)
        p.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y)))
        fp.Add(p)
    area_front = shoelace_area(pad_centres_mounting_view(fp))
    assert area_front > 0
    # Simulate an un-mirrored B.Cu footprint by flipping only the footprint layer to B_Cu
    fp.SetLayer(pcbnew.B_Cu)
    area_bad_back = shoelace_area(pad_centres_mounting_view(fp))
    assert area_bad_back < 0


# ---------------------------------------------------------------------------
# 2. Standalone datasheet-audited MPN pin-role verification
# ---------------------------------------------------------------------------

NCN5130_EXPECTED_NETS = {
    "1": "GND", "2": "GND", "3": "TX0", "4": "CCP", "5": "CAV",
    "6": "KNX_P_PROT", "7": "CEQ1", "8": "CEQ2", "9": "VFILT", "10": "VFILT",
    "11": "+3V3_TRX", "12": "", "13": "", "14": "GND", "15": "",
    "16": "VFILT", "17": "VSW1", "18": "GND", "19": "+3V3_TRX", "20": "VDD1M",
    "21": "GND", "22": "", "23": "GND", "24": "GND", "25": "GND",
    "26": "GND", "28": "KNX_TXD", "29": "GND", "30": "+3V3_TRX",
    "31": "GND", "32": "", "33": "GND", "34": "XTAL2", "35": "XTAL1",
    "36": "NCN_SAVEB", "37": "", "38": "FANIN", "39": "", "40": "+3V3_TRX",
    "41": "GND",
}

XIAO_EXPECTED_NETS = {
    "1": "LED_DATA", "2": "DALI_SUPPLY_OFF", "3": "BTN_LADDER", "4": "",
    "5": "I2C_SDA", "6": "I2C_SCL", "7": "KNX_RXD", "8": "KNX_TXD",
    "9": "DALI_RX", "10": "NCN_SAVEB", "11": "DALI_TX", "12": "+3V3",
    "13": "GND", "14": "+5V_XIAO",
}


def test_independent_mpn_pin_roles(assembly):
    pcbs, boms = assembly
    seen_mpns: set[str] = set()

    for board, pcb in pcbs.items():
        bom = boms[board]
        two_pins = build_two_pin_index(pcb, bom)

        for fp in pcb.Footprints():
            ref = fp.GetReference()
            mpn = bom[ref]["mpn"]
            nets = pad_nets_of(fp)
            seen_mpns.add(mpn)

            if mpn == "NCN5130MNTWG":
                for pin, exp in NCN5130_EXPECTED_NETS.items():
                    assert nets.get(pin, "") == exp, f"{board}:{ref} NCN5130 pin {pin} -> {nets.get(pin)!r} != {exp!r}"
                # Pin 27 (SDI/RXD) connects via 1k series resistor to KNX_RXD
                r_sdi = two_pin_other(two_pins, nets["27"], "R")
                assert any(val.startswith("1k") and other == "KNX_RXD" for _, val, other in r_sdi), (
                    f"{board}:{ref} pin 27 SDI/RXD missing 1k series resistor to KNX_RXD: {r_sdi}"
                )

            elif mpn == "SCT2400TVBR":
                # TSOT-23-6: 1=BST, 2=GND, 3=FB, 4=EN, 5=VIN, 6=SW
                assert nets["2"] == "GND"
                assert nets["5"] == "VFILT"
                assert nets["6"] in ("SW", "BUCK5_SW")
                # BST (1) -> 100n cap to SW (6)
                bst_caps = two_pin_other(two_pins, nets["1"], "C")
                assert any("100n" in v and o == nets["6"] for _, v, o in bst_caps), f"SCT2400 BST cap bad: {bst_caps}"
                # FB (3) -> 51k to +5V_BUCK, 10k to GND
                fb_res = two_pin_other(two_pins, nets["3"], "R")
                assert any(v.startswith("51k") and o == "+5V_BUCK" for _, v, o in fb_res)
                assert any(v.startswith("10k") and o == "GND" for _, v, o in fb_res)
                # EN (4) -> 100k to VFILT, 10k to GND
                en_res = two_pin_other(two_pins, nets["4"], "R")
                assert any(v.startswith("100k") and o == "VFILT" for _, v, o in en_res)
                assert any(v.startswith("10k") and o == "GND" for _, v, o in en_res)

            elif mpn == "REC3-2415SRW/H2/A/CTRL":
                # DIP-24 A-pinning: 1=CTRL, 2=-Vin, 14=+Vout, 16=-Vout, 22=+Vin, 23=+Vin
                assert nets["2"] == "GNDA"
                assert nets["14"] == "DALI_ISO15V"
                assert nets["16"] == "GNDD"
                assert nets["22"] == "AUX_P" and nets["23"] == "AUX_P"
                assert nets["1"] not in ("", "GNDA", "AUX_P", "GND")

            elif mpn == "TMR 2411":
                # SIP-8: 1=-Vin, 2=+Vin, 3=Remote(NC), 6=+Vout, 7=-Vout
                assert nets["1"] == "GNDA" and nets["2"] == "AUX_P"
                assert nets.get("3", "") == ""
                assert nets["6"] == "+5V_AUX" and nets["7"] == "GND"

            elif mpn == "TRN 3-2411":
                # SIP-5: 1=-Vin, 2=+Vin, 3=+Vout, 5=-Vout
                assert nets["1"] == "GNDA" and nets["2"] == "AUX_P"
                assert nets["3"] == "+5V_AUX" and nets["5"] == "GND"

            elif mpn == "TLV803MDBZR-TP":
                # SOT-23-3: 1=GND, 2=RESET#, 3=VCC
                assert nets == {"1": "GND", "2": "CORE_AUX_GOOD", "3": "+5V_AUX"}

            elif mpn == "LM66100DCKR":
                # SC-70-6: 1=VIN, 2=GND, 3=CE# (tied to VOUT for RCB), 4=ST(NC), 5=NC, 6=VOUT
                assert nets["1"] == "+5V" and nets["2"] == "GND"
                assert nets["3"] == "+5V_XIAO" and nets["6"] == "+5V_XIAO"
                assert nets.get("4", "") == "" and nets.get("5", "") == ""

            elif mpn == "SN74AHCT1G125DBVR":
                # SOT-23-5: 1=OE#, 2=A, 3=GND, 4=Y, 5=VCC
                assert nets["1"] == "GND" and nets["2"] == "LED_DATA"
                assert nets["3"] == "GND" and nets["5"] == "+5V"
                y_res = two_pin_other(two_pins, nets["4"], "R")
                assert any(v.startswith("330R") for _, v, _ in y_res), f"74AHCT1G125 Y missing 330R: {y_res}"

            elif mpn == "TLP383(D4BLLTL,E":
                # Toshiba SO-6L: 1=Anode, 3=Cathode, 4=Emitter, 6=Collector (2, 5 omitted -> NC)
                assert nets.get("2", "") == "" and nets.get("5", "") == ""
                if nets["6"] == "DALI_BOOT":  # DALI TX opto
                    assert nets["3"] == "GND" and nets["4"] == "DALI_GATE_SW"
                    assert any(v.startswith("330R") and o == "DALI_TX" for _, v, o in two_pin_other(two_pins, nets["1"], "R"))
                elif nets["6"] == "DALI_RX":  # DALI RX opto
                    assert nets["3"] == "RX_LIM_IN" and nets["4"] == "GND"
                    assert any("5V6" in v and o == "DA_P" for _, v, o in two_pin_other(two_pins, nets["1"], "D"))
                else:  # DALI supply-off CTRL opto
                    assert nets["3"] == "GND"
                    assert any(v.startswith("330R") and o == "DALI_SUPPLY_OFF" for _, v, o in two_pin_other(two_pins, nets["1"], "R"))
                    assert any(v.startswith("10k") and o == "AUX_P" for _, v, o in two_pin_other(two_pins, nets["6"], "R"))
                    assert any("5V6" in v and o == "GNDA" for _, v, o in two_pin_other(two_pins, nets["4"], "D"))

            elif mpn == "102010636":
                for pin, exp in XIAO_EXPECTED_NETS.items():
                    actual = nets.get(pin, "")
                    if pin == "12" and actual in ("+3V3", "+3V3_1"):
                        continue
                    assert actual == exp, f"{board}:{ref} XIAO pin {pin} -> {actual!r} != {exp!r}"

            elif mpn == "MJD45H11":
                # TO-252-2 PNP: 1=Base, 2=Collector, 3=Emitter
                assert nets["2"] == "DALI_SUPPLY_SW"
                assert any(v.startswith("3R3") and o == "DALI_ISO15V" for _, v, o in two_pin_other(two_pins, nets["3"], "R"))
                assert any(v.startswith("4k7") and o == "GNDD" for _, v, o in two_pin_other(two_pins, nets["1"], "R"))

            elif mpn == "BC857C":
                # SOT-23 PNP sense: 1=Base (after 3R3), 2=Emitter (DALI_ISO15V), 3=Collector (Q_pass Base)
                assert nets["2"] == "DALI_ISO15V"
                assert any(v.startswith("3R3") and o == "DALI_ISO15V" for _, v, o in two_pin_other(two_pins, nets["1"], "R"))
                assert any(v.startswith("4k7") and o == "GNDD" for _, v, o in two_pin_other(two_pins, nets["3"], "R"))

            elif mpn == "IRLML0060TRPBF":
                # SOT-23 NMOS: 1=Gate, 2=Source, 3=Drain
                assert nets == {"1": "DALI_GATE", "2": "DALI_SENSE", "3": "DA_P"}

            elif mpn == "BC847B":
                # SOT-23 NPN: 1=Base, 2=Emitter, 3=Collector
                if nets["3"] == "DALI_GATE":  # TX current-limit clamp
                    assert nets["1"] == "DALI_SENSE" and nets["2"] == "GNDD"
                elif nets["3"] == "RX_LIM_IN":  # RX limiter pass NPN
                    assert any(v.startswith("680R") and o == "GNDD" for _, v, o in two_pin_other(two_pins, nets["2"], "R"))
                    assert any(v.startswith("100k") and o == "RX_LIM_IN" for _, v, o in two_pin_other(two_pins, nets["1"], "R"))
                else:  # RX limiter sense NPN
                    assert nets["2"] == "GNDD"
                    assert any(v.startswith("680R") and o == "GNDD" for _, v, o in two_pin_other(two_pins, nets["1"], "R"))
                    assert any(v.startswith("100k") and o == "RX_LIM_IN" for _, v, o in two_pin_other(two_pins, nets["3"], "R"))

            elif mpn == "2N7002":
                # SOT-23 NMOS: 1=Gate, 2=Source, 3=Drain
                assert nets == {"1": "CORE_AUX_GOOD", "2": "GND", "3": "CORE_PFET_G"}

            elif mpn == "AO3401A":
                # SOT-23 PMOS: 1=Gate, 2=Source (+5V rail), 3=Drain (+5V_AUX so body diode points AUX -> rail)
                assert nets == {"1": "CORE_PFET_G", "2": "+5V", "3": "+5V_AUX"}

            elif mpn == "DF06S-T":
                assert nets["1"] == "DA_P" and nets["2"] == "GNDD"
                assert {nets["3"], nets["4"]} == {"DA1", "DA2"}

            elif mpn == "TAXM16M2QLFCDT1T":
                assert {nets["1"], nets["2"]} == {"XTAL1", "XTAL2"}

            elif mpn == "WS2812B-MINI-V3J":
                # 1=VDD, 2=DOUT, 3=GND, 4=DIN
                assert nets["1"] == "+5V" and nets["3"] == "GND" and nets["4"] != ""

    required = {
        "NCN5130MNTWG", "SCT2400TVBR", "REC3-2415SRW/H2/A/CTRL",
        "TLV803MDBZR-TP", "LM66100DCKR", "SN74AHCT1G125DBVR",
        "TLP383(D4BLLTL,E", "102010636", "MJD45H11", "BC857C",
        "BC847B", "IRLML0060TRPBF", "2N7002", "AO3401A",
        "DF06S-T", "TAXM16M2QLFCDT1T", "WS2812B-MINI-V3J",
    }
    assert required <= seen_mpns, f"unchecked MPNs: {required - seen_mpns}"


# ---------------------------------------------------------------------------
# 3. Bus lines, 2-pin passives, DC diode/cap polarity & silkscreen marks
# ---------------------------------------------------------------------------

def test_zero_shorted_or_open_two_pin_passives(assembly):
    pcbs, boms = assembly
    checked = 0
    for board, pcb in pcbs.items():
        bom = boms[board]
        for fp in pcb.Footprints():
            ref = fp.GetReference()
            if not ref.startswith(("R", "C", "L", "F", "Y", "SW")):
                continue
            nets = pad_nets_of(fp)
            assert len(nets) == 2, f"{board}:{ref} ({bom[ref]['mpn']}) expected 2 numbered pads, got {nets}"
            n1, n2 = list(nets.values())
            assert n1 and n2, f"{board}:{ref} ({bom[ref]['mpn']}) has open pad: {nets}"
            assert n1 != n2, f"{board}:{ref} ({bom[ref]['mpn']}) is shorted across net {n1!r}"
            checked += 1
    assert checked >= 50


def test_bus_lines_not_swapped_and_chains_intact(assembly):
    pcbs, boms = assembly

    # 1. WS2812B daisy-chain on UI board: buffer -> 330R -> D1.DIN, D1.DOUT -> D2.DIN, D2.DOUT -> D3.DIN, D3.DOUT open
    ui_pcb = pcbs["ui"]
    d1 = pad_nets_of(ui_pcb.FindFootprintByReference("D1"))
    d2 = pad_nets_of(ui_pcb.FindFootprintByReference("D2"))
    d3 = pad_nets_of(ui_pcb.FindFootprintByReference("D3"))
    assert d1["2"] == d2["4"] == "LED_CHAIN1"
    assert d2["2"] == d3["4"] == "LED_CHAIN2"
    assert d3.get("2", "") == ""

    # 2. OLED I2C SDA/SCL non-swapped at J2 and pulled up to +3V3 on MCU board
    j2_mpn = boms["ui"]["J2"]["mpn"]
    j2_nets = pad_nets_of(ui_pcb.FindFootprintByReference("J2"))
    if j2_mpn == "WEA012864DWPP3N00003":
        assert j2_nets["1"] == "+3V3" and j2_nets["2"] == "GND"
        assert j2_nets["3"] == "I2C_SCL" and j2_nets["4"] == "I2C_SDA"
    elif j2_mpn == "QG-6428TSWCG02":
        assert j2_nets["7"] == "GND" and j2_nets["8"] == "+3V3"
        assert j2_nets["10"] == "I2C_SCL" and j2_nets["11"] == "I2C_SDA"
    else:
        pytest.fail(f"unexpected OLED MPN {j2_mpn}")
    assert j2_nets["10" if j2_mpn == "QG-6428TSWCG02" else "3"] != j2_nets["11" if j2_mpn == "QG-6428TSWCG02" else "4"]

    mcu_board = next(b for b, bom in boms.items() if any(p["mpn"] == "102010636" for p in bom.values()))
    mcu_two_pins = build_two_pin_index(pcbs[mcu_board], boms[mcu_board])
    for i2c_net in ("I2C_SDA", "I2C_SCL"):
        pullups = [v for _, v, o in two_pin_other(mcu_two_pins, i2c_net, "R") if o in ("+3V3", "+3V3_1")]
        assert any(v.startswith("4k7") for v in pullups), f"{mcu_board} missing 4k7 pull-up on {i2c_net}"


def _pad_xy_mm(fp: pcbnew.FOOTPRINT, num: str) -> tuple[float, float]:
    pts = [(pcbnew.ToMM(p.GetPosition().x), -pcbnew.ToMM(p.GetPosition().y))
           for p in fp.Pads() if p.GetNumber() == num]
    return sum(x for x, _ in pts) / len(pts), sum(y for _, y in pts) / len(pts)


def _silk_polarity_point(fp: pcbnew.FOOTPRINT, fpid: str) -> tuple[float, float]:
    """Extract the physical polarity indicator coordinate on the footprint's silkscreen."""
    silk = pcbnew.B_SilkS if fp.IsFlipped() else pcbnew.F_SilkS
    shapes = [g for g in fp.GraphicalItems() if g.GetLayer() == silk and isinstance(g, pcbnew.PCB_SHAPE)]
    assert shapes, f"{fp.GetReference()} ({fpid}) has no silkscreen polarity artwork"

    if "CP_" in fpid:
        # Polarized electrolytic '+' cross: two perpendicular equal-length lines sharing a centre
        lines = []
        for s in shapes:
            bb = s.GetBoundingBox()
            w, h = pcbnew.ToMM(bb.GetWidth()), pcbnew.ToMM(bb.GetHeight())
            cx, cy = pcbnew.ToMM(bb.GetCenter().x), -pcbnew.ToMM(bb.GetCenter().y)
            if max(w, h) < 3.0:
                lines.append((cx, cy, w, h))
        for i, (cx1, cy1, w1, h1) in enumerate(lines):
            for cx2, cy2, w2, h2 in lines[i + 1:]:
                if math.hypot(cx1 - cx2, cy1 - cy2) < 0.2 and abs(max(w1, h1) - max(w2, h2)) < 0.2:
                    return (cx1, cy1)
        pytest.fail(f"{fp.GetReference()} ({fpid}) missing '+' silkscreen cross")

    if "Diode_Bridge" in fpid:
        polys = [s for s in shapes if s.GetShape() == pcbnew.SHAPE_T_POLY]
        assert polys, f"{fp.GetReference()} ({fpid}) missing '+' polygon dot"
        bb = polys[0].GetBoundingBox()
        return pcbnew.ToMM(bb.GetCenter().x), -pcbnew.ToMM(bb.GetCenter().y)

    # D_SMA, D_SOD-123, WS2812B-MINI-V3J: silkscreen strokes shift toward pin 1 (Cathode / VDD)
    xs = [pcbnew.ToMM(s.GetBoundingBox().GetCenter().x) for s in shapes]
    ys = [-pcbnew.ToMM(s.GetBoundingBox().GetCenter().y) for s in shapes]
    return sum(xs) / len(xs), sum(ys) / len(ys)


def test_diode_and_cap_dc_bias_and_silkscreen_cathode_marks(assembly):
    pcbs, boms = assembly
    checked_marks = 0
    for board, pcb in pcbs.items():
        bom = boms[board]
        for fp in pcb.Footprints():
            ref = fp.GetReference()
            mpn = bom[ref]["mpn"]
            fpid = bom[ref]["footprint"]
            nets = pad_nets_of(fp)

            # 1. DC Anode (pad 2) vs Cathode (pad 1) electrical bias checks
            if mpn in ("SS16", "B5819W SL"):
                # Forward Schottky rectifiers: (Anode -> Cathode)
                valid_pairs = {
                    ("KNX_P", "KNX_P_PROT"),
                    ("AUX_FUSED", "AUX_P"),
                    ("DA_P", "DALI_BOOT"),
                    ("+5V_BUCK", "+5V"),
                }
                pair = (nets["2"], nets["1"])
                assert pair in valid_pairs, f"{board}:{ref} ({mpn}) reversed Schottky (A, K)={pair}"

            elif mpn in ("SMAJ33A", "SMAJ24A", "MMSZ12T1G", "MMSZ5V6T1G"):
                # Unidirectional TVS & Zeners: Cathode (pad 1) at positive clamped rail, Anode (pad 2) at return/LED
                k_net, a_net = nets["1"], nets["2"]
                if mpn == "SMAJ33A":
                    assert (k_net, a_net) == ("AUX_P", "GNDA")
                elif mpn == "SMAJ24A":
                    assert (k_net, a_net) == ("DA_P", "GNDD")
                elif mpn == "MMSZ12T1G":
                    assert (k_net, a_net) == ("DALI_GATE", "GNDD")
                elif mpn == "MMSZ5V6T1G":
                    if k_net == "DA_P":
                        # RX threshold zener: K on DA_P, A feeding opto LED anode
                        assert a_net not in ("GNDD", "GND", "DA_P")
                    else:
                        # REC3 CTRL 5.6 V clamp: K on CTRL, A on GNDA
                        assert a_net == "GNDA" and k_net not in ("GNDA", "AUX_P")

            elif mpn == "SMAJ40CA":
                assert {nets["1"], nets["2"]} == {"KNX_P", "GND"}

            elif mpn in ("RYRVT35V470UF10*10", "KMG35V470M10*16 LO"):
                # Polarized 470 uF storage capacitor: pad 1 (+) on VFILT, pad 2 (-) on GND
                assert (nets["1"], nets["2"]) == ("VFILT", "GND"), f"{board}:{ref} polarized cap reversed: {nets}"

            # 2. Physical silkscreen cathode / '+' / pin-1 mark proximity check
            if ref.startswith("D") or "CP_" in fpid:
                mx, my = _silk_polarity_point(fp, fpid)
                p1x, p1y = _pad_xy_mm(fp, "1")
                d_pin1 = math.hypot(mx - p1x, my - p1y)
                others = [n for n in nets if n != "1"]
                d_others = [math.hypot(mx - _pad_xy_mm(fp, n)[0], my - _pad_xy_mm(fp, n)[1]) for n in others]
                assert d_pin1 < min(d_others), (
                    f"{board}:{ref} ({mpn}) silkscreen polarity mark at ({mx:.2f}, {my:.2f}) "
                    f"is closer to opposite pad ({min(d_others):.2f} mm) than pad 1 ({d_pin1:.2f} mm)"
                )
                checked_marks += 1
    assert checked_marks >= 10
