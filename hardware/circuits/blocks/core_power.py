"""Core 5 V rail: the protected AUX input carries the gateway core through an
isolated TRACO converter (18-36 V in, 5.00 V +/-1 %, functional isolation
between SELV-AUX and SELV-KNX): the 2 W TMR 2411 (400 mA) and the 3 W TRN 3-2411
(600 mA) carry the WiFi TX peaks on their own (a 1 W converter folds back
under them). A TI TPS2121 priority power mux selects it (IN1) whenever its PR1
divider sees more than the 1.10 V reference, i.e. above 4.4 V, and switches to
the KNX buck (IN2)
when AUX goes away (up to 100 us with CP2 grounded); both inputs are reverse
blocked, so the two 5 V sources never feed each other whatever their
tolerances."""

from builtins import NC

from common import block, part, src
from skidl import Net

SOT23 = "Package_TO_SOT_SMD:SOT-23"
SOD123 = "Diode_SMD:D_SOD-123"
R_SMD = "Resistor_SMD:R_0805_2012Metric"   # hand-solderable size (user, 2026-11-11)
C_SMD = "Capacitor_SMD:C_0805_2012Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
C1206 = "Capacitor_SMD:C_1206_3216Metric"

TRN3_2411 = {
    "symbol": "DCDC_SIP5", "value": "TRN 3-2411 24V->5V 3W iso", "series": "TRN 3",
    "footprint": "gateway_fp:Converter_DCDC_TRACO_TRN3_SIP5", "mpn": "TRN 3-2411",
    "source": "TRACO TRN 3 datasheet: 18-36 V in, 5 V 600 mA regulated (+/-1 %), 1.6 kVDC functional "
              "isolation, no minimum load, current limit 180 % typ, 2200 uF max capacitive load; "
              "SIP 11.9 x 7.7 x 11.0",
}
TMR2_2411 = {
    "symbol": "DCDC_SIP8_REMOTE", "value": "TMR 2411 24V->5V 2W iso", "series": "TMR 2",
    "footprint": "gateway_fp:Converter_DCDC_TRACO_TMR2_SIP8", "mpn": "TMR 2411",
    "source": "TRACO TMR 2 datasheet: 18-36 V in, 5 V 400 mA regulated (+/-1 %), 1.6 kVDC functional "
              "isolation, no minimum load, continuous short-circuit protection, 1000 uF max capacitive load, "
              "remote open = on; SIP-8 21.8 x 9.1 x 11.2",
}


def res(value, source, ref):
    return part("Device", "R", value, R_SMD, src(source=source), ref=ref)


def cap(value, source, ref, footprint=C_SMD):
    return part("Device", "C", value, footprint, src(source=source), ref=ref)


def build(gnd, gnda, aux_p, v5_buck, v5, converter):
    """The core rail v5 from the isolated AUX converter (priority) and the KNX
    buck. Built last in each board with fixed references so the rest keeps
    its numbering."""
    block("aux_core")
    v5_aux = Net("+5V_AUX")
    c_in = cap("10u 50V", f"{converter['series']} input decoupling next to pins 1/2 (AUX up to the 53 V TVS clamp)",
               "C20", C0805)
    c_in[1] += aux_p
    c_in[2] += gnda
    u = part("gateway", converter["symbol"], converter["value"], converter["footprint"],
             src(mpn=converter["mpn"], mfr="TRACO Power", hand=True, source=converter["source"]), ref="U9")
    u["+Vin"] += aux_p
    u["-Vin"] += gnda
    u["+Vout"] += v5_aux
    u["-Vout"] += gnd
    if converter is TMR2_2411:
        u["Remote"] += NC
    c_out = cap("10u 25V", f"{converter['series']} output decoupling", "C21")
    c_out[1] += v5_aux
    c_out[2] += gnd

    block("core_mux")
    gate, good = Net("CORE_PFET_G"), Net("CORE_AUX_GOOD")
    sup = part("Power_Supervisor", "LM809", "TLV803M supervisor", SOT23,
               src(mpn="TLV803MDBZR-TP", mfr="MCC", lcsc="C52817175",
                   source="TLV803M: 4.38 V supervisor, open-drain active-low RESET (1 GND, 2 RESET, 3 VCC), "
                          "~240 ms delay after VCC rises, releases low within us when VCC falls"),
               ref="U10")
    sup["VCC"] += v5_aux
    sup["GND"] += gnd
    sup["~{RESET}"] += good
    r_good = res("100k", "TLV803M open-drain pull-up: AUX good -> Q10 on", "R24")
    r_good[1] += v5_aux
    r_good[2] += good
    q_n = part("Transistor_FET", "2N7002", "2N7002", SOT23,
               src(mpn="2N7002", mfr="Changjiang", lcsc="C8545", source="pulls Q9's gate low while AUX is good"),
               ref="Q10")
    q_n["G"] += good
    q_n["S"] += gnd
    q_n["D"] += gate
    r_gate = res("10k", "Q9 gate pull-up to the rail: off (~10 us) as soon as Q10 lets go", "R25")
    r_gate[1] += v5
    r_gate[2] += gate
    q_p = part("Transistor_FET", "AO3401A", "AO3401A", SOT23,
               src(mpn="AO3401A", mfr="AOS", lcsc="C15127",
                   source="AUX -> rail switch: drain on +5V_AUX, source on the rail (body diode AUX -> rail), "
                          "60 mOhm at Vgs -4.5 V"),
               ref="Q9")
    q_p["D"] += v5_aux
    q_p["S"] += v5
    q_p["G"] += gate
    d_knx = part("Device", "D_Schottky", "B5819W", SOD123,
                 src(mpn="B5819W SL", mfr="Changjiang", lcsc="C8598",
                     source="KNX buck -> rail OR diode, 40 V 1 A; ~0.3 V at the core load"),
                 ref="D11")
    d_knx["A"] += v5_buck
    d_knx["K"] += v5
    c_core = cap("100u 6.3V", "core rail bulk: WiFi TX peaks and the AUX -> KNX handover",
                 "C23", C1206)
    c_core[1] += v5
    c_core[2] += gnd
