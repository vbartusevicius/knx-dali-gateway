"""Circuit physics from the generated netlists: component values are read from
the netlist and parts list of the active variant, so a value change in
hardware/circuits/blocks is checked against the datasheet limits below.

- DALI TX: sinks the 250 mA IEC 62386-101 requires, below 4.5 V, and its fault
  clamp never undercuts the internal supply's limiter
- DALI TX gate: enough V_GS at the lowest bus high level, fast turn-off, and a
  bootstrap reservoir that holds through the longest active period we send
- DALI RX: quiescent bus current and pull-up the optocoupler can saturate
- KNX 5 V buck: its EN undervoltage lockout starts at the lowest KNX bus
  voltage and stops above the NCN5130 VFILT reset
- KNX power budget: modem-sleep WiFi down to the lowest KNX bus voltage,
  continuous WiFi from CONTINUOUS_WIFI_BUS_MIN_V
- AUX core converter: carries the sustained load and the WiFi TX peak (rating
  from the fitted part)
- Core mux: the core rail rides through the TPS2121 switchover at a WiFi TX peak
"""

import math
import re

import board_variant as bv
import pytest

BOARDS = bv.BOARDS

DALI_SINK_A = 0.250             # IEC 62386-101: a transmitter sinks the max bus current
DALI_LOW_MAX_V = 4.5            # IEC 62386-101 low level at that current
DALI_HIGH_MIN_V = 9.5           # IEC 62386-101 lowest high level
DALI_HIGH_MAX_V = 22.5
DALI_QUIESCENT_MAX_A = 2e-3     # IEC 62386-101 per-device idle bus current
VBE_MIN, VBE_MAX = 0.55, 0.75   # BC847/BC857 over -25..85 C at 0.1..3 mA (Nexperia figs)
BRIDGE_VF_V = 1.1               # DF06S VF max at 1 A per diode (Diodes Inc. DF-S)
SS16_VF_V = 0.5                 # MDD SS16 at < 10 mA
MOSFET_RDS_OHM = 0.116          # IRLML0060 R_DS(on) max at V_GS 4.5 V
MOSFET_VGS_SPEC_V = 4.5
MOSFET_VTH_MAX_V = 2.5
MOSFET_CISS_F = 290e-12 + 100e-12   # IRLML0060 Ciss + 12 V zener and layout
MOSFET_QGD_C = 1.3e-9
GATE_CLAMP_V = 12.0
TX_HALF_BIT_MAX_EXCESS_S = 16e-6    # 433 us tx_half_max - 417 us Te (gateway/src/dali/bus/Timing.hpp)
GATE_TURN_OFF_SHARE = 0.75          # rest of the excess for the optocoupler, measured on the bench
LONGEST_ACTIVE_S = 1.3e-3           # collision break (Timing.hpp collision_break_us)
OPTO_CTR_MIN = 1.0                  # TLP383 BL rank, conservative at about 1 mA LED current
SCT2400_EN_RISE_MAX_V = 1.4
SCT2400_EN_FALL_MIN_V = 0.9
SCT2400_EN_PULLDOWN_OHM = 480e3
KNX_BUS_MIN_V = 21.0                # KNX TP1 lowest voltage at a device
NCN_DROP_MAX_V = 4.25               # NCN5130 Table 4 coupler drop at 40 mA
NCN_COUPLER_MIN_A = 39.6e-3         # NCN5130 Table 4, R6 = 10k
NCN_OWN_MAX_A = 4.4e-3              # NCN5130 Table 4 IBUS1_Int at 40 mA
NCN_VFILTL_MAX_V = 9.4              # NCN5130 VFILT undervoltage trigger, max
EN_MARGIN_V = 0.3
SCT2400_EFFICIENCY = 0.82           # SCT2400 at about 0.1 A from 15-25 V
XIAO_EFFICIENCY = 0.88              # XIAO ESP32C6 SGM6029C 5 V -> 3.3 V buck (Seeed schematic v1.0)
XIAO_LOAD_3V3_A = {"cpu 160 MHz, peripherals on": 38e-3, "SSD1312 OLED": 15e-3,
                   "supply-off opto (external DALI PSU)": 6.4e-3, "DALI RX pull-up and TX opto": 1.7e-3}
WIFI_EXTRA_A = {"modem sleep": 12e-3, "continuous RX": 78e-3 - 17e-3}   # ESP32-C6 datasheet tables 5-7, 5-10
LEDS_5V_A = 3 * 5e-3
CONTINUOUS_WIFI_BUS_MIN_V = 24.0    # documented limit for wifi_power_save = false


def value_of(text, units):
    token = text.split()[0]
    m = re.fullmatch(rf"(\d+)([{"".join(units)}])(\d*)", token)
    assert m, f"cannot read {text!r}"
    return float(f"{m.group(1)}.{m.group(3) or 0}") * units[m.group(2)]


def ohms(text):
    return value_of(text, {"R": 1.0, "k": 1e3, "M": 1e6})


def farads(text):
    return value_of(text, {"p": 1e-12, "n": 1e-9, "u": 1e-6})


def board_with(net):
    for board in BOARDS:
        if net in bv.parse_netlist(board):
            return bv.parse_netlist(board), bv.parse_parts(board)
    pytest.skip(f"no {net} net in {bv.VARIANT}")


def part_between(nets, parts, a, b, prefix):
    refs = {r for r, _ in nets[a] if r.startswith(prefix)} & {r for r, _ in nets[b] if r.startswith(prefix)}
    assert len(refs) == 1, (a, b, refs)
    return parts[refs.pop()]["value"]


def net_of(nets, ref, pin):
    return next(name for name, nodes in nets.items() if (ref, pin) in nodes)


def tx():
    nets, parts = board_with("DALI_SENSE")
    return {"sense": ohms(part_between(nets, parts, "DALI_SENSE", "GNDD", "R")),
            "series": ohms(part_between(nets, parts, "DALI_GATE_SW", "DALI_GATE", "R")),
            "pulldown": ohms(part_between(nets, parts, "DALI_GATE", "GNDD", "R")),
            "boot": farads(part_between(nets, parts, "DALI_BOOT", "GNDD", "C"))}


def supply_limit_max_a():
    nets, parts = board_with("DALI_ISO15V")
    resistor = next(r for r, _ in nets["DALI_ISO15V"] if r.startswith("R"))
    return VBE_MAX / ohms(parts[resistor]["value"])


def test_tx_clamp_lets_the_bus_supply_set_the_low_level():
    clamp_min = VBE_MIN / tx()["sense"]
    assert clamp_min >= DALI_SINK_A * 1.1, f"TX clamp {clamp_min * 1e3:.0f} mA < 250 mA + 10 %"
    assert clamp_min > supply_limit_max_a(), "TX clamp would undercut the internal supply limiter"


def test_tx_low_level_at_250ma():
    low = 2 * BRIDGE_VF_V + DALI_SINK_A * (tx()["sense"] + MOSFET_RDS_OHM)
    assert low <= DALI_LOW_MAX_V, f"low level {low:.2f} V"


def gate_volts(boot_v, t):
    return min(GATE_CLAMP_V, boot_v * t["pulldown"] / (t["series"] + t["pulldown"]))


@pytest.mark.parametrize("bus_v", [DALI_HIGH_MIN_V, DALI_HIGH_MAX_V])
def test_tx_gate_drive_holds_through_the_longest_active_period(bus_v):
    """The bootstrap capacitor feeds the gate divider while the bus is low;
    at the end of a collision break V_GS must still reach the R_DS(on) point."""
    t = tx()
    boot = bus_v - SS16_VF_V
    current = (boot - gate_volts(boot, t)) / t["series"]
    droop = current * LONGEST_ACTIVE_S / t["boot"]
    vgs_end = gate_volts(boot - droop, t)
    assert vgs_end >= MOSFET_VGS_SPEC_V, f"V_GS {vgs_end:.2f} V after {droop:.2f} V bootstrap droop"


def test_tx_gate_turns_off_within_the_half_bit_tolerance():
    t = tx()
    vgs = gate_volts(DALI_HIGH_MAX_V - SS16_VF_V, t)
    discharge = t["pulldown"] * MOSFET_CISS_F * math.log(vgs / MOSFET_VTH_MAX_V)
    miller = MOSFET_QGD_C * t["pulldown"] / MOSFET_VTH_MAX_V
    budget = TX_HALF_BIT_MAX_EXCESS_S * GATE_TURN_OFF_SHARE
    assert discharge + miller <= budget, f"gate turn-off {(discharge + miller) * 1e6:.1f} us > {budget * 1e6:.0f} us"


TLP383_TURN_OFF_TYP_S = 28e-6      # TLP383 datasheet table 10: toff typ, IF 16 mA, RL 1.9 kOhm (saturated)
TLP383_TURN_ON_TYP_S = 0.95e-6
FIRMWARE_TX_ACTIVE_TRIM_S = 0.0     # gateway/src/dali/bus: no edge compensation of active half-bits yet


@pytest.mark.xfail(strict=True, reason="review-2026-10-23 E13: needs firmware TX edge compensation; open")
def test_tx_active_half_bit_stays_inside_the_transmitter_window():
    """The TX opto runs saturated (6 mA LED, about 1 mA collector), so its
    storage time adds to the gate discharge on every active half-bit."""
    t = tx()
    vgs = gate_volts(DALI_HIGH_MAX_V - SS16_VF_V, t)
    gate_off = t["pulldown"] * MOSFET_CISS_F * math.log(vgs / MOSFET_VTH_MAX_V)
    gate_off += MOSFET_QGD_C * t["pulldown"] / MOSFET_VTH_MAX_V
    stretch = TLP383_TURN_OFF_TYP_S + gate_off - TLP383_TURN_ON_TYP_S - FIRMWARE_TX_ACTIVE_TRIM_S
    assert abs(stretch) <= TX_HALF_BIT_MAX_EXCESS_S, f"active half-bit stretched by {stretch * 1e6:.0f} us"


def rx():
    nets, parts = board_with("RX_LIM_IN")
    pass_q = next(r for r, p in nets["RX_LIM_IN"] if r.startswith("Q") and p == "3")
    emitter = net_of(nets, pass_q, "2")
    return {"emitter": ohms(part_between(nets, parts, emitter, "GNDD", "R")),
            "pullup": ohms(part_between(nets, parts, "DALI_RX", "+3V3", "R"))}


def test_dali_quiescent_bus_current():
    current = VBE_MAX / rx()["emitter"]
    assert current <= DALI_QUIESCENT_MAX_A, f"{current * 1e3:.2f} mA"


def test_rx_optocoupler_saturates_its_pull_up():
    r = rx()
    led_min = VBE_MIN / r["emitter"]
    assert OPTO_CTR_MIN * led_min >= 1.1 * 3.3 / r["pullup"]


def buck():
    nets, parts = board_with("VFILT")
    en = net_of(nets, "U2", "4")
    top = ohms(part_between(nets, parts, "VFILT", en, "R"))
    bottom = ohms(part_between(nets, parts, en, "GND", "R"))
    return top, 1 / (1 / bottom + 1 / SCT2400_EN_PULLDOWN_OHM)


def test_buck_starts_at_the_lowest_knx_bus_voltage():
    top, bottom = buck()
    turn_on_max = SCT2400_EN_RISE_MAX_V * (1 + top / bottom)
    vfilt_min = KNX_BUS_MIN_V - NCN_DROP_MAX_V
    assert turn_on_max <= vfilt_min - EN_MARGIN_V, f"turn-on up to {turn_on_max:.2f} V, VFILT {vfilt_min:.2f} V"


def test_buck_stops_before_the_ncn5130_resets():
    top, bottom = buck()
    assert SCT2400_EN_FALL_MIN_V * (1 + top / bottom) >= NCN_VFILTL_MAX_V + EN_MARGIN_V


SCT2400_FB_V = (0.79, 0.83)         # SCT2400 datasheet VFB 0.79-0.83 V
FB_RESISTOR_TOL = 0.01
AUX_CONVERTER_V = (4.95, 5.05)      # TRACO TMR 2411 / TRN 3-2411 set accuracy +/-1 %
AUX_CONVERTER_A = {"TMR 2411": 0.400, "TRN 3-2411": 0.600}  # TRACO datasheets, 5 V models
TPS2121_PR1_RISE_MAX_V = 1.10       # TPS2121 datasheet VPR1 rising 1.01-1.10 V
AHCT_VCC_MIN_V = 4.5                # SN74AHCT1G125 recommended VCC minimum
PR1_MARGIN = 1.05


def core_mux():
    nets, parts = board_with("CORE_PR1")
    return (ohms(part_between(nets, parts, "+5V_AUX", "CORE_PR1", "R")),
            ohms(part_between(nets, parts, "CORE_PR1", "GND", "R")))


def aux_converter_a():
    _, parts = board_with("+5V_AUX")
    return AUX_CONVERTER_A[parts["U9"]["mpn"]]


def test_aux_converter_keeps_priority_at_its_lowest_output():
    top, bottom = core_mux()
    pr1 = AUX_CONVERTER_V[0] * bottom * (1 - FB_RESISTOR_TOL) / (top * (1 + FB_RESISTOR_TOL) + bottom)
    assert pr1 >= TPS2121_PR1_RISE_MAX_V * PR1_MARGIN, f"PR1 {pr1:.3f} V"


TPS2121_PR1_FALL_MIN_V = 0.99       # TPS2121 datasheet VPR1 falling 0.99-1.09 V
TPS2121_SWITCHOVER_S = 100e-6       # TPS2121 tSW max with CP2 < VREF (5 us tFSW only with CP2 >= VREF)
TPS2121_FAST_SWITCHOVER_S = 5e-6
ESP32C6_TX_PEAK_A = 0.354           # ESP32-C6 datasheet table 5-7: 802.11b 1 Mbps at 21 dBm, whole chip
MLCC_BIAS_RETAINED = {"10u": 0.5,    # 10 uF 10 V X5R 0603/0805 at 5 V DC bias (Samsung CL10A/CL21A curves)
                      "100u": 0.3}   # 100 uF 6.3 V X5R 1206 (CL31A107MQHNNNE) at 5 V, estimate from the series curve
CORE_RAIL_MIN_V = 3.5               # XIAO SGM6029C in dropout still gives the ESP32-C6 its 3.0 V minimum


def core_rail_capacitance_f():
    """Ceramics on the mux output and behind the LM66100; the XIAO's own
    input capacitors are not counted (not in the Seeed BOM we can verify)."""
    total = 0.0
    for board in BOARDS:
        nets, parts = bv.parse_netlist(board), bv.parse_parts(board)
        for net in ("+5V", "+5V_XIAO"):
            for ref, _ in nets.get(net, ()):
                if ref.startswith("C"):
                    value = parts[ref]["value"]
                    total += farads(value) * MLCC_BIAS_RETAINED.get(value.split()[0], 1.0)
    return total


def tx_peak_5v_a():
    peripherals_3v3 = sum(v for k, v in XIAO_LOAD_3V3_A.items() if not k.startswith("cpu"))
    return (ESP32C6_TX_PEAK_A + peripherals_3v3) * 3.3 / XIAO_EFFICIENCY / 5.0 + LEDS_5V_A


def test_aux_converter_carries_the_wifi_tx_peak():
    """Otherwise every TX burst drags AUX below the PR1 threshold and hands
    the core to the KNX path through the TPS2121 switchover."""
    assert tx_peak_5v_a() <= aux_converter_a(), f"{tx_peak_5v_a() * 1e3:.0f} mA of {aux_converter_a() * 1e3:.0f} mA"


@pytest.mark.xfail(strict=True, reason="review-2026-10-23 E12: AUX unplugged during a TX burst still waits up "
                                       "to 100 us for the switchover (CP2 grounded); open")
def test_core_rail_rides_through_the_mux_switchover():
    """AUX lost during a WiFi TX burst: the core caps alone carry the load
    for the whole TPS2121 switchover."""
    nets, _ = board_with("CORE_PR1")
    top, bottom = core_mux()
    v_switch = TPS2121_PR1_FALL_MIN_V * (1 + top * (1 - FB_RESISTOR_TOL) / (bottom * (1 + FB_RESISTOR_TOL)))
    t_switch = TPS2121_SWITCHOVER_S if net_of(nets, "U10", "3") == "GND" else TPS2121_FAST_SWITCHOVER_S
    load_5v = tx_peak_5v_a()
    v_min = v_switch - load_5v * t_switch / core_rail_capacitance_f()
    assert v_min >= CORE_RAIL_MIN_V, f"core rail dips to {v_min:.2f} V ({t_switch * 1e6:.0f} us at {load_5v * 1e3:.0f} mA)"


def test_knx_buck_alone_runs_the_core_and_the_led_buffer():
    nets, parts = board_with("VFILT")
    fb = net_of(nets, "U2", "3")
    out = next(n for n in ("+5V_BUCK", "+5V") if n in nets)
    top = ohms(part_between(nets, parts, out, fb, "R"))
    bottom = ohms(part_between(nets, parts, fb, "GND", "R"))
    buck_min = SCT2400_FB_V[0] * (1 + top * (1 - FB_RESISTOR_TOL) / (bottom * (1 + FB_RESISTOR_TOL)))
    assert buck_min >= AHCT_VCC_MIN_V, f"buck down to {buck_min:.2f} V"


def test_aux_converter_carries_the_sustained_core_load():
    load_5v = (sum(XIAO_LOAD_3V3_A.values()) + WIFI_EXTRA_A["continuous RX"]) * 3.3 / XIAO_EFFICIENCY / 5.0 + LEDS_5V_A
    assert load_5v <= 0.8 * aux_converter_a(), f"{load_5v * 1e3:.0f} mA of {aux_converter_a() * 1e3:.0f} mA"


def vfilt_load_w(wifi_a):
    load_5v = (sum(XIAO_LOAD_3V3_A.values()) + wifi_a) * 3.3 / XIAO_EFFICIENCY + 5.0 * LEDS_5V_A
    return load_5v / SCT2400_EFFICIENCY


def lowest_bus_v(wifi_a):
    """Bus voltage at which the NCN5130 coupler limit just carries the load."""
    return NCN_DROP_MAX_V + vfilt_load_w(wifi_a) / (NCN_COUPLER_MIN_A - NCN_OWN_MAX_A)


def test_knx_power_budget_modem_sleep_at_the_lowest_bus_voltage():
    assert lowest_bus_v(WIFI_EXTRA_A["modem sleep"]) <= KNX_BUS_MIN_V - 1.0


def test_knx_power_budget_continuous_wifi_at_the_documented_bus_voltage():
    """wifi_power_save = false is only allowed from CONTINUOUS_WIFI_BUS_MIN_V
    (docs); TX bursts above the average come from the 470 uF VFILT store."""
    assert lowest_bus_v(WIFI_EXTRA_A["continuous RX"]) <= CONTINUOUS_WIFI_BUS_MIN_V - 0.5
