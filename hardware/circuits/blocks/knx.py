"""KNX block: bus terminal + surge + reverse polarity + NCN5130 transceiver
+ DC-DC1 (3V3 transceiver) + 5V buck (>=40V input) for the MCU.

Per NCN5130 datasheet Figure 12 (9-bit UART 19200 bps, single supply,
external FANIN). DC2 and V20V are unused per the lead's power-architecture
decision (storage on VFILT).

Returns the nets the rest of the system needs:
  knx_txd, knx_rxd, saveb, v5_buck (buck output), v5 (core 5 V rail, fed by
  blocks/core_power.py from v5_buck and the AUX converter), gnd
"""

from builtins import NC

from common import block, part, power_flag, src
from skidl import Net

R0603 = "Resistor_SMD:R_0603_1608Metric"
R2512 = "Resistor_SMD:R_2512_6332Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
C_ELEC = "Capacitor_THT:CP_Radial_D10.0mm_P5.00mm"
C_ELEC_BENT = "gateway_fp:CP_Radial_D10.0mm_P5.00mm_Horizontal_Bent"
D_SMA = "Diode_SMD:D_SMA"
D_SOD123 = "Diode_SMD:D_SOD-123"
TSOT23_6 = "Package_TO_SOT_SMD:TSOT-23-6"
QFN40 = "Package_DFN_QFN:QFN-40-1EP_6x6mm_P0.5mm_EP4.6x4.6mm_ThermalVias"
QFN40_NO_VIAS = "Package_DFN_QFN:QFN-40-1EP_6x6mm_P0.5mm_EP4.6x4.6mm"
XTAL3225 = "Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm"
L_INDUCTOR = "Inductor_SMD:L_Wuerth_7440604_8x8mm"
L_0805 = "Inductor_SMD:L_0805_2012Metric"
HDR_KNX = "gateway_fp:Phoenix_PST_1,0-2-5,75_R32_1x02_P5.75mm"
C_ELEC_SMD = "Capacitor_SMD:CP_Elec_10x10"


def knx_pin_strip(value, ref):
    """Phoenix PST 1,0/2-5,75 pin strip for a KNX-style plug (red/black bus or
    yellow/white auxiliary voltage)."""
    return part("Connector", "Conn_01x02_Pin", value, HDR_KNX,
                src(mpn="1574200", mfr="Phoenix Contact", hand=True,
                    source="Phoenix PST 1,0/2-5,75 R32; Mouser 651-1574200"), ref=ref)


def res(value, fields=None, footprint=R0603):
    return part("Device", "R", value, footprint, fields)


def cap(value, fields=None, footprint=C0603):
    return part("Device", "C", value, footprint, fields)


def input_protection(gnd, knx_p, knx_p_prot):
    """NCN5130 Figure 12 bus input: TVS across the bus, then the reverse-polarity diode."""
    d_tvs = part("Device", "D_TVS", "SMAJ40CA", D_SMA,
                 src(mpn="SMAJ40CA", mfr="MDD", lcsc="C113970",
                     source="NCN5130 Table 6: D2 TVS; SMAJ40CA equiv to 1SMA40CA; "
                            "40VWM 64.5VC bidirectional SMA"))
    d_tvs[1] += gnd
    d_tvs[2] += knx_p
    d_rev = part("Device", "D_Schottky", "SS16", D_SMA,
                 src(mpn="SS16", mfr="MDD", lcsc="C2481",
                     source="NCN5130 Table 6: D1 reverse polarity SS16; "
                            "anode at KNX+ per Figure 12"))
    d_rev["A"] += knx_p
    d_rev["K"] += knx_p_prot


def build(gnd, *, horizontal_storage=False, smd_storage=False, terminal=True, input_stage=True, buck=True,
          thermal_vias=True):
    """Build the KNX block. Returns dict with knx_p, knx_p_prot, knx_txd, knx_rxd, saveb, v5.
    terminal=False leaves the KNX pin strip to another board; input_stage=False
    also leaves the bus protection there (KNX_P_PROT arrives on a header);
    buck=False leaves the 5 V buck to another board (VFILT leaves on a header);
    thermal_vias=False keeps the exposed pad's vias out of the other side."""
    knx_p = Net("KNX_P")
    knx_p_prot = Net("KNX_P_PROT")
    v3t = Net("+3V3_TRX")
    vfilt = Net("VFILT")
    v5 = Net("+5V")
    v5_buck = Net("+5V_BUCK")
    knx_txd = Net("KNX_TXD")
    knx_rxd = Net("KNX_RXD")
    saveb = Net("NCN_SAVEB")
    for n in (knx_p, knx_p_prot, v5, v5_buck):
        power_flag(n)

    # --- KNX bus terminal + TVS + reverse polarity (Figure 12) ---
    block("knx_in")
    if terminal:
        j1 = knx_pin_strip("KNX bus terminal pins", "J1")
        j1[1] += knx_p
        j1[2] += gnd
    if input_stage:
        input_protection(gnd, knx_p, knx_p_prot)

    # --- U1 NCN5130 (Figure 12, 9-bit UART 19200 bps) ---
    block("ncn5130")
    u1 = part("gateway", "NCN5130", "NCN5130MNTWG", QFN40 if thermal_vias else QFN40_NO_VIAS,
              src(mpn="NCN5130MNTWG", mfr="onsemi", lcsc="C513000",
                  source="onsemi NCN5130 datasheet Figure 12"))
    ccp = Net("CCP")
    cav = Net("CAV")
    ceq1 = Net("CEQ1")
    ceq2 = Net("CEQ2")
    vsw1 = Net("VSW1")
    vdd1m = Net("VDD1M")
    fanin = Net("FANIN")
    xtal1 = Net("XTAL1")
    xtal2 = Net("XTAL2")
    sw = Net("SW")

    u1["VBUS1"] += knx_p_prot
    u1["VBUS2"] += gnd
    u1["VSSA"] += gnd
    u1["VSS1"] += gnd
    u1["VSS2"] += gnd
    u1["VSSD"] += gnd
    u1["EPAD"] += gnd

    u1["VFILT"] += vfilt
    u1["V20V"] += vfilt
    u1["VIN"] += vfilt

    u1["CCP"] += ccp
    u1["CAV"] += cav
    u1["CEQ1"] += ceq1
    u1["CEQ2"] += ceq2

    u1["VSW1"] += vsw1
    u1["VDD1M"] += vdd1m
    u1["VDD1"] += v3t
    u1["VDDA"] += v3t
    u1["VDDD"] += v3t

    u1["VDD2MV"] += v3t
    u1["VDD2MC"] += NC
    u1["VDD2"] += NC
    u1["VSW2"] += NC

    for pin in ("MODE1", "MODE2", "TREQ", "CSB/UC1", "SCK/UC2",
                "XSEL", "XCLKC"):
        u1[pin] += gnd

    u1["SDO/TXD"] += knx_txd
    u1["SAVEB"] += saveb
    u1["RESETB"] += NC

    for pin in ("TRIG", "XCLK", "ANAOUT"):
        u1[pin] += NC

    r_fan = res("10k 1%", src(source="NCN5130 Table 6 R6 FANIN 10k 1% = 40mA"))
    r_fan.fields["Place"] = "ncn_right"
    u1["FANIN"] += fanin
    fanin += r_fan[1]
    r_fan[2] += gnd

    r_saveb = res("10k", src(source="NCN5130 SAVEB pull-up (open-collector)"))
    r_saveb.fields["Place"] = "ncn_right"
    r_saveb[1] += v3t
    r_saveb[2] += saveb

    # --- R1: KNX transmit shunt (Table 6, 27Ω 1W, KNX_P_PROT to TX0) ---
    tx0 = Net("TX0")
    r_tx = res("27R",
               src(source="NCN5130 Table 6 R1 transmit shunt 27Ω 1W"),
               footprint=R2512)
    r_tx.fields["Place"] = "ncn_left"
    r_tx[1] += knx_p_prot
    r_tx[2] += tx0
    u1["TX0"] += tx0

    # --- C1: CCP AC coupling (Table 6, 47nF/50V, KNX_P_PROT to CCP) ---
    c_ccp = cap("47n 50V", src(source="NCN5130 Table 6 C1 47nF 50V"))
    c_ccp.fields["Place"] = "ncn_left"
    c_ccp[1] += knx_p_prot
    c_ccp[2] += ccp

    # --- C3: CAV bus DC averaging (Table 6, 100nF/50V) ---
    c_cav = cap("100n 50V", src(source="NCN5130 Table 6 C3 100nF 50V"))
    c_cav.fields["Place"] = "ncn_left"
    c_cav[1] += cav
    c_cav[2] += gnd

    # --- C2: Equalization (Table 6, 220nF/50V, CEQ1 to CEQ2) ---
    c_ceq = cap("220n 50V", src(source="NCN5130 Table 6 C2 220nF 50V"))
    c_ceq.fields["Place"] = "ncn_left"
    c_ceq[1] += ceq1
    c_ceq[2] += ceq2

    # --- C4: VFILT storage (Table 6, 470µF/35V) ---
    storage_source = "NCN5130 Table 6 C4 470µF 35V storage; D10 L16 P5"
    if horizontal_storage:
        storage_source += ("; hand-fit lying down from the back: bend both leads 90 deg 3 mm from the body, "
                           "body axis 7 mm below the board (2 mm air gap) in the base cradles, solder on the front")
    if smd_storage:
        c_vfilt = part("Device", "C_Polarized", "470u 35V", C_ELEC_SMD,
                       src(mpn="RYRVT35V470UF10*10", mfr="Honor Elec", lcsc="C48970703",
                           source="NCN5130 Table 6 C4 470µF 35V storage; SMD D10 x 10.2 mm for the 1-module "
                                  "case (286 mA ripple at 120 Hz, 105 C 2000 h)"))
    else:
        c_vfilt = part("Device", "C_Polarized", "470u 35V", C_ELEC_BENT if horizontal_storage else C_ELEC,
                       src(mpn="KMG35V470M10*16 LO", mfr="SamYoung", lcsc="C165500", hand=horizontal_storage,
                           source=storage_source))
    c_vfilt.fields["Place"] = "ncn_left"
    c_vfilt[1] += vfilt
    c_vfilt[2] += gnd

    # --- C5: VDDA HF rejection (Table 6, 100nF) ---
    c_vdda = cap("100n 50V", src(source="NCN5130 Table 6 C5 VDDA 100nF"))
    c_vdda.fields["Place"] = "ncn_right"
    c_vdda[1] += v3t
    c_vdda[2] += gnd

    # --- C6: VDDD HF rejection (Table 6, 100nF) ---
    c_vddd = cap("100n 50V", src(source="NCN5130 Table 6 C6 VDDD 100nF"))
    c_vddd.fields["Place"] = "ncn_right"
    c_vddd[1] += v3t
    c_vddd[2] += gnd

    # --- C10: VDD1 load cap (Table 6 min 8µF effective; 22µF/10V 0805 X5R) ---
    c_vdd1 = cap("22u 25V",
                 src(source="NCN5130 Table 6 C10 min 8µF eff; 22µF/10V 0805 X5R "
                            "retains ~12µF at 3.3V DC bias; ESR<0.1Ω"),
                 footprint=C0805)
    c_vdd1.fields["Place"] = "ncn_bottom"
    c_vdd1[1] += v3t
    c_vdd1[2] += gnd

    # --- R2: DC1 sense resistor (Table 6, 1Ω between VDD1M and VDD1) ---
    r_sense_dc1 = res("1R", src(source="NCN5130 Table 6 R2 DC1 sense 1Ω"))
    r_sense_dc1.fields["Place"] = "ncn_bottom"
    r_sense_dc1[1] += vdd1m
    r_sense_dc1[2] += v3t

    # --- L1: DC1 inductor (Table 6, 220µH) ---
    l1 = part("Device", "L", "220uH", "Inductor_SMD:L_Sunlord_SWPA6045S",
              src(mpn="SWPA6045S221MT", mfr="Sunlord", lcsc="C91047",
                  source="NCN5130 Table 6 L1 220µH; shielded 6x6, I_sat 590 mA"))
    l1.fields["Place"] = "ncn_bottom"
    l1[1] += vsw1
    l1[2] += vdd1m

    # --- X1: 16 MHz crystal (Table 6, mandatory, ≤50 ppm) ---
    x1 = part("Device", "Crystal_GND24", "16MHz", XTAL3225,
              src(mpn="X322516MOB4SI", mfr="YXC", lcsc="C12668",
                  source="NCN5130 Table 6 X1 16MHz ≤50ppm; YXC X322516MOB4SI"))
    x1.fields["Place"] = "ncn_top"
    x1[1] += xtal1
    xtal1 += u1["XTAL1"]
    x1[3] += xtal2
    xtal2 += u1["XTAL2"]
    x1[2] += gnd
    x1[4] += gnd

    # --- C8, C9: Crystal load capacitors (C = 2*(CL - C_stray) = 2*(12-3) = 18pF) ---
    c8 = cap("18p C0G 50V", src(source="Crystal CL=12pF, C_stray≈3pF → C=2*(12-3)=18pF"))
    c8.fields["Place"] = "ncn_top"
    c8[1] += xtal1
    c8[2] += gnd
    c9 = cap("18p C0G 50V", src(source="Crystal CL=12pF, C_stray≈3pF → C=2*(12-3)=18pF"))
    c9.fields["Place"] = "ncn_top"
    c9[1] += xtal2
    c9[2] += gnd

    if buck:
        buck_5v(gnd, vfilt, v5_buck, sw)

    # --- KNX_RXD series resistor (prevents USB-powered XIAO back-powering NCN) ---
    r_knx_rxd = res("1k", src(source="Series R in KNX_RXD near NCN; "
                                "isolates USB-powered XIAO from unpowered NCN I/O"))
    r_knx_rxd.fields["Place"] = "ncn_right"
    r_knx_rxd[1] += knx_rxd
    r_knx_rxd[2] += u1["SDI/RXD"]

    block("ncn5130")
    c_vin = cap("1u 50V",
                src(mpn="CL21B105KBFNNNE", mfr="Samsung", lcsc="C28323",
                    source="DC1 switching-loop decoupling at VIN/VSS1; "
                           "storage C4 sits off-chip"),
                footprint=C0805)
    c_vin.fields["Place"] = "ncn_bottom"
    c_vin[1] += vfilt
    c_vin[2] += gnd

    return {"knx_p": knx_p, "knx_p_prot": knx_p_prot, "knx_txd": knx_txd, "knx_rxd": knx_rxd, "saveb": saveb,
            "v5": v5, "v5_buck": v5_buck, "vfilt": vfilt}


NET_CLASSES = {
    "SELV-KNX": ["GND", "+3V3_TRX", "+5V", "KNX_P", "KNX_P_PROT", "KNX_TXD",
                 "KNX_RXD", "NCN_SAVEB", "VFILT", "CCP", "CAV", "CEQ1",
                 "CEQ2", "VSW1", "VDD1M", "+5V_BUCK", "+5V_AUX", "Net-*"],
    "KNX-Power": ["KNX_P", "KNX_P_PROT", "VFILT"],
}


def buck_5v(gnd, vfilt, v5_buck, sw):
    """SCT2400 5 V buck from VFILT, with its EN undervoltage divider."""
    # --- 5V buck: VFILT -> +5V (SCT2400, >=40V input) ---
    block("buck5v")
    u2 = part("gateway", "SCT2400", "SCT2400TVBR", TSOT23_6,
              src(mpn="SCT2400TVBR", mfr="SCT", lcsc="C2693819",
                  source="SCT2400 datasheet; 4.5-40V in, 600mA, 2MHz, 0.81V FB"))
    u2["VIN"] += vfilt
    u2["GND"] += gnd
    r_en_top = res("100k 1%", src(source="SCT2400 EN UVLO top (EN 1.21/1.4V rising, 0.9V falling min, 480k "
                                         "internal pull-down): on 13.5V typ / 15.7V max, below VFILT at a 21V bus; "
                                         "off >= 10.1V, above the NCN5130 VFILT reset"))
    r_en_top.fields["Place"] = "buck5v"
    r_en_top[1] += vfilt
    r_en_top[2] += u2["EN"]
    r_en_bot = res("10k 1%", src(source="SCT2400 EN UVLO bottom"))
    r_en_bot.fields["Place"] = "buck5v"
    r_en_bot[1] += u2["EN"]
    r_en_bot[2] += gnd
    c_bst = cap("100n 50V", src(source="SCT2400 datasheet BST cap"))
    c_bst.fields["Place"] = "buck5v"
    c_bst[1] += u2["BST"]
    c_bst[2] += sw
    sw += u2["SW"]
    # L3: 4.7uH shielded, I_sat >= 1.2A, 4x4mm (Sunna SMNR4012-4R7MT)
    l3 = part("Device", "L", "4.7uH", "Inductor_SMD:L_APV_ANR4012",
              src(mpn="SMNR4012-4R7MT", mfr="Sunna", lcsc="C135248",
                  source="SCT2400 datasheet; 4.7uH 1.15A sat shielded 4x4mm"))
    l3.fields["Place"] = "buck5v"
    l3[1] += sw
    l3[2] += v5_buck
    r_fb_top = res("51k 1%", src(source="SCT2400 FB divider: VOUT=0.81*(1+51k/10k)=4.94V"))
    r_fb_top.fields["Place"] = "buck5v"
    r_fb_top[1] += v5_buck
    r_fb_top[2] += u2["FB"]
    r_fb_bot = res("10k 1%", src(source="SCT2400 FB divider bottom"))
    r_fb_bot.fields["Place"] = "buck5v"
    r_fb_bot[1] += u2["FB"]
    r_fb_bot[2] += gnd
    c_cin = cap("10u 50V",
                src(source="SCT2400 datasheet Cin; VFILT up to 30V+"),
                footprint=C0805)
    c_cin.fields["Place"] = "buck5v"
    c_cin[1] += vfilt
    c_cin[2] += gnd
    for val in ("22u 25V", "22u 25V", "100n 50V"):
        c = cap(val,
                src(source="SCT2400 datasheet Cout"),
                footprint=C0805 if "22u" in val else C0603)
        c.fields["Place"] = "buck5v"
        c[1] += v5_buck
        c[2] += gnd
