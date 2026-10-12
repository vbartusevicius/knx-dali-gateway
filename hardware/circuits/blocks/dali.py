"""DALI block: AUX input protection + isolated DC-DC (RECOM REM3-2415S 15V)
+ two-transistor current limiter (~180 mA) + supply jumpers + bridge
rectifier + opto-isolated TX (bootstrap reservoir) + RX (zener threshold).

Returns: dali_tx, dali_rx (MCU-side logic signals).

TX design (fail-safe bootstrap reservoir):
  GPIO low / MCU reset / unpowered -> opto LED off -> MOSFET off -> bus released.
  GPIO high -> opto LED on -> opto transistor conducts -> gate pulled up
  toward bootstrap reservoir -> MOSFET on -> bus pulled low.
  Bootstrap reservoir: diode from DA_P + 1uF cap feeds opto collector.
  Gate series R 22k limits bootstrap discharge; gate pull-down 100k ensures
  off when opto off. 12V zener clamps V_GS.
  Q2/R_sense limits TX current: R_sense=3R3 -> 180-230mA (V_BE 0.6-0.75V).

  Low level at 250mA: 2 bridge drops (1.4V) + R_sense (0.83V) + R_DS(on)
  ~ 2.3V <= 4.5V.

  Reservoir droop over 1ms low: gate current ~12V/22k = 0.55mA.
  Droop = 0.55mA * 1ms / 1uF = 0.55V (V_GS drops 12V -> 11.45V, OK).

RX design (zener threshold + two-NPN current limiter):
  5V6 zener + two-NPN limiter at ~0.9mA.
  Bus < 6.5V -> DA_P < 5.1V -> zener off -> LED off -> DALI_RX high (pull-up).
  Bus >= 9.5V -> DA_P >= 8.1V -> zener on -> limiter -> LED on ->
  DALI_RX low (opto transistor sinks).
  Polarity: bus high -> DALI_RX low; bus low -> DALI_RX high (inverted).

  Bus current at 22.5V: RX 0.9mA (limited) + TX bootstrap ~0.55mA = 1.45mA
  <= 2mA per IEC 62386-101.

DALI supply limiter (high-side PNP):
  Q_pass MJD45H11 PNP (not Darlington): E=iso15, C=sw_in, B driven.
  R_sense 3R3 between iso15 and Q_pass.E.
  Q_sense BC857 PNP: E=iso15 (before R_sense), B=Q_pass.E (after R_sense),
    C=Q_pass.B. When V_R_sense > V_BE, Q_sense steals base drive.
  R_pull 4k7 from Q_pass.B to GNDD (turns Q_pass on; I_B = 14.3V/4k7 = 3mA,
  h_FE(min)=40 -> I_C_sat = 120mA, derated to 180mA at higher h_FE).
  I_limit = 0.6V / 3R3 = 182mA.

  Bus idle voltage: iso15 - V_ECsat(Q_pass) ~ 15 - 0.5 = 14.5V (not 12.5V
  as with MJD117 Darlington V_CE(sat) ~ 2V).

Internal-supply jumpers JP1 (2x2 header, 3A; no slide switch rated for the
bus current was in stock):
  Jumpers fitted: DA1=sw_in(+), DA2=GNDD(-) -> polarity-defined
  Jumpers removed: DA1/DA2 -> bridge only -> polarity-free

AUX protection:
  PTC 1812L110/33 (1.1A hold, 33V) + reverse-polarity Schottky + TVS SMAJ33A.
  DALI-side TVS SMAJ24A across DA_P-GNDD (24VWM > 22.5V bus max).
  Residual risk: REM3 36V max; AUX TVS V_BR=36.7V slightly exceeds REM3 max.
"""

from common import block, part, power, power_flag, src
from skidl import Net

R0603 = "Resistor_SMD:R_0603_1608Metric"
R1206 = "Resistor_SMD:R_1206_3216Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
D_SMA = "Diode_SMD:D_SMA"
D_SOD123 = "Diode_SMD:D_SOD-123"
D_BRIDGE = "Diode_SMD:Diode_Bridge_Vishay_DFS"
SOT23 = "Package_TO_SOT_SMD:SOT-23"
SO6L = "Package_SO:SO-6L_10x3.84mm_P1.27mm"
DPAK = "Package_TO_SOT_SMD:TO-252-2"
FUSE1812 = "Fuse:Fuse_1812_4532Metric"
TERM_5MM = "TerminalBlock:TerminalBlock_MaiXu_MX126-5.0-02P_1x02_P5.00mm"
DCDC_REM3 = "gateway_fp:Converter_DCDC_RECOM_REM3_DIP24"
JUMPER_2X2 = "Connector_PinHeader_2.54mm:PinHeader_2x02_P2.54mm_Vertical"


def res(value, fields=None, footprint=R0603):
    return part("Device", "R", value, footprint, fields)


def cap(value, fields=None, footprint=C0603):
    return part("Device", "C", value, footprint, fields)


def build(gnd, v3):
    """Build the DALI block. Returns (dali_tx, dali_rx)."""
    gndd = power("GNDD")
    gnda = power("GNDA")
    power_flag(gnda)
    da1 = Net("DA1")
    da2 = Net("DA2")
    da_p = Net("DA_P")
    aux_p = Net("AUX_P")
    aux_fused = Net("AUX_FUSED")
    iso15 = Net("DALI_ISO15V")
    sw_in = Net("DALI_SUPPLY_SW")
    dali_tx = Net("DALI_TX")
    dali_rx = Net("DALI_RX")
    for n in (da_p, da1, da2, aux_p, aux_fused, sw_in):
        power_flag(n)

    # --- AUX input protection ---
    block("aux_in")
    j2 = part("Connector", "Screw_Terminal_01x02", "AUX", TERM_5MM,
              src(mpn="MX126-5.0-02P-GN01-Cu-S-A", mfr="MaiXu", lcsc="C5188434",
                  source="spec.yaml terminals.aux_dc"), ref="J2")
    f1 = part("Device", "Polyfuse", "1.1A", FUSE1812,
              src(mpn="1812L110/33MR", mfr="Littelfuse", lcsc="C142747",
                  source="AUX PTC 1812 1.1A hold 33V; REM3 input protection"))
    f1.fields["Place"] = "aux_fuse"
    j2[1] += f1[1]
    f1[2] += aux_fused
    j2[2] += gnda
    d_aux_rev = part("Device", "D_Schottky", "SS16", D_SMA,
                     src(mpn="SS16", mfr="MDD", lcsc="C2481",
                         source="AUX reverse polarity; 60V 1A Schottky SMA"))
    d_aux_rev["A"] += aux_fused
    d_aux_rev["K"] += aux_p
    d_aux_tvs = part("Device", "D_Zener", "SMAJ33A", D_SMA,
                     src(mpn="SMAJ33A", mfr="Littelfuse", lcsc="C113966",
                         source="AUX TVS, unidirectional; V_RWM=33V V_BR=36.7V "
                                "V_C=53.3V; residual risk: REM3 36V max"))
    d_aux_tvs["K"] += aux_p
    d_aux_tvs["A"] += gnda

    # --- Isolated DC-DC: RECOM REM3-2415S/A (15V output, reinforced) ---
    block("dcdc_iso")
    c_aux_in = cap("10u 50V",
                   src(source="RECOM REM3 datasheet: input cap 10uF"),
                   footprint=C0805)
    c_aux_in[1] += aux_p
    c_aux_in[2] += gnda
    u3 = part("gateway", "REM3_DCDC", "REM3-2415S/A 24V->15V 3W iso",
              DCDC_REM3,
              src(mpn="REM3-2415S/A", mfr="RECOM", hand=True,
                  source="RECOM REM3 datasheet: 18-36V in, 15V 200mA out, "
                         "5kVAC reinforced 2MOPP, DIP-24 A-pinning"))
    u3["+Vin"] += aux_p
    u3["-Vin"] += gnda
    u3["+Vout"] += iso15
    u3["-Vout"] += gndd
    c_iso_out = cap("10u 25V",
                    src(source="RECOM REM3 datasheet: output cap 10uF 25V"),
                    footprint=C0805)
    c_iso_out[1] += iso15
    c_iso_out[2] += gndd
    c_iso_out.fields["Place"] = "dali_tx"

    # --- Two-transistor current limiter (~180 mA, high-side PNP) ---
    # Q_pass MJD45H11 PNP (not Darlington): E=iso15 (via R_sense), C=sw_in
    # R_sense 3R3: iso15 -> Q_pass.E
    # Q_sense BC857 PNP: E=iso15 (before R_sense), B=Q_pass.E (after), C=Q_pass.B
    # R_pull 4k7: Q_pass.B -> GNDD (turns Q_pass on; I_B=14.3/4k7=3mA)
    # I_limit = V_BE / R_sense = 0.6 / 3R3 = 182mA
    block("dali_limit")
    q_pass = part("Transistor_BJT", "Q_PNP_BCE", "MJD45H11", DPAK,
                  src(mpn="MJD45H11", mfr="CJ", lcsc="C48580717",
                      source="PNP pass 80V 8A DPAK; h_FE(min)=40; "
                             "V_ECsat~0.5V -> bus idles ~14.5V"))
    q_sense = part("Transistor_BJT", "BC857", "BC857", SOT23,
                   src(mpn="BC857C", mfr="Guangdong Hottech", lcsc="C181147",
                       source="Current-limit sense PNP; E=iso15, B=Q_pass.E"))
    r_sense = res("3R3", src(source="I_limit = 0.6V/3R3 = 182mA"),
                  footprint=R1206)
    r_pull = res("4k7", src(source="Q_pass base pull-down; I_B=14.3/4k7=3mA"))
    r_sense[1] += iso15
    r_sense[2] += q_pass["E"]
    q_pass["C"] += sw_in
    q_pass["B"] += r_pull[1]
    r_pull[2] += gndd
    q_sense["E"] += iso15
    q_sense["B"] += q_pass["E"]
    q_sense["C"] += q_pass["B"]

    # Internal-supply jumpers: both fitted -> DA1=sw_in(+), DA2=GNDD(-);
    # both removed -> external DALI supply, terminals go to the bridge only.
    block("dali_switch")
    jp1 = part("Connector_Generic", "Conn_02x02_Odd_Even", "DALI supply", JUMPER_2X2,
               src(mpn="2.54-2*2PPin", mfr="BOOMELE", lcsc="C66690",
                   source="2x2 header, 3A; two jumper caps C100114 (hand-fit) "
                          "enable the internal supply"), ref="JP1")
    jp1[1] += sw_in
    jp1[2] += da1
    jp1[3] += gndd
    jp1[4] += da2

    # --- DALI terminal + bridge rectifier ---
    block("dali_terminal")
    j3 = part("Connector", "Screw_Terminal_01x02", "DALI", TERM_5MM,
              src(mpn="MX126-5.0-02P-GN01-Cu-S-A", mfr="MaiXu", lcsc="C5188434",
                  source="spec.yaml terminals.dali"), ref="J3")
    j3[1] += da1
    j3[2] += da2

    block("dali_bridge")
    d5 = part("Diode_Bridge", "DF06S", "DF06S", D_BRIDGE,
              src(mpn="DF06S-T", mfr="MDD", lcsc="C460867",
                  source="DALI bridge rectifier; 600V 1A; 2 drops ~1.4V"))
    d5[3] += da1
    d5[4] += da2
    d5["+"] += da_p
    d5["-"] += gndd

    # --- DALI-side TVS across DA_P-GNDD (standoff 24V > 22.5V bus max) ---
    d_dali_tvs = part("Device", "D_Zener", "SMAJ24A", D_SMA,
                      src(mpn="SMAJ24A", mfr="Littelfuse", lcsc="C148222",
                          source="DALI bus TVS, unidirectional; V_RWM=24V > 22.5V "
                                 "bus max; V_C=38.9V"))
    d_dali_tvs["K"] += da_p
    d_dali_tvs["A"] += gndd

    # --- TX path: opto -> bootstrap reservoir -> MOSFET gate ---
    block("dali_tx_led")
    u4 = part("gateway", "TLP383", "TLP383(D4BLLTL,E", SO6L,
              src(mpn="TLP383(D4BLLTL,E", mfr="Toshiba", lcsc="C3002263",
                  source="DALI TX isolation; SO6L 8mm creepage; BL rank"))
    r_led_tx = res("330R", src(source="DALI TX opto LED: (3V3-1.2V)/330R=6mA"))
    r_led_tx[1] += dali_tx
    r_led_tx[2] += u4[1]
    u4[3] += gnd
    r_tx_pd = res("10k", src(source="MCU-off defined state: GPIO low -> opto off"))
    r_tx_pd[1] += dali_tx
    r_tx_pd[2] += gnd

    block("dali_tx")
    boot = Net("DALI_BOOT")
    gate = Net("DALI_GATE")
    gate_sw = Net("DALI_GATE_SW")
    # Bootstrap reservoir: diode from DA_P + 1uF cap
    d_boot = part("Device", "D_Schottky", "SS16", D_SMA,
                  src(mpn="SS16", mfr="MDD", lcsc="C2481",
                      source="Bootstrap charge diode from DA_P; 60V 1A SMA"))
    d_boot["A"] += da_p
    d_boot["K"] += boot
    c_boot = cap("1u 50V",
                 src(source="Bootstrap reservoir; droop 0.55V over 1ms at 0.55mA"),
                 footprint=C0805)
    c_boot[1] += boot
    c_boot[2] += gndd
    # Opto transistor: collector from bootstrap, emitter via 22k to gate
    u4[6] += boot
    u4[4] += gate_sw
    # Gate series resistor: limits bootstrap discharge to ~0.55mA
    r_gate = res("22k", src(source="Gate series R; limits bootstrap discharge; "
                                     "I=12V/22k=0.55mA; droop 0.55V/1ms"))
    r_gate[1] += gate_sw
    r_gate[2] += gate
    # Gate pull-down (fail-safe: opto off -> gate low -> MOSFET off)
    r_gate_pd = res("100k", src(source="Gate pull-down; fail-safe MOSFET off"))
    r_gate_pd[1] += gate
    r_gate_pd[2] += gndd
    # Gate zener (clamps V_GS to 12V at high bus voltage)
    dz_gate = part("Device", "D_Zener", "12V", D_SOD123,
                   src(mpn="MMSZ12T1G", mfr="onsemi", lcsc="C72599",
                       source="Gate zener 12V ±5% SOD-123; V_GS clamp"))
    dz_gate["K"] += gate
    dz_gate["A"] += gndd
    # MOSFET: 60V 2.7A SOT-23 (Infineon IRLML0060)
    q1 = part("Transistor_FET", "IRLML0030", "IRLML0060", SOT23,
              src(mpn="IRLML0060TRPBF", mfr="Infineon", lcsc="C67274",
                  source="DALI TX sink MOSFET; 60V 2.7A SOT-23; "
                         "V_GS(th) max 2.5V; R_DS(on) 92mΩ@10V"))
    q1["G"] += gate
    q1["D"] += da_p
    sense = Net("DALI_SENSE")
    q1["S"] += sense
    # R_sense for TX current limit: 3.3Ω -> 180-230mA
    r_sense_tx = res("3R3", src(source="TX current limit: 0.65V/3R3=197mA"),
                     footprint=R1206)
    r_sense_tx[1] += sense
    r_sense_tx[2] += gndd
    # Q2 NPN current-limit clamp
    q2 = part("Transistor_BJT", "BC847", "BC847", SOT23,
              src(mpn="BC847B", mfr="Nexperia", lcsc="C57668",
                  source="TX current-limit clamp NPN"))
    q2["B"] += sense
    q2["E"] += gndd
    q2["C"] += gate

    # --- RX path: zener threshold + two-NPN current limiter + opto LED ---
    block("dali_rx")
    u5 = part("gateway", "TLP383", "TLP383(D4BLLTL,E", SO6L,
              src(mpn="TLP383(D4BLLTL,E", mfr="Toshiba", lcsc="C3002263",
                  source="DALI RX isolation; SO6L 8mm creepage; BL rank"))
    # 5V6 zener: bus < 6.5V -> DA_P < 5.1V -> zener off -> LED off
    #            bus >= 9.5V -> DA_P >= 8.1V -> zener on -> limiter -> LED on
    dz_rx = part("Device", "D_Zener", "5V6", D_SOD123,
                 src(mpn="MMSZ5V6T1G", mfr="onsemi", lcsc="C236111",
                     source="RX threshold zener 5V6 ±5% SOD-123"))
    dz_rx["K"] += da_p
    # Two-NPN current limiter (~0.9mA):
    # Q_rx_pass NPN: C=zener cathode, E=R_E->GNDD, B=biased by R_bias
    # Q_rx_sense NPN: B=Q_rx_pass.E (top of R_E), E=GNDD, C=Q_rx_pass.B
    # R_bias 100k: zener cathode -> Q_rx_pass.B
    # R_E 680R: Q_rx_pass.E -> GNDD; I_limit = 0.6V/680R = 0.88mA
    # LED opto in series between zener and limiter
    rx_lim_in = Net("RX_LIM_IN")
    dz_rx["A"] += u5[1]
    u5[3] += rx_lim_in
    q_rx_pass = part("Transistor_BJT", "BC847", "BC847", SOT23,
                     src(mpn="BC847B", mfr="Nexperia", lcsc="C57668",
                         source="RX limiter pass NPN"))
    q_rx_sense = part("Transistor_BJT", "BC847", "BC847", SOT23,
                      src(mpn="BC847B", mfr="Nexperia", lcsc="C57668",
                          source="RX limiter sense NPN"))
    r_rx_bias = res("100k", src(source="RX limiter bias R; Q_pass.B drive"))
    r_rx_e = res("680R", src(source="RX limiter R_E; I_limit=0.6V/680R=0.88mA"))
    q_rx_pass["C"] += rx_lim_in
    q_rx_pass["E"] += r_rx_e[1]
    r_rx_e[2] += gndd
    r_rx_bias[1] += rx_lim_in
    r_rx_bias[2] += q_rx_pass["B"]
    q_rx_sense["B"] += q_rx_pass["E"]
    q_rx_sense["E"] += gndd
    q_rx_sense["C"] += q_rx_pass["B"]
    u5[6] += dali_rx
    u5[4] += gnd

    block("dali_rx_selv")
    r_rx_pu = res("10k", src(source="DALI_RX pull-up; bus high -> RX low"))
    r_rx_pu[1] += v3
    r_rx_pu[2] += dali_rx

    return {"dali_tx": dali_tx, "dali_rx": dali_rx}


NET_CLASSES = {
    "SELV-AUX": ["GNDA", "AUX_P", "AUX_FUSED"],
    "DALI": ["GNDD", "DA1", "DA2", "DA_P", "DALI_GATE", "DALI_GATE_SW",
             "DALI_SENSE", "DALI_BOOT", "DALI_ISO15V", "DALI_SUPPLY_SW",
             "RX_LIM_IN"],
}
