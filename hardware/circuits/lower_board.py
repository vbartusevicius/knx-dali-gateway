# SPDX-FileCopyrightText: The knx-dali-gateway contributors
# SPDX-License-Identifier: CERN-OHL-P-2.0
"""Lower board circuit: terminals, KNX transceiver + power path, DALI
interface, auxiliary-input isolated DALI supply, board interconnect.

Topology follows the lead-authored hardware/docs/architecture.md:

  * KNX domain (floating SELV, net class SELV): NCN5130 per its datasheet
    application circuit, DC-DC1 = 3V3 transceiver rail, DC-DC2 charges the
    ~12 V storage rail, a buck generates +3V3_MCU (>=500 mA) for the ESP32.
  * DALI domain (non-SELV, net class DALI): bridge rectifier, opto-isolated
    TX with ~270 mA constant-current bus sink, opto-isolated RX with a zener
    threshold, and the switchable internal ~16 V bus supply.
  * AUX domain (SELV, net class AUX, galvanically separated from KNX):
    protected 24/30 V input feeding an isolated DC-DC converter.

Component values flagged TBD are deliberate review placeholders.
"""

from builtins import NC  # injected by skidl's default Circuit

from skidl import Net

from common import block, part, power, power_flag, src

R0603 = "Resistor_SMD:R_0603_1608Metric"
R1206 = "Resistor_SMD:R_1206_3216Metric"
R2512 = "Resistor_SMD:R_2512_6332Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C_ELEC = "Capacitor_THT:CP_Radial_D12.5mm_P5.00mm"
D_SMA = "Diode_SMD:D_SMA"
D_SMB = "Diode_SMD:D_SMB"
D_SOD123 = "Diode_SMD:D_SOD-123"
D_BRIDGE = "Diode_SMD:Diode_Bridge_Vishay_DFS"
SOT23 = "Package_TO_SOT_SMD:SOT-23"
SOT23_6 = "Package_TO_SOT_SMD:TSOT-23-6"
SOT223 = "Package_TO_SOT_SMD:SOT-223-3_TabPin2"
SOP4 = "Package_SO:SOP-4_4.4x2.6mm_P1.27mm"
SOIC14 = "Package_SO:SOIC-14_3.9x8.7mm_P1.27mm"
TERM_5MM = "TerminalBlock:TerminalBlock_MaiXu_MX126-5.0-02P_1x02_P5.00mm"
HDR_KNX = "Connector_PinHeader_1.00mm:PinHeader_1x02_P1.00mm_Vertical"
HDR_DCDC = "Converter_DCDC:Converter_DCDC_TRACO_TDN_5-xxxxWISM_SMD"
JST_SH10 = "Connector_JST:JST_SH_SM10B-SRSS-TB_1x10-1MP_P1.00mm_Horizontal"
QFN40 = "Package_DFN_QFN:QFN-40-1EP_6x6mm_P0.5mm_EP4.6x4.6mm"
L1210 = "Inductor_SMD:L_1210_3225Metric"
SW_SLIDE = "Button_Switch_SMD:SW_SPDT_PCM12"
FUSE1812 = "Fuse:Fuse_1812_4532Metric"
MOUNT = "MountingHole:MountingHole_3.2mm_M3"


def res(value, fields=None, footprint=R0603):
    return part("Device", "R", value, footprint, fields)


def cap(value, fields=None, footprint=C0603):
    return part("Device", "C", value, footprint, fields)


def d_schottky(value, footprint=D_SOD123, fields=None):
    return part("Device", "D_Schottky", value, footprint, fields)


def build():
    # --- power-domain nets -------------------------------------------------
    gnd = power("GND")       # KNX-domain logic ground (SELV)
    gnda = power("GNDA")     # auxiliary-input ground (SELV, separate)
    gndd = power("GNDD")     # DALI-domain ground
    power_flag(gnd)
    power_flag(gnda)
    power_flag(gndd)

    knx_p = Net("KNX_P")            # KNX bus + after terminal
    knx_p_prot = Net("KNX_P_PROT")  # after reverse-polarity diode
    v3t = Net("+3V3_TRX")           # NCN5130 DC-DC1 output
    store = Net("+12V_STORE")       # NCN5130 DC-DC2 storage rail
    v3m = Net("+3V3_MCU")           # buck output for the upper board
    da1 = Net("DA1")
    da2 = Net("DA2")
    da_p = Net("DA_P")              # rectified DALI +
    iso24 = Net("DALI_ISO24V")
    v20 = Net("DALI_20V")
    sw_in = Net("DALI_SUPPLY_SW")
    aux_p = Net("AUX_P")
    aux_fused = Net("AUX_FUSED")
    knx_txd = Net("KNX_TXD")
    knx_rxd = Net("KNX_RXD")
    dali_tx = Net("DALI_TX")
    dali_rx = Net("DALI_RX")
    spare1 = Net("INT_SPARE1")
    spare2 = Net("INT_SPARE2")
    for n in (knx_p, knx_p_prot, v3t, store, v3m, da_p, iso24, v20, sw_in,
              aux_p, aux_fused, da1, da2):
        power_flag(n)

    # =======================================================================
    # KNX bus terminal + surge + reverse polarity (NCN5130 datasheet bus
    # coupling; protection values TBD — draft).
    # =======================================================================
    block("knx_in")
    j1 = part("Connector", "Conn_01x02_Pin", "KNX bus terminal pins", HDR_KNX,
              src(mpn="TBD knx bus terminal pins", source="spec.yaml terminals.knx"))
    j1[1] += knx_p
    j1[2] += gnd                        # bus return = chip ground domain

    d_tvs = part("Device", "D_TVS", "SMBJ33A TBD", D_SMB,
                 src(mpn="TBD ~33V TVS", source="KNX bus surge protection, review"))
    d_tvs[1] += gnd
    d_tvs[2] += knx_p

    d_rev = d_schottky("TBD 60V 1A", D_SMA,
                       src(mpn="TBD", source="reverse polarity series diode, review"))
    d_rev[2] += knx_p
    d_rev[1] += knx_p_prot

    # =======================================================================
    # U1 NCN5130 — onsemi datasheet + NCN5130ASGEVB evaluation schematic.
    # UART mode (MODE1=MODE2=TREQ=0), UC1=0 -> 19200 bps, UC2=1 -> 8-bit.
    # =======================================================================
    block("ncn5130")
    u1 = part("gateway", "NCN5130", "NCN5130MNTWG", QFN40,
              src(mpn="NCN5130MNTWG", mfr="onsemi", lcsc="TBD",
                  source="onsemi NCN5130 datasheet, Table 1 pinout"))

    vfilt = Net("VFILT")
    v20v = Net("V20V")
    ccp_a = Net("CCP")
    ccp_b = Net("CCP_B")
    cav = Net("CAV")
    ceq1 = Net("CEQ1")
    ceq2 = Net("CEQ2")
    vsw1 = Net("VSW1")
    vsw2 = Net("VSW2")
    fb2 = Net("VDD2MV")

    u1["VBUS1"] += knx_p_prot
    u1["VBUS2"] += gnd
    u1["VSSA"] += gnd
    u1["VSSD"] += gnd
    u1["VSS1"] += gnd
    u1["VSS2"] += gnd

    u1["VFILT"] += vfilt
    u1["V20V"] += v20v
    u1["TX0"] += ccp_a
    u1["CCP"] += ccp_b
    u1["CAV"] += cav
    u1["CEQ1"] += ceq1
    u1["CEQ2"] += ceq2
    u1["VIN"] += vfilt
    u1["VSW1"] += vsw1
    u1["VSW2"] += vsw2
    u1["VDD1"] += v3t
    u1["VDD1M"] += v3t
    u1["VDD2"] += store
    u1["VDD2MC"] += store
    u1["VDD2MV"] += fb2
    u1["VDDA"] += v3t
    u1["VDDD"] += v3t

    # interface configuration: UART, 8-bit, 19200 bps
    for pin in ("MODE1", "MODE2", "TREQ", "CSB/UC1", "XSEL", "XCLKC"):
        u1[pin] += gnd
    u1["SCK/UC2"] += v3t          # UC2=1 -> 8-bit UART
    u1["SDI/RXD"] += knx_rxd      # MCU TX -> transceiver
    u1["SDO/TXD"] += knx_txd      # transceiver -> MCU RX
    u1["SAVEB"] += spare1
    u1["RESETB"] += spare2
    for pin in ("TRIG", "XCLK", "XTAL1", "XTAL2", "ANAOUT"):
        u1[pin] += NC

    # fan-in resistor sets the 40 mA bus-current draw (datasheet TBD value)
    r_fan = res("TBD ~10k", src(source="NCN5130 datasheet, fan-in 40 mA, review"))
    u1["FANIN"] += r_fan[1]
    r_fan[2] += gnd

    # analogue pin capacitors — datasheet typical values, all TBD/review
    c_ccp = cap("TBD", src(source="NCN5130 AC coupling cap TX0-CCP, review"))
    c_ccp[1] += ccp_a
    c_ccp[2] += ccp_b
    for net_, val in ((cav, "TBD 1u"), (ceq1, "TBD"), (ceq2, "TBD"),
                      (vfilt, "TBD 10u"), (v20v, "TBD 1u")):
        c = cap(val, src(source="NCN5130 datasheet typical application, review"))
        c[1] += net_
        c[2] += gnd

    # DC-DC1 buck: VSW1 -> L1 -> +3V3_TRX, catch diode GND -> VSW1
    block("dcdc1")
    l1 = part("Device", "L", "TBD 68uH", L1210,
              src(source="NCN5130 datasheet DC-DC1, review"))
    l1[1] += vsw1
    l1[2] += v3t
    d1 = d_schottky("TBD 60V 1A", fields=src(source="DC-DC1 catch diode, review"))
    d1[2] += vsw1
    d1[1] += gnd
    c_vdd1 = cap("TBD 33u", src(source="NCN5130 datasheet DC-DC1 output, review"))
    c_vdd1[1] += v3t
    c_vdd1[2] += gnd

    # DC-DC2 buck: VSW2 -> L2 -> +12V_STORE, feedback divider -> VDD2MV
    block("dcdc2")
    l2 = part("Device", "L", "TBD 68uH", L1210,
              src(source="NCN5130 datasheet DC-DC2, review"))
    l2[1] += vsw2
    l2[2] += store
    d2 = d_schottky("TBD 60V 1A", fields=src(source="DC-DC2 catch diode, review"))
    d2[2] += vsw2
    d2[1] += gnd
    r_fb2t = res("TBD", src(source="DC-DC2 ~12V feedback top, review"))
    r_fb2b = res("TBD", src(source="DC-DC2 ~12V feedback bottom, review"))
    r_fb2t[1] += store
    r_fb2t[2] += fb2
    r_fb2b[1] += fb2
    r_fb2b[2] += gnd

    # storage-rail bulk capacitance (~1000 uF per architecture.md)
    c_bulk = cap("1000u 25V", src(source="architecture.md storage rail, review"),
                 footprint=C_ELEC)
    c_bulk[1] += store
    c_bulk[2] += gnd
    c_st100 = cap("100n")
    c_st100[1] += store
    c_st100[2] += gnd

    # =======================================================================
    # U2 buck +12V_STORE -> +3V3_MCU (>=500 mA; Diodes AP63203WU-7: 32 V in,
    # 3.3 V fixed, 2 A — datasheet typical application)
    # =======================================================================
    block("buck3v3")
    u2 = part("Regulator_Switching", "AP63205WU", "AP63203WU-7", SOT23_6,
              src(mpn="AP63203WU-7", mfr="Diodes Inc.", lcsc="TBD",
                  source="AP63200/03/05 datasheet typical application"))
    u2["IN"] += store
    r_en = res("100k")
    r_en[1] += store
    u2["EN"] += r_en[2]
    c_bst = cap("100n")
    c_bst[1] += u2["BST"]
    c_bst[2] += u2["SW"]
    l3 = part("Device", "L", "TBD 4.7uH", L1210,
              src(mpn="TBD", source="AP63203 datasheet L value, review"))
    l3[1] += u2["SW"]
    l3[2] += v3m
    u2["FB"] += v3m             # fixed-output version: FB senses VOUT
    u2["GND"] += gnd
    for val in ("10u", "100n"):
        c = cap(val, src(source="AP63203 datasheet Cin, review"))
        c[1] += store
        c[2] += gnd
    for val in ("22u", "22u", "100n"):
        c = cap(val, src(source="AP63203 datasheet Cout, review"))
        c[1] += v3m
        c[2] += gnd

    # =======================================================================
    # DALI interface — bridge rectifier + opto-isolated TX/RX
    # (classic DALI bus interface topology, e.g. Microchip AN1465; all
    # values draft/TBD).
    # =======================================================================
    block("dali_terminal")
    j2 = part("Connector", "Screw_Terminal_01x02", "DALI", TERM_5MM,
              src(mpn="TBD 5.0mm 2-pole", source="spec.yaml terminals.dali"))
    j2[1] += da1
    j2[2] += da2

    block("dali_bridge")
    d3 = part("Diode_Bridge", "DF06S", "TBD 600V 1A bridge", D_BRIDGE,
              src(mpn="TBD DF06S-class", source="DALI rectifier, review"))
    d3[3] += da1                # AC in
    d3[4] += da2                # AC in
    d3["+"] += da_p
    d3["-"] += gndd

    # --- TX path: logic opto -> gate drive -> current-limited sink ---------
    block("dali_tx")
    u3 = part("Isolator", "LTV-352T", "TBD opto", SOP4,
              src(mpn="TBD LTV-352T-class", source="DALI TX isolation, review"))
    r_led_tx = res("1k", src(source="DALI TX opto LED resistor, review"))
    r_led_tx[1] += dali_tx
    u3[1] += r_led_tx[2]        # LED anode
    u3[2] += gnd                # LED cathode
    r_tx_pd = res("10k", src(source="MCU-off defined state, review"))
    r_tx_pd[1] += dali_tx
    r_tx_pd[2] += gnd

    gate = Net("DALI_GATE")
    r_gate_up = res("TBD 47k", src(source="DALI TX gate bias, review"))
    r_gate_up[1] += da_p
    r_gate_up[2] += u3[4]       # opto collector
    u3[3] += gate               # opto emitter: LED on -> gate pulled high
    dz_gate = part("Device", "D_Zener", "TBD 12V", D_SOD123,
                   src(mpn="TBD", source="gate clamp, review"))
    dz_gate["K"] += gate
    dz_gate["A"] += gndd
    r_gate_pd = res("TBD 47k", src(source="gate pull-down, review"))
    r_gate_pd[1] += gate
    r_gate_pd[2] += gndd

    q1 = part("Transistor_FET", "2N7002", "TBD >=60V >=0.5A NMOS", SOT23,
              src(mpn="TBD e.g. BSS806N/IRLML0060", source="DALI bus sink FET, review"))
    q1["G"] += gate
    q1["D"] += da_p
    sense = Net("DALI_SENSE")
    q1["S"] += sense
    r_sense = res("2R4 0.5W", src(source="I_lim ~0.65V/2.4R ~ 270mA, review"),
                  footprint=R1206)
    r_sense[1] += sense
    r_sense[2] += gndd
    q2 = part("Transistor_BJT", "BC847", "TBD NPN", SOT23,
              src(mpn="TBD", source="current-limit clamp, review"))
    q2["B"] += sense
    q2["E"] += gndd
    q2["C"] += gate

    # --- RX path: zener threshold + opto LED from bus ------------------------
    block("dali_rx")
    u4 = part("Isolator", "LTV-352T", "TBD opto", SOP4,
              src(mpn="TBD", source="DALI RX isolation, review"))
    r_rx = res("TBD 10k", src(source="DALI RX threshold current set, review"))
    r_rx[1] += da_p
    dz_rx = part("Device", "D_Zener", "TBD 8V2", D_SOD123,
                 src(mpn="TBD", source="RX threshold vs DALI 9.5V high spec, review"))
    dz_rx["K"] += r_rx[2]
    dz_rx["A"] += u4[1]
    u4[2] += gndd
    u4[4] += dali_rx            # collector
    r_rx_pu = res("10k")
    r_rx_pu[1] += v3m
    r_rx_pu[2] += dali_rx
    u4[3] += gnd                # emitter -> logic ground (isolated output)

    # =======================================================================
    # Auxiliary input -> isolated DC-DC -> LM2574 buck (~20.5 V) ->
    # LM317 current limiter (250 mA) -> slide switch -> DA1
    # =======================================================================
    block("aux_in")
    j4 = part("Connector", "Screw_Terminal_01x02", "AUX", TERM_5MM,
              src(mpn="TBD 5.0mm 2-pole", source="spec.yaml terminals.aux_dc"))
    f1 = part("Device", "Polyfuse", "TBD 250mA", FUSE1812,
              src(mpn="TBD", source="aux input fuse, review"))
    j4[1] += f1[1]
    f1[2] += aux_fused
    j4[2] += gnda
    d_aux_rev = d_schottky("TBD 60V 1A", D_SMA,
                           src(source="aux reverse polarity, review"))
    d_aux_rev[1] += aux_fused
    d_aux_rev[2] += aux_p
    d_aux_tvs = part("Device", "D_TVS", "TBD ~36V", D_SMB,
                     src(source="aux surge, review"))
    d_aux_tvs[1] += gnda
    d_aux_tvs[2] += aux_p
    c_aux_in = cap("TBD 10u", src(source="isolated DC-DC input cap, review"))
    c_aux_in[1] += aux_p
    c_aux_in[2] += gnda

    block("dcdc_iso")
    u5 = part("gateway", "DCDC_ISO_4P", "TDN5-2425WISM 24V->24V 5W iso", HDR_DCDC,
              src(mpn="TDN 5-2425WISM", mfr="TRACO",
                  source="TRACO TDN 5 WI datasheet pinout, review"))
    u5["+Vin"] += aux_p
    u5["-Vin"] += gnda
    u5["Remote"] += NC      # remote on/off: open = on (TDN5WI), review
    u5["+Vo"] += iso24
    u5["-Vo"] += gndd
    u5["Trim"] += NC        # fixed 24 V output
    c_iso_out = cap("TBD 10u", src(source="isolated DC-DC output cap, review"))
    c_iso_out[1] += iso24
    c_iso_out[2] += gndd

    # adjustable buck 24V -> ~20.5V (headroom for the LM317 limiter drop)
    block("dali_buck")
    u6 = part("Regulator_Switching", "LM2574HVM-ADJ", "TBD LM2574HVM-ADJ", SOIC14,
              src(mpn="TBD LM2574HVM-ADJ-class",
                  source="LM2574HV datasheet typical buck, review"))
    u6["VIN"] += iso24
    u6["~{ON}/OFF"] += gndd
    u6["SGND"] += gndd
    u6["PGND"] += gndd
    l4 = part("Device", "L", "TBD 330uH", L1210,
              src(source="LM2574 datasheet inductor, review"))
    l4[1] += u6["OUT"]
    l4[2] += v20
    d6 = d_schottky("TBD 60V 1A", D_SMA,
                    src(source="LM2574 catch diode, review"))
    d6[1] += gndd
    d6[2] += u6["OUT"]
    fb6 = Net("DALI_FB6")
    r_fb6t = res("TBD", src(source="~20.5V set divider, review"))
    r_fb6b = res("TBD", src(source="~20.5V set divider, review"))
    r_fb6t[1] += v20
    r_fb6t[2] += fb6
    r_fb6b[1] += fb6
    r_fb6b[2] += gndd
    u6["FB"] += fb6
    for net_, val in ((iso24, "TBD 10u"), (v20, "TBD 100u")):
        c = cap(val, src(source="LM2574 datasheet caps, review"))
        c[1] += net_
        c[2] += gndd

    # LM317 current regulator: 1.25V / 5R = 250 mA limit (datasheet standard
    # current-limiter circuit), feeds the bus via the disable switch.
    block("dali_limit")
    u7 = part("Regulator_Linear", "LM317_SOT-223", "TBD LM317", SOT223,
              src(mpn="TBD LM317", source="LM317 datasheet current regulator, review"))
    u7["VI"] += v20
    r_lim = res("5R 1W", src(source="250mA limit, review"), footprint=R2512)
    u7["VO"] += r_lim[1]
    r_lim[2] += sw_in
    u7["ADJ"] += sw_in

    block("dali_switch")
    sw1 = part("Switch", "SW_SPDT", "TBD slide SPDT", SW_SLIDE,
               src(mpn="TBD slide switch", source="spec.yaml dali_supply_switch"))
    sw1["A"] += sw_in
    sw1["B"] += da1
    sw1["C"] += NC              # second throw unconnected (on-off function)

    # =======================================================================
    # interconnect: 10-pin 1.0 mm JST-SH cable to the upper board
    # =======================================================================
    block("interconnect")
    j3 = part("Connector", "Conn_01x10_Pin", "JST-SH 1x10", JST_SH10,
              src(mpn="SM10B-SRSS-TB", mfr="JST", lcsc="C160404",
                  source="spec.yaml boards.interconnect"))
    pins = [v3m, gnd, knx_txd, knx_rxd, gnd, dali_tx, dali_rx, gnd,
            spare1, spare2]
    for i, net_ in enumerate(pins, start=1):
        j3[i] += net_

    # Mounting holes are added directly on the PCB by the pcbnew placement
    # script (pin-less symbols crash SKiDL's schematic placer).


NET_CLASSES = {
    "SELV": ["GND", "+3V3_TRX", "+3V3_MCU", "+12V_STORE", "KNX_P",
             "KNX_P_PROT", "KNX_TXD", "KNX_RXD", "DALI_TX", "DALI_RX",
             "INT_SPARE1", "INT_SPARE2", "VFILT", "V20V", "CCP", "CCP_B",
             "CAV", "CEQ1", "CEQ2", "VSW1", "VSW2", "VDD2MV", "Net-*"],
    "DALI": ["GNDD", "DA1", "DA2", "DA_P", "DALI_ISO24V", "DALI_20V",
             "DALI_SUPPLY_SW", "DALI_GATE", "DALI_SENSE", "DALI_FB6"],
    "AUX": ["GNDA", "AUX_P", "AUX_FUSED"],
}
