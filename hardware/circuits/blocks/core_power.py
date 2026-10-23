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

VQFN_HR_12 = "Package_DFN_QFN:Texas_VQFN-HR-12_2x2.5mm_P0.5mm"
R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
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
    return part("Device", "R", value, R0603, src(source=source), ref=ref)


def cap(value, source, ref, footprint=C0603):
    return part("Device", "C", value, footprint, src(source=source), ref=ref)


def build(gnd, gnda, aux_p, v5_buck, v5, converter):
    """The core rail v5 from the isolated AUX converter (priority) and the KNX
    buck. Built last in each board with fixed references so the rest keeps
    its numbering."""
    block("aux_core")
    v5_aux = Net("+5V_AUX")
    pr1 = Net("CORE_PR1")
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
    c_out = cap("10u 10V", f"{converter['series']} output decoupling", "C21")
    c_out[1] += v5_aux
    c_out[2] += gnd

    block("core_mux")
    mux = part("gateway", "TPS2121", "TPS2121RUXR", VQFN_HR_12,
               src(mpn="TPS2121RUXR", mfr="TI", lcsc="C485916",
                   source="TI TPS2121 datasheet: priority power mux 2.8-22 V, 56 mOhm, reverse blocking; "
                          "PR1 vs internal 1.04-1.10 V reference, CP2/OV1/OV2 grounded (tSW up to 100 us)"),
               ref="U10")
    mux["IN1"] += v5_aux
    mux["IN2"] += v5_buck
    mux["OUT"] += v5
    mux["PR1"] += pr1
    for pin in ("CP2", "OV1", "OV2", "ST", "GND"):
        mux[pin] += gnd
    r_top = res("30k", "TPS2121 PR1 divider top: 4.95 V AUX -> 1.24 V > 1.10 V rising reference; "
                       "falls back to the KNX buck below 3.96-4.36 V", "R24")
    r_top[1] += v5_aux
    r_top[2] += pr1
    r_bot = res("10k", "TPS2121 PR1 divider bottom", "R25")
    r_bot[1] += pr1
    r_bot[2] += gnd
    r_ilm = res("100k 1%", "TPS2121 ILM: 65.2 / 100^0.861 = 1.24 A current limit", "R26")
    r_ilm[1] += mux["ILM"]
    r_ilm[2] += gnd
    c_ss = cap("100n 50V", "TPS2121 SS: 780 V/s soft start at 5 V (datasheet table 9-1)", "C22")
    c_ss[1] += mux["SS"]
    c_ss[2] += gnd
    c_core = cap("100u 6.3V", "core rail hold-up during the TPS2121 switchover (up to 100 us with CP2 grounded)",
                 "C23", C1206)
    c_core[1] += v5
    c_core[2] += gnd
