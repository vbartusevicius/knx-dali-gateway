"""V3 DALI board, parallel to the front under the top shoulder: WAGO 2601-1102
lever terminal at the top (wire entry up, lever forward), bridge, TX sink, RX
threshold and the two optocouplers, on both sides. The DALI bus from the
carrier's supply jumpers and logic arrive on two FFCs on the back, 6 mm apart."""

from blocks import dali
from common import block, part, power, power_flag, src
from skidl import Net

from variants.v3.main import DALI_BUS_LINK, DALI_SELV_LINK, ffc, wire

WAGO_2601_1102 = "TerminalBlock_WAGO:TerminalBlock_WAGO_2601-1102_1x02_P3.50mm_Horizontal"


def build():
    gnd = power("GND")
    gndd = power("GNDD")
    names = ("+3V3", "DA1", "DA2", "DA_P", "DALI_TX", "DALI_RX")
    nets = {name: Net(name) for name in names}
    nets.update({"GND": gnd, "GNDD": gndd})
    for name in ("+3V3", "DA1", "DA2", "DA_P"):
        power_flag(nets[name])
    for net in (gnd, gndd):
        power_flag(net)
    dali.build_interface(gnd, nets["+3V3"], gndd, nets["DA1"], nets["DA2"], nets["DA_P"], nets["DALI_TX"],
                         nets["DALI_RX"])
    block("dali_terminal")
    terminal = part("Connector", "Screw_Terminal_01x02", "DALI", WAGO_2601_1102,
                    src(mpn="2601-1102", mfr="WAGO", hand=True,
                        source="WAGO 2601-1102: lever, Push-in CAGE CLAMP, 1.5 mm2, 3.5 mm pitch, 8.5 x 12.75 x "
                               "16.58 mm, horizontal wire entry"), ref="J1")
    terminal[1] += nets["DA1"]
    terminal[2] += nets["DA2"]
    block("int_logic")
    wire(ffc("Carrier logic", "J2", "on the back, FFC to the carrier J2"), DALI_SELV_LINK, nets)
    block("int_bus")
    wire(ffc("Carrier bus", "J3", "on the back, FFC to the carrier J3: two contacts each for DA1, DA2, GNDD"),
         DALI_BUS_LINK, nets)

NET_CLASSES = {"SELV-KNX": ["GND", "+3V3", "DALI_TX", "DALI_RX"],
               "DALI": ["GNDD", "DA1", "DA2", "DA_P", "DALI_GATE", "DALI_GATE_SW", "DALI_SENSE", "DALI_BOOT",
                        "RX_LIM_IN"]}
