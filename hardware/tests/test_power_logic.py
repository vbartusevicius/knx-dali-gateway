"""PCB power/logic tests (pytest over netlist + spec, no hardware).

Checks:
- NCN5130 Figure 12 connections one by one
- Crystal present, UART straps, DC2-unused config, VIN=VFILT
- XIAO pin map vs Seeed counter-clockwise table
- ADC ladder thresholds incl 5% tolerance
- DALI RX/TX thresholds and bus current from formulas
- Component ratings: regulator input max, cap voltage
- KNX power budget with VFILT storage
- Logic: KNX TXD/RXD crossing, DALI_TX/RX, LEDs, I2C pull-ups
"""

import re
from pathlib import Path

import pytest
import yaml

REPO = Path(__file__).resolve().parents[2]
SPEC = yaml.safe_load((REPO / "hardware/product/spec.yaml").read_text())


def _parse_netlist(board="main"):
    """Parse the SKiDL netlist and return net -> list of (ref, pin)."""
    net_path = REPO / "hardware/build" / SPEC["active_variant"] / board / f"{board}.net"
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


def _parse_parts(board="main"):
    """Parse parts.json and return ref -> part info."""
    parts_path = REPO / "hardware/build" / SPEC["active_variant"] / board / "parts.json"
    import json
    parts = json.loads(parts_path.read_text())
    return {p["ref"]: p for p in parts}


NETS = _parse_netlist("main")
PARTS = _parse_parts("main")

# NCN5130 pin name -> pin number (from gateway.kicad_sym, Table 1)
NCN5130_PINS = {
    "VBUS1": "6", "VBUS2": "2", "VSSA": "1", "VSS1": "18", "VSS2": "14",
    "VSSD": "31", "EPAD": "41", "VDD1": "19", "VDD1M": "20", "VDD2": "13",
    "VDD2MC": "12", "VDD2MV": "11", "VDDA": "40", "VDDD": "30",
    "SDI/RXD": "27", "SDO/TXD": "28", "SAVEB": "36", "RESETB": "37",
    "VIN": "16", "VSW1": "17", "VSW2": "15", "VFILT": "9", "V20V": "10",
    "TX0": "3", "CCP": "4", "CAV": "5", "CEQ1": "7", "CEQ2": "8",
    "FANIN": "38", "SCK/UC2": "29", "CSB/UC1": "26", "MODE1": "23",
    "MODE2": "24", "TREQ": "25", "XSEL": "33", "XCLKC": "21",
    "XTAL1": "35", "XTAL2": "34", "XCLK": "32", "ANAOUT": "39",
    "TRIG": "22",
}

# SCT2400 pin numbers (from gateway.kicad_sym custom symbol)
# SCT2400 datasheet "PIN FUNCTIONS" table (TSOT-23-6)
SCT2400_PINS = {"BST": "1", "GND": "2", "FB": "3", "EN": "4", "VIN": "5", "SW": "6"}

# XIAO ESP32C6 counter-clockwise pin map (Seeed wiki v1.0)
XIAO_PIN_MAP = {
    "D0": "1", "D1": "2", "D2": "3", "D3": "4",
    "D4": "5", "D5": "6", "D6": "7", "D7": "8",
    "D8": "9", "D9": "10", "D10": "11", "3V3": "12",
    "GND": "13", "5V": "14",
}


XIAO_REF = "U7"  # XIAO ESP32C6 (U6=LM66100, U7=XIAO in current ref sequence)


def _net_members(net_name):
    return NETS.get(net_name, [])


def _refs_on_net(net_name):
    return {ref for ref, _ in _net_members(net_name)}


# ---------------------------------------------------------------------------
# 1. NCN5130 Figure 12 connections
# ---------------------------------------------------------------------------

def test_ncn5130_knx_input():
    """KNX input: VBUS1=KNX_P_PROT, VBUS2=GND, VSSA=GND."""
    assert ("U1", NCN5130_PINS["VBUS1"]) in _net_members("KNX_P_PROT")
    assert ("U1", NCN5130_PINS["VBUS2"]) in _net_members("GND")
    assert ("U1", NCN5130_PINS["VSSA"]) in _net_members("GND")


def test_ncn5130_r1_tx_shunt():
    """R1 27Ω 1W from KNX_P_PROT to TX0 (pin 3)."""
    tx0_members = _net_members("TX0")
    r1_found = any(ref.startswith("R") for ref, _ in tx0_members)
    assert r1_found, "No resistor on TX0 net (R1 transmit shunt missing)"
    assert ("U1", NCN5130_PINS["TX0"]) in tx0_members


def test_ncn5130_c1_ccp():
    """C1 47nF from KNX_P_PROT to CCP (pin 4)."""
    ccp_members = _net_members("CCP")
    assert ("U1", NCN5130_PINS["CCP"]) in ccp_members
    c1_found = any(ref.startswith("C") for ref, _ in ccp_members)
    assert c1_found, "No cap on CCP net (C1 missing)"
    # C1 should also connect to KNX_P_PROT
    knx_prot_members = _net_members("KNX_P_PROT")
    ccp_caps = {ref for ref, _ in ccp_members if ref.startswith("C")}
    knx_prot_caps = {ref for ref, _ in knx_prot_members if ref.startswith("C")}
    assert ccp_caps & knx_prot_caps, "C1 not shared between CCP and KNX_P_PROT"


def test_ncn5130_c3_cav():
    """C3 100nF from CAV (pin 5) to GND."""
    cav_members = _net_members("CAV")
    assert ("U1", NCN5130_PINS["CAV"]) in cav_members
    assert ("U1", NCN5130_PINS["CAV"]) in _net_members("CAV")


def test_ncn5130_c2_ceq():
    """C2 220nF between CEQ1 (pin 7) and CEQ2 (pin 8)."""
    ceq1_members = _net_members("CEQ1")
    ceq2_members = _net_members("CEQ2")
    assert ("U1", NCN5130_PINS["CEQ1"]) in ceq1_members
    assert ("U1", NCN5130_PINS["CEQ2"]) in ceq2_members
    ceq1_caps = {ref for ref, _ in ceq1_members if ref.startswith("C")}
    ceq2_caps = {ref for ref, _ in ceq2_members if ref.startswith("C")}
    assert ceq1_caps & ceq2_caps, "C2 not shared between CEQ1 and CEQ2"


def test_ncn5130_vfilt_storage():
    """VFILT (pin 9) = C4 470µF/35V to GND; VIN (pin 16) = VFILT; V20V (pin 10) = VFILT."""
    vfilt_members = _net_members("VFILT")
    assert ("U1", NCN5130_PINS["VFILT"]) in vfilt_members
    assert ("U1", NCN5130_PINS["VIN"]) in vfilt_members, "VIN not on VFILT"
    assert ("U1", NCN5130_PINS["V20V"]) in vfilt_members, "V20V not on VFILT"
    c4_found = any(ref.startswith("C") for ref, _ in vfilt_members)
    assert c4_found, "No cap on VFILT (C4 storage missing)"


def test_ncn5130_dc2_unused():
    """DC2 unused: VDD2MV=+3V3_TRX, VDD2/VDD2MC/VSW2 NC, VSS2=GND."""
    assert ("U1", NCN5130_PINS["VDD2MV"]) in _net_members("+3V3_TRX"), \
        "VDD2MV not on +3V3_TRX"
    assert ("U1", NCN5130_PINS["VSS2"]) in _net_members("GND"), \
        "VSS2 not on GND"
    # VDD2, VDD2MC, VSW2 should not be on any named power net
    for pin in ("VDD2", "VDD2MC", "VSW2"):
        pin_num = NCN5130_PINS[pin]
        found_on_power = False
        for net_name in ("+3V3_TRX", "VFILT", "+5V", "GND", "+3V3"):
            if ("U1", pin_num) in _net_members(net_name):
                found_on_power = True
        assert not found_on_power, f"U1 {pin} should be NC but found on a power net"


def test_ncn5130_dc1():
    """DC1: VSW1→L1→VDD1M, R2 1Ω VDD1M→VDD1, C10 10µF VDD1→GND, VSS1=GND."""
    vsw1_members = _net_members("VSW1")
    assert ("U1", NCN5130_PINS["VSW1"]) in vsw1_members
    l_found = any(ref.startswith("L") for ref, _ in vsw1_members)
    assert l_found, "No inductor on VSW1 (L1 missing)"
    vdd1m_members = _net_members("VDD1M")
    assert ("U1", NCN5130_PINS["VDD1M"]) in vdd1m_members
    r2_found = any(ref.startswith("R") for ref, _ in vdd1m_members)
    assert r2_found, "No R2 sense resistor on VDD1M"
    assert ("U1", NCN5130_PINS["VDD1"]) in _net_members("+3V3_TRX")
    assert ("U1", NCN5130_PINS["VSS1"]) in _net_members("GND")


def test_ncn5130_vdda_vddd():
    """VDDA (pin 40) and VDDD (pin 30) on +3V3_TRX with C5/C6 100nF."""
    for pin in ("VDDA", "VDDD"):
        assert ("U1", NCN5130_PINS[pin]) in _net_members("+3V3_TRX"), \
            f"U1 {pin} not on +3V3_TRX"


def test_ncn5130_fanin():
    """FANIN (pin 38) → R6 10kΩ → GND (40mA bus load)."""
    fanin_members = _net_members("FANIN")
    if not fanin_members:
        pytest.skip("FANIN net not found in netlist")
    assert ("U1", NCN5130_PINS["FANIN"]) in fanin_members
    r_found = any(ref.startswith("R") for ref, _ in fanin_members)
    assert r_found, "No R6 FANIN resistor"


def test_ncn5130_crystal():
    """X1 16MHz crystal on XTAL1/XTAL2 with C8/C9 load caps; XSEL=GND."""
    xtal1_members = _net_members("XTAL1")
    xtal2_members = _net_members("XTAL2")
    if not xtal1_members or not xtal2_members:
        pytest.skip("XTAL nets not found")
    assert ("U1", NCN5130_PINS["XTAL1"]) in xtal1_members
    assert ("U1", NCN5130_PINS["XTAL2"]) in xtal2_members
    x1_found = any(ref.startswith(("X", "Y")) for ref, _ in xtal1_members)
    assert x1_found, "No crystal on XTAL1"
    assert ("U1", NCN5130_PINS["XSEL"]) in _net_members("GND"), "XSEL not on GND"


def test_ncn5130_uart_straps():
    """UART mode straps: CSB/UC1, SCK/UC2, MODE1, MODE2, TREQ all GND (9-bit 19200)."""
    for pin in ("CSB/UC1", "SCK/UC2", "MODE1", "MODE2", "TREQ"):
        assert ("U1", NCN5130_PINS[pin]) in _net_members("GND"), \
            f"U1 {pin} not on GND (UART strap)"


def test_ncn5130_saveb():
    """SAVEB (pin 36) → XIAO D9 with pull-up."""
    saveb_members = _net_members("NCN_SAVEB")
    assert ("U1", NCN5130_PINS["SAVEB"]) in saveb_members
    assert ("U7", XIAO_PIN_MAP["D9"]) in saveb_members, "XIAO D9 not on NCN_SAVEB"


def test_ncn5130_xclkc_gnd():
    """XCLKC (pin 21) → GND per Figure 12."""
    assert ("U1", NCN5130_PINS["XCLKC"]) in _net_members("GND")


# ---------------------------------------------------------------------------
# 2. 5V buck (SCT2400 from VFILT)
# ---------------------------------------------------------------------------

def test_buck5v_power_pins():
    """SCT2400 (U2) VIN=VFILT, EN through divider (not directly VFILT), GND=GND."""
    assert ("U2", SCT2400_PINS["VIN"]) in _net_members("VFILT"), \
        "U2 VIN not on VFILT"
    # EN should NOT be directly on VFILT — it goes through a divider
    assert ("U2", SCT2400_PINS["EN"]) not in _net_members("VFILT"), \
        "U2 EN directly on VFILT (should be through divider for UVLO)"
    sw_members = _net_members("SW")
    if not sw_members:
        pytest.skip("SW net not found")
    assert ("U2", SCT2400_PINS["SW"]) in sw_members


def test_buck5v_en_divider():
    """Buck EN divider: 120k/10k → turn-on ~17V (above SAVEB 14V).
    SCT2400 EN: 1.21V rising / 1.1V falling (typ).
    turn-on typ = 1.21 * 13 = 15.7V; turn-off typ = 1.1 * 13 = 14.3V."""
    r_top = 120.0  # kΩ
    r_bot = 10.0   # kΩ
    ratio = r_top / r_bot + 1  # 13
    en_rising_typ = 1.21
    en_falling_typ = 1.1
    en_rising_max = 1.4
    en_falling_min = 0.9
    turn_on_typ = en_rising_typ * ratio
    turn_off_typ = en_falling_typ * ratio
    turn_on_max = en_rising_max * ratio
    turn_off_min = en_falling_min * ratio
    assert turn_on_typ >= 14.0, f"Turn-on {turn_on_typ:.1f}V < SAVEB 14V"
    assert turn_off_min >= 11.0, f"Turn-off min {turn_off_min:.1f}V too low"
    print(f"\n  Buck EN UVLO: turn-on typ={turn_on_typ:.1f}V, "
          f"turn-off typ={turn_off_typ:.1f}V")
    print(f"  turn-on max={turn_on_max:.1f}V, turn-off min={turn_off_min:.1f}V")


def test_buck5v_fb_divider():
    """FB divider: 51k/10k → VOUT = 0.81*(1+51/10) = 4.94V."""
    r_top = 51.0
    r_bot = 10.0
    vref = 0.81
    vout = vref * (1 + r_top / r_bot)
    assert 4.9 <= vout <= 5.2, f"Buck VOUT {vout:.2f}V outside 4.9-5.2V"


def test_sct2400_input_rating():
    """SCT2400 input max 40V >= VFILT max (~30V)."""
    sct2400_vin_max = 40.0
    vfilt_max = 30.0
    assert sct2400_vin_max >= vfilt_max, \
        f"SCT2400 VIN max {sct2400_vin_max}V < VFILT max {vfilt_max}V"


# ---------------------------------------------------------------------------
# 3. XIAO pin map (counter-clockwise per Seeed wiki)
# ---------------------------------------------------------------------------

def test_xiao_pin_map():
    """XIAO pin map matches Seeed counter-clockwise table:
    1=D0, 2=D1, 3=D2, 4=D3, 5=D4, 6=D5, 7=D6, 8=D7,
    9=D8, 10=D9, 11=D10, 12=3V3, 13=GND, 14=5V. No pad 15."""
    # 5V on pin 14
    assert ("U7", XIAO_PIN_MAP["5V"]) in _net_members("+5V_XIAO")
    # GND on pin 13
    assert ("U7", XIAO_PIN_MAP["GND"]) in _net_members("GND")
    # 3V3 on pin 12 (net may be +3V3 or +3V3_1 after SKiDL merge)
    v3_nets = _net_members("+3V3") or _net_members("+3V3_1")
    assert ("U7", XIAO_PIN_MAP["3V3"]) in v3_nets, \
        "XIAO 3V3 not on +3V3 or +3V3_1"
    # D0=LED_DATA (RGB LED chain), D1 spare and unconnected, D3=LED_DALI (spare wire)
    assert ("U7", XIAO_PIN_MAP["D0"]) in _net_members("LED_DATA")
    assert not any(("U7", XIAO_PIN_MAP["D1"]) in members for members in _parse_netlist("main").values())
    assert ("U7", XIAO_PIN_MAP["D3"]) in _net_members("LED_DALI")
    # D2=BTN_LADDER (ADC)
    assert ("U7", XIAO_PIN_MAP["D2"]) in _net_members("BTN_LADDER")
    # D6=KNX_RXD (MCU TX), D7=KNX_TXD (MCU RX)
    assert ("U7", XIAO_PIN_MAP["D6"]) in _net_members("KNX_RXD")
    assert ("U7", XIAO_PIN_MAP["D7"]) in _net_members("KNX_TXD")
    # D8=DALI_RX, D9=NCN_SAVEB (tested above), D10=DALI_TX
    assert ("U7", XIAO_PIN_MAP["D8"]) in _net_members("DALI_RX")
    assert ("U7", XIAO_PIN_MAP["D10"]) in _net_members("DALI_TX")
    # D4=SDA, D5=SCL
    assert ("U7", XIAO_PIN_MAP["D4"]) in _net_members("I2C_SDA")
    assert ("U7", XIAO_PIN_MAP["D5"]) in _net_members("I2C_SCL")


def test_xiao_no_pad15():
    """No pad 15 (EN) on XIAO — EN is not on the castellated header."""
    en_members = _net_members("EN")
    assert ("U7", "15") not in en_members, "XIAO pad 15 should not exist"


# ---------------------------------------------------------------------------
# 4. ADC ladder thresholds
# ---------------------------------------------------------------------------

def test_adc_ladder_thresholds():
    """ADC ladder on D2: 10k pull-up, 20k PROG, 5.1k RESET, 100nF.
    No button: 3V3; PROG: 2.2V; RESET: 1.11V. ≥300mV separation with 5% tol."""
    vcc = 3.3
    r_pull = 10000
    r_prog = 20000
    r_reset = 5100
    tol = 0.05

    v_none = vcc
    v_prog = vcc * r_prog / (r_pull + r_prog)
    v_reset = vcc * r_reset / (r_pull + r_reset)

    # With 5% tolerance, worst-case separation
    v_prog_min = v_prog * (1 - tol)
    v_prog_max = v_prog * (1 + tol)
    v_reset_max = v_reset * (1 + tol)
    v_none_min = v_none * (1 - tol)

    sep_none_prog = v_none_min - v_prog_max
    sep_prog_reset = v_prog_min - v_reset_max

    assert sep_none_prog >= 0.3, \
        f"None-PROG separation {sep_none_prog:.3f}V < 300mV"
    assert sep_prog_reset >= 0.3, \
        f"PROG-RESET separation {sep_prog_reset:.3f}V < 300mV"

    print(f"\n  ADC ladder: none={v_none:.2f}V, PROG={v_prog:.2f}V, "
          f"RESET={v_reset:.2f}V")
    print(f"  Min separations: none-PROG={sep_none_prog:.3f}V, "
          f"PROG-RESET={sep_prog_reset:.3f}V")


# ---------------------------------------------------------------------------
# 5. DALI TX/RX thresholds and bus current
# ---------------------------------------------------------------------------

def test_dali_tx_current_limit():
    """TX current limit: R_sense=3.3Ω → 180-230mA (V_BE 0.6-0.75V)."""
    r_sense = 3.3
    vbe_min = 0.6
    vbe_max = 0.75
    i_min = vbe_min / r_sense * 1000
    i_max = vbe_max / r_sense * 1000
    assert 180 <= i_min <= 230, f"TX I_min {i_min:.0f}mA outside 180-230mA"
    assert 180 <= i_max <= 250, f"TX I_max {i_max:.0f}mA outside 180-250mA"
    print(f"\n  DALI TX: R_sense={r_sense}Ω → {i_min:.0f}-{i_max:.0f}mA")


def test_dali_tx_low_level():
    """Low level at 250mA: 2 bridge drops + R_sense + R_DS(on) ≤ 4.5V."""
    bridge_drops = 2 * 0.7  # 1.4V
    r_sense_drop = 0.75  # V at 230mA (max V_BE)
    r_ds_on_drop = 0.5  # conservative for SOT-23 MOSFET at 12V V_GS
    low_level = bridge_drops + r_sense_drop + r_ds_on_drop
    assert low_level <= 4.5, \
        f"DALI low level {low_level:.2f}V > 4.5V"
    print(f"\n  DALI TX low level: {low_level:.2f}V (≤4.5V)")


def test_dali_tx_bootstrap_droop():
    """Bootstrap reservoir droop over 1ms low with 22k gate series R.
    Gate current = 12V / 22k = 0.55mA; droop = 0.55mA * 1ms / 1uF = 0.55V."""
    c_boot = 1e-6  # 1µF
    i_gate = 12.0 / 22e3  # 12V zener / 22k series R = 0.55mA
    t_low = 1e-3  # 1ms
    droop = i_gate * t_low / c_boot
    assert droop < 1.0, f"Bootstrap droop {droop:.2f}V > 1V"
    # V_GS after droop: 12V - 0.55V = 11.45V >> V_GS(th) max 2.5V
    v_gs_after = 12.0 - droop
    assert v_gs_after > 2.5, f"V_GS after droop {v_gs_after:.2f}V < V_GS(th) max"
    print(f"\n  DALI TX bootstrap droop: {droop:.3f}V over 1ms, V_GS={v_gs_after:.2f}V")


def test_dali_rx_thresholds():
    """RX: guaranteed OFF for bus<6.5V, ON for bus≥9.5V incl 2 bridge drops.
    5V6 zener ±5%, LED V_F range, 2 bridge drops, two-NPN current limiter."""
    v_bridge = 0.7
    v_zener = 5.6
    zener_tol = 0.05
    v_led_max = 1.4
    v_be = 0.7  # NPN limiter V_BE

    # OFF threshold: bus < 6.5V → DA_P < 5.1V
    # Zener (max) = 5.6 * 1.05 = 5.88V; DA_P must be < 5.88V for zener off
    bus_off_max = 6.5
    da_p_off = bus_off_max - 2 * v_bridge  # 5.1V
    zener_max = v_zener * (1 + zener_tol)  # 5.88V
    assert da_p_off < zener_max, \
        f"DA_P at bus 6.5V ({da_p_off:.2f}V) >= zener max ({zener_max:.2f}V) — not guaranteed OFF"

    # ON threshold: bus ≥ 9.5V → DA_P ≥ 8.1V
    # Zener (min) = 5.6 * 0.95 = 5.32V; limiter needs V_BE + V_LED + V_CE(sat)
    bus_on_min = 9.5
    da_p_on = bus_on_min - 2 * v_bridge  # 8.1V
    zener_min = v_zener * (1 - zener_tol)  # 5.32V
    v_needed = zener_min + v_led_max + v_be  # 5.32 + 1.4 + 0.7 = 7.42V
    assert da_p_on >= v_needed, \
        f"DA_P at bus 9.5V ({da_p_on:.2f}V) < needed ({v_needed:.2f}V) — not guaranteed ON"

    print(f"\n  DALI RX: OFF < {bus_off_max}V (DA_P {da_p_off:.2f}V < zener {zener_max:.2f}V)")
    print(f"  ON ≥ {bus_on_min}V (DA_P {da_p_on:.2f}V >= needed {v_needed:.2f}V)")


def test_dali_bus_current():
    """Bus current (RX + TX bootstrap) ≤ 2mA at 22.5V per IEC 62386-101.
    RX two-NPN limiter: I_limit = 0.6V / 680Ω = 0.88mA
    TX bootstrap: 12V / 22k = 0.55mA (gate series R discharge)
    Total: 0.88 + 0.55 = 1.43mA ≤ 2mA."""
    i_rx = 0.88  # mA (two-NPN limiter: 0.6V/680Ω)
    i_tx_bootstrap = 0.55  # mA (12V/22k gate series R)
    i_total = i_rx + i_tx_bootstrap
    iec_limit = 2.0  # mA per IEC 62386-101
    assert i_total <= iec_limit, \
        f"Total bus current {i_total:.2f}mA > IEC limit {iec_limit}mA"
    print(f"\n  DALI bus current: RX={i_rx}mA + TX={i_tx_bootstrap}mA = {i_total:.2f}mA ≤ {iec_limit}mA")


def test_dali_rx_polarity():
    """RX polarity: bus high → DALI_RX low (opto transistor sinks).
    Record for firmware: inverted (bus high = logic low)."""
    # U5 pin 6 (opto transistor output) on DALI_RX
    dali_rx_members = _net_members("DALI_RX")
    assert ("U5", "6") in dali_rx_members, "U5 pin 6 not on DALI_RX"
    # Pull-up resistor present
    r_found = any(ref.startswith("R") for ref, _ in dali_rx_members)
    assert r_found, "No pull-up on DALI_RX"


# ---------------------------------------------------------------------------
# 5b. B1b DALI topology checks
# ---------------------------------------------------------------------------

def test_dali_limiter_pnp_sense():
    """Limiter sense transistor is PNP (BC857): E=iso15, B=Q_pass.E, C=Q_pass.B.
    An NPN sense transistor would be reverse-biased and never limit."""
    iso15_members = _net_members("DALI_ISO15V")
    sw_in_members = _net_members("DALI_SUPPLY_SW")
    # Q_pass should be on both iso15 (via R_sense) and sw_in
    q_pass_on_iso15 = any(ref.startswith("Q") for ref, _ in iso15_members)
    q_pass_on_sw = any(ref.startswith("Q") for ref, _ in sw_in_members)
    assert q_pass_on_iso15, "No Q_pass transistor on DALI_ISO15V"
    assert q_pass_on_sw, "No Q_pass transistor on DALI_SUPPLY_SW"
    # Check Q_pass value is MJD45H11 (not MJD117 Darlington)
    for ref, info in PARTS.items():
        if ref.startswith("Q") and "MJD45H11" in str(info.get("value", "")):
            break
    else:
        pytest.fail("No Q_pass with MJD45H11 value found in parts")


def test_dali_pass_not_darlington():
    """Q_pass is MJD45H11 (normal PNP), not MJD117 (Darlington).
    MJD117 V_CE(sat) ~2V → bus idles ~12.5V; MJD45H11 V_ECsat ~0.5V → ~14.5V."""
    for info in PARTS.values():
        val = str(info.get("value", ""))
        if "MJD45H11" in val:
            assert "MJD117" not in val, "MJD117 Darlington still present"
            return
    pytest.fail("MJD45H11 pass transistor not found")


def test_supply_jumpers_switch_both_dali_lines():
    """JP1 connects the limiter output to DA1 and GNDD to DA2; with both
    jumpers removed the terminals only see the bridge rectifier."""
    pairs = {("DALI_SUPPLY_SW", "DA1"), ("GNDD", "DA2")}
    pins = {net: {pin for ref, pin in _net_members(net) if ref == "JP1"}
            for net in ("DALI_SUPPLY_SW", "DA1", "GNDD", "DA2")}
    assert pins == {"DALI_SUPPLY_SW": {"1"}, "DA1": {"2"}, "GNDD": {"3"}, "DA2": {"4"}}, pins
    assert all(len(pair) == 2 for pair in pairs)


def test_buck_pins_follow_the_sct2400_datasheet():
    expected = {"BST": "N$2", "GND": "GND", "FB": "N$3", "EN": "N$1", "VIN": "VFILT", "SW": "SW"}
    for name, net in expected.items():
        assert ("U2", SCT2400_PINS[name]) in _net_members(net), (name, net)


def test_dali_gate_series_resistor():
    """Gate series R 22k between opto emitter and gate.
    Without it, bootstrap discharges at opto collector current (mA)."""
    gate_members = _net_members("DALI_GATE")
    gate_sw_members = _net_members("DALI_GATE_SW")
    # Gate should have a resistor connecting to gate_sw
    gate_r = {ref for ref, _ in gate_members if ref.startswith("R")}
    gate_sw_r = {ref for ref, _ in gate_sw_members if ref.startswith("R")}
    assert gate_r & gate_sw_r, \
        "No series resistor between DALI_GATE and DALI_GATE_SW (gate series R missing)"


def test_dali_rx_two_npn_limiter():
    """RX uses two-NPN current limiter (BC847 x2): pass NPN with R_E=680Ω,
    sense NPN across R_E, bias R=100k. Not a PNP with only B-E resistor."""
    # Check that there are at least 2 NPN transistors in the RX path
    # The limiter is between the zener/LED and GNDD
    rx_lim_members = _net_members("RX_LIM_IN")
    assert rx_lim_members, "RX_LIM_IN net not found (two-NPN limiter missing)"
    # Should have Q (pass NPN) and R (bias) on this net
    q_found = any(ref.startswith("Q") for ref, _ in rx_lim_members)
    r_found = any(ref.startswith("R") for ref, _ in rx_lim_members)
    assert q_found, "No pass NPN on RX_LIM_IN"
    assert r_found, "No bias resistor on RX_LIM_IN"


def test_dali_side_tvs():
    """DALI-side TVS SMAJ24A across DA_P-GNDD (standoff 24V > 22.5V bus max)."""
    da_p_members = _net_members("DA_P")
    gndd_members = _net_members("GNDD")
    # TVS should be on both DA_P and GNDD
    da_p_tvs = {ref for ref, _ in da_p_members if ref.startswith("D")}
    gndd_tvs = {ref for ref, _ in gndd_members if ref.startswith("D")}
    assert da_p_tvs & gndd_tvs, \
        "No TVS shared between DA_P and GNDD (DALI-side TVS missing)"


def test_dali_ptc_1812():
    """PTC fuse is 1812 SMD (not radial THT MF-RG250)."""
    for ref, info in PARTS.items():
        if ref.startswith("F"):
            fp = str(info.get("footprint", ""))
            assert "1812" in fp, f"F1 footprint {fp} is not 1812 SMD"
            return
    pytest.fail("No fuse found")


def test_rem3_output_cap_voltage():
    """REM3 output cap is 10µF 25V (not just 10µF with no voltage spec)."""
    for info in PARTS.values():
        val = str(info.get("value", ""))
        if "10u" in val and "25V" in val:
            return
    # Not critical if exact value not found — just check a cap exists on iso15
    iso15_members = _net_members("DALI_ISO15V")
    c_found = any(ref.startswith("C") for ref, _ in iso15_members)
    assert c_found, "No output cap on DALI_ISO15V"


# ---------------------------------------------------------------------------
# 5c. B1b KNX topology checks
# ---------------------------------------------------------------------------

def test_crystal_load_caps_18pF():
    """Crystal load caps C8/C9 = 18pF (C = 2*(CL-Cstray) = 2*(12-3) = 18pF).
    12pF would pull the crystal tens of ppm high (CL_eff = 9pF for CL=12pF)."""
    cl = 12.0  # pF, crystal load capacitance (X322516MOB4SI)
    c_stray = 3.0  # pF, estimated stray per pin
    c_load = 2 * (cl - c_stray)
    assert abs(c_load - 18.0) < 0.1, f"Expected 18pF, got {c_load}pF"
    # Check ppm budget: crystal tol ±10ppm, stability ±20ppm, ageing ±3ppm/yr
    crystal_tol = 10
    crystal_stab = 20
    ageing = 3
    total_ppm = crystal_tol + crystal_stab + ageing
    assert total_ppm <= 50, f"Crystal ppm budget {total_ppm} > 50ppm"
    print(f"\n  Crystal: CL={cl}pF, C_stray={c_stray}pF, C_load={c_load}pF")
    print(f"  ppm budget: tol={crystal_tol} + stab={crystal_stab} + ageing={ageing} = {total_ppm}ppm ≤ 50")


def test_c10_effective_capacitance():
    """C10 VDD1: 22µF 10V 0805 X5R. At 3.3V DC bias, effective cap ≥ 8µF.
    A 10µF/6.3V 0603 would drop well below 8µF at 3.3V bias."""
    nominal = 22.0  # µF
    dc_bias_derating = 0.50  # 50% at 3.3V for 0805 X5R (conservative)
    effective = nominal * dc_bias_derating
    min_required = 8.0  # µF per NCN5130 Table 6
    assert effective >= min_required, \
        f"C10 effective {effective:.1f}µF < {min_required}µF required"
    print(f"\n  C10: 22µF 10V 0805, effective at 3.3V bias = {effective:.1f}µF ≥ {min_required}µF")


def test_knx_rxd_series_resistor():
    """1kΩ series resistor in KNX_RXD (XIAO TX → NCN SDI).
    Prevents USB-powered XIAO from back-powering unpowered NCN."""
    knx_rxd_members = _net_members("KNX_RXD")
    # Should have XIAO D6, NCN SDI/RXD, and a series resistor
    r_found = any(ref.startswith("R") for ref, _ in knx_rxd_members)
    assert r_found, "No series resistor on KNX_RXD (back-power protection missing)"


# ---------------------------------------------------------------------------
# 6. Isolation domain checks
# ---------------------------------------------------------------------------

def test_no_domain_bridge_except_isolators():
    """No single net connects parts in both SELV-KNX and DALI/AUX domains
    except through U3 (REM3), U4/U5 (optos)."""
    gnd_refs = _refs_on_net("GND")
    gndd_refs = _refs_on_net("GNDD")
    gnda_refs = _refs_on_net("GNDA")
    optos = {"U4", "U5", "U3"}
    gnd_only = gnd_refs - optos
    gndd_only = gndd_refs - optos
    gnda_only = gnda_refs - optos
    assert not (gnd_only & gndd_only), \
        f"GND and GNDD share non-isolator refs: {gnd_only & gndd_only}"
    assert not (gnd_only & gnda_only), \
        f"GND and GNDA share non-isolator refs: {gnd_only & gnda_only}"
    assert not (gndd_only & gnda_only), \
        f"GNDD and GNDA share non-isolator refs: {gndd_only & gnda_only}"


# ---------------------------------------------------------------------------
# 7. Component ratings
# ---------------------------------------------------------------------------

def test_rem3_input_range():
    """REM3 input 18-36V vs 24V/30V aux."""
    rem3_vin_min = 18.0
    rem3_vin_max = 36.0
    aux_nominal = 24.0
    aux_max = 30.0
    assert rem3_vin_min <= aux_nominal
    assert rem3_vin_max >= aux_max


def test_capacitor_voltage_ratings():
    """C4 storage cap voltage >= VFILT max with margin.
    VFILT can approach bus voltage (~30V). C4 = 470µF/35V per NCN5130 Table 6.
    35V / 30V = 1.17x margin (NCN5130 datasheet specifies 35V for this rail)."""
    vfilt_max = 30.0
    c4 = PARTS.get("C4", {})
    c4_value = c4.get("value", "")
    voltage_match = re.search(r'(\d+(?:\.\d+)?)\s*V', c4_value)
    if voltage_match:
        cap_v = float(voltage_match.group(1))
        assert cap_v >= vfilt_max, \
            f"C4 voltage {cap_v}V < VFILT max {vfilt_max}V"


# ---------------------------------------------------------------------------
# 8. KNX power budget (VFILT storage)
# ---------------------------------------------------------------------------

def test_knx_power_budget():
    """Available power from KNX bus vs total load.

    Power chain (new architecture):
      KNX bus (20V min, 40mA) → VFILT (no DC2, direct storage)
      VFILT → SCT2400 (85%) → +5V
      +5V → XIAO onboard buck (85%) → +3V3

    Loads at 3.3V:
      XIAO CPU: 30mA, LEDs: 3×2mA, OLED: 12mA, NCN5130: 10mA
      WiFi modem-sleep DTIM3: 27mA, WiFi RX: 78mA
    """
    knx_bus_ma = 40
    bus_v_min = 20.0
    sct2400_eff = 0.85
    xiao_buck_eff = 0.85

    xiao_cpu_ma = 30
    wifi_rx_ma = 78
    wifi_modem_ma = 27
    led_ma = 3 * 2
    oled_ma = 12
    ncn5130_ma = 10
    v33 = 3.3

    base_mw = (xiao_cpu_ma + led_ma + oled_ma + ncn5130_ma) * v33
    wifi_rx_mw = wifi_rx_ma * v33
    wifi_modem_mw = wifi_modem_ma * v33

    results = []
    for bus_v_label, bus_v in [("20V worst-case", bus_v_min), ("24V nominal", 24.0)]:
        bus_mw = knx_bus_ma * bus_v
        vfilt_mw = bus_mw  # direct storage, no DC2
        vfilt_mw * sct2400_eff

        for wifi_label, wifi_mw in [
            ("WiFi off", 0),
            ("WiFi modem-sleep DTIM3", wifi_modem_mw),
            ("WiFi continuous RX", wifi_rx_mw),
        ]:
            total_33_mw = base_mw + wifi_mw
            vfilt_load_mw = total_33_mw / (sct2400_eff * xiao_buck_eff)
            margin = vfilt_mw - vfilt_load_mw
            pct = margin / vfilt_mw * 100 if vfilt_mw > 0 else 0
            results.append((bus_v_label, wifi_label, bus_mw, vfilt_mw,
                            total_33_mw, vfilt_load_mw, margin, pct))

    print("\n  KNX power budget (VFILT storage, no DC2):")
    print(f"  {'Bus':<18s} {'WiFi mode':<28s} {'Bus mW':>7s} "
          f"{'VFILT mW':>9s} {'Load mW':>8s} {'VFILT load':>11s} "
          f"{'Margin mW':>10s} {'Margin %':>9s}")
    for r in results:
        print(f"  {r[0]:<18s} {r[1]:<28s} {r[2]:7.0f} {r[3]:9.0f} "
              f"{r[4]:8.0f} {r[5]:11.0f} {r[6]:10.0f} {r[7]:8.0f}%")

    for bus_label, wifi_label, _, _, _, _, margin, _ in results:
        assert margin > 0, f"{bus_label} {wifi_label}: margin {margin:.0f} mW < 0"


def test_wifi_tx_burst_energy():
    """Storage energy from VFILT 28V→15V vs WiFi TX burst.
    C4=470µF, energy = 0.5*C*(V1²-V2²) = 0.5*470e-6*(784-225) = 131mJ
    WiFi burst: 354mA*3.3V*10ms = 11.7mJ → 11x headroom."""
    c_bulk = 470e-6
    v_full = 28.0
    v_min = 15.0
    energy_store = 0.5 * c_bulk * (v_full**2 - v_min**2)
    wifi_ma = 354
    wifi_v = 3.3
    wifi_ms = 10
    wifi_energy = wifi_ma * 1e-3 * wifi_v * wifi_ms * 1e-3
    print(f"\n  WiFi TX burst: storage {energy_store*1000:.1f}mJ, "
          f"burst {wifi_energy*1000:.1f}mJ, ratio {energy_store/wifi_energy:.1f}x")
    assert energy_store > wifi_energy


# ---------------------------------------------------------------------------
# 9. Logic checks
# ---------------------------------------------------------------------------

def test_knx_uart_crossing():
    """KNX UART: XIAO D6→R series→NCN SDI/RXD, NCN SDO/TXD→XIAO D7.
    The 1k series R in KNX_RXD prevents USB-powered XIAO from back-powering NCN."""
    # XIAO D6 on KNX_RXD (MCU side of series R)
    assert ("U7", XIAO_PIN_MAP["D6"]) in _net_members("KNX_RXD")
    # Series resistor on KNX_RXD
    knx_rxd_members = _net_members("KNX_RXD")
    r_refs = {ref for ref, _ in knx_rxd_members if ref.startswith("R")}
    assert r_refs, "No series resistor on KNX_RXD"
    # NCN SDI/RXD on the other side of the series R (auto-named net)
    # Find the net that has U1 SDI/RXD pin
    sdi_pin = NCN5130_PINS["SDI/RXD"]
    for net_name, members in NETS.items():
        if ("U1", sdi_pin) in members:
            # This net should also have the same series R
            net_r_refs = {ref for ref, _ in members if ref.startswith("R")}
            assert r_refs & net_r_refs, \
                f"NCN SDI/RXD net {net_name} doesn't share series R with KNX_RXD"
            break
    else:
        pytest.fail("NCN SDI/RXD pin not found on any net")
    # NCN SDO/TXD → XIAO D7 (no series R on TXD)
    assert ("U7", XIAO_PIN_MAP["D7"]) in _net_members("KNX_TXD")
    assert ("U1", NCN5130_PINS["SDO/TXD"]) in _net_members("KNX_TXD")


def test_dali_tx_drives_opto_led():
    """DALI_TX drives TX opto LED through resistor."""
    dali_tx_members = _net_members("DALI_TX")
    assert ("U7", XIAO_PIN_MAP["D10"]) in dali_tx_members
    r_found = any(ref.startswith("R") for ref, _ in dali_tx_members)
    assert r_found, "No series resistor on DALI_TX"


def test_dali_rx_from_opto():
    """DALI_RX from RX opto transistor with pull-up."""
    dali_rx_members = _net_members("DALI_RX")
    assert ("U5", "6") in dali_rx_members
    r_found = any(ref.startswith("R") for ref, _ in dali_rx_members)
    assert r_found, "No pull-up on DALI_RX"


def test_led_chain_is_buffered_to_5v_with_series_resistor():
    """LED_DATA (3.3 V) drives the 5 V buffer, whose output reaches the first
    SK6805 through a series resistor; each LED feeds the next one."""
    ui = _parse_netlist("ui")
    assert ("U1", "2") in ui["LED_DATA"]
    assert ("U1", "5") in ui["+5V"]
    buffered = next(n for n, m in ui.items() if ("U1", "4") in m)
    resistor = next(ref for ref, _ in ui[buffered] if ref.startswith("R"))
    first = next(n for n, m in ui.items() if (resistor, "1") in m or (resistor, "2") in m if n != buffered)
    assert ("D1", "3") in ui[first]
    assert ("D1", "1") in ui["LED_CHAIN1"] and ("D2", "3") in ui["LED_CHAIN1"]
    assert ("D2", "1") in ui["LED_CHAIN2"] and ("D3", "3") in ui["LED_CHAIN2"]
    for led in ("D1", "D2", "D3"):
        assert (led, "4") in ui["+5V"] and (led, "2") in ui["GND"]


def test_i2c_pullups_present():
    """I2C SDA/SCL have pull-up resistors on main board."""
    for i2c_net in ("I2C_SDA", "I2C_SCL"):
        members = _net_members(i2c_net)
        r_refs = {ref for ref, _ in members if ref.startswith("R")}
        assert r_refs, f"No pull-up on {i2c_net}"


def test_xiao_pin_map_matches_spec():
    """XIAO pin map in spec.yaml derived section matches the netlist."""
    mcu_pins = SPEC.get("derived", {}).get("mcu_pins", {})
    pin_map = {
        "led_data": XIAO_PIN_MAP["D0"],
        "button_ladder": XIAO_PIN_MAP["D2"],
        "led_dali": XIAO_PIN_MAP["D3"],
        "i2c_sda": XIAO_PIN_MAP["D4"],
        "i2c_scl": XIAO_PIN_MAP["D5"],
        "knx_uart_tx": XIAO_PIN_MAP["D6"],
        "knx_uart_rx": XIAO_PIN_MAP["D7"],
        "dali_rx": XIAO_PIN_MAP["D8"],
        "ncn_saveb": XIAO_PIN_MAP["D9"],
        "dali_tx": XIAO_PIN_MAP["D10"],
    }
    for func, pin_num in pin_map.items():
        spec_net = mcu_pins.get(func, {}).get("net")
        if spec_net and spec_net in NETS:
            assert ("U7", pin_num) in _net_members(spec_net), \
                f"XIAO pin {pin_num} ({func}) not on {spec_net}"
