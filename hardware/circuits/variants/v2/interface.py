from blocks import dali
from common import block, part, power, power_flag, src
from skidl import Net

HEADER_1X3 = "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical"
HEADER_1X4 = "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical"


def build():
    gnd = power("GND")
    gndd = power("GNDD")
    v3, da1, da2, da_p = (Net(name) for name in ("+3V3", "DA1", "DA2", "DA_P"))
    tx, rx = Net("DALI_TX"), Net("DALI_RX")
    for net in (gnd, gndd, v3, da1, da2, da_p):
        power_flag(net)
    dali.build_interface(gnd, v3, gndd, da1, da2, da_p, tx, rx)
    block("int_logic")
    header = part("Connector", "Conn_01x04_Pin", "Main logic", HEADER_1X4,
                  src(mpn="KH-2.54PH180-1X4P-L11.5", mfr="Kinghelm", lcsc="C2905435",
                      source="on the back, plugs into main J6 (2.5 mm plastic, 6 mm mating pins)"), ref="J1")
    for i, net in enumerate([v3, gnd, tx, rx], start=1):
        header[i] += net
    block("int_bus")
    header = part("Connector", "Conn_01x03_Pin", "Main DALI", HEADER_1X3,
                  src(mpn="2.54-1*3P", mfr="BOOMELE", lcsc="C49257",
                      source="on the back, plugs into main J5 (2.5 mm plastic, 6 mm mating pins)"), ref="J2")
    for i, net in enumerate([da1, da2, gndd], start=1):
        header[i] += net


NET_CLASSES = {"SELV-KNX": ["GND", "+3V3", "DALI_TX", "DALI_RX"], "DALI": ["GNDD", "DA1", "DA2", "DA_P"]}
