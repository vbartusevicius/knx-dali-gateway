from blocks import dali, knx
from common import block, part, power, power_flag, src
from skidl import Net

JST_SH10 = "Connector_JST:JST_SH_BM10B-SRSS-TB_1x10-1MP_P1.00mm_Vertical"
SOCKET_1X3 = "Connector_PinSocket_2.54mm:PinSocket_1x03_P2.54mm_Vertical"
SOCKET_1X4 = "Connector_PinSocket_2.54mm:PinSocket_1x04_P2.54mm_Vertical"
INTERFACE_STACK = ("8.5 mm socket + 2.5 mm header plastic = 11.0 mm board gap; "
                   "the interface board plugs straight down onto main")


def build():
    gnd = power("GND")
    power_flag(gnd)
    v3 = Net("+3V3")
    power_flag(v3)
    dali_tx, dali_rx, supply_off = Net("DALI_TX"), Net("DALI_RX"), Net("DALI_SUPPLY_OFF")
    knx_nets = knx.build(gnd, horizontal_storage=True)
    dali_nets = dali.build(gnd, v3, include_interface=False, supply_off=supply_off)
    block("int_ui")
    connector = part("Connector", "Conn_01x10_Pin", "Controller cable", JST_SH10,
                     src(mpn="BM10B-SRSS-TB(LF)(SN)", mfr="JST", lcsc="C160396",
                         source="V2 main-to-UI/controller, pin 1 to pin 1"), ref="J4")
    pins = [knx_nets["v5"], gnd, v3, knx_nets["knx_txd"], knx_nets["knx_rxd"],
            knx_nets["saveb"], dali_tx, dali_rx, supply_off, gnd]
    for i, net in enumerate(pins, start=1):
        connector[i] += net
    block("int_logic")
    socket = part("Connector", "Conn_01x04_Socket", "Interface logic", SOCKET_1X4,
                  src(mpn="KH-2.54FH-1X4P-H8.5", mfr="Kinghelm", lcsc="C2905415",
                      source=f"SELV side of the interface board (its J1); {INTERFACE_STACK}"), ref="J6")
    for i, net in enumerate([v3, gnd, dali_tx, dali_rx], start=1):
        socket[i] += net
    block("int_bus")
    socket = part("Connector", "Conn_01x03_Socket", "Interface DALI", SOCKET_1X3,
                  src(mpn="KH-2.54FH-1X3P-H8.5", mfr="Kinghelm", lcsc="C2932670",
                      source=f"DALI side of the interface board (its J2); {INTERFACE_STACK}"), ref="J5")
    for i, name in enumerate(("da1", "da2", "gndd"), start=1):
        socket[i] += dali_nets[name]


NET_CLASSES = {**knx.NET_CLASSES, **dali.NET_CLASSES}
