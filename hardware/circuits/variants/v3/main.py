"""V3 carrier board (vertical along the left wall of the 1-DIN case, 1.2 mm).

Carries the KNX (red/black, J1) and AUX 20-32 V (yellow/white, J2) pin strips
and their input protections at the bottom shoulder (pins pointing +x to the
right so the plugs push on from the right side and the wires enter from the
bottom), the XIAO ESP32-C6 on 5 mm sockets with the 5 V buck, XIAO power feed
and I2C pull-ups underneath it in the space below the REM3-2415S/A/CTRL, the
isolated CTRL switch-off optocoupler, the DALI current limiter and supply
jumpers at the top, and the four 8-pin 0.5 mm FFC links (J3 -> UI, J4 -> KNX
board, J5 -> DALI logic, J6 -> DALI bus).
"""

from builtins import NC

from blocks import dali, knx, mcu_xiao
from common import block, part, power, power_flag, src
from skidl import Net

FPC_8P = "gateway_fp:FPC_Kinghelm_KH-FPC0.5-H1.2SMT-8P"

UI_LINK = ("+3V3", "GND", "BTN_LADDER", "LED_DATA", "+5V", "I2C_SDA", "I2C_SCL", "GND")
KNX_LINK = ("KNX_P_PROT", "GND", "VFILT", "VFILT", "GND", "KNX_TXD", "KNX_RXD", "NCN_SAVEB")
DALI_SELV_LINK = ("+3V3", "GND", "DALI_TX", "GND", "DALI_RX", "GND", None, None)
DALI_BUS_LINK = ("DA1", "DA1", "DA2", "DA2", "GNDD", "GNDD", None, None)


def ffc(value, ref, source):
    conn = part("Connector", "Conn_01x08_Socket", value, FPC_8P,
                src(mpn="KH-FPC0.5-H1.2SMT-8P-QCHF", mfr="Kinghelm", lcsc="C48532769", source=source), ref=ref)
    return conn


def wire(conn, names, nets):
    for pin, name in enumerate(names, start=1):
        conn[pin] += NC if name is None else nets[name]


def build():
    gnd = power("GND")
    v3, v5 = power("+3V3"), power("+5V")
    knx_p, knx_p_prot, vfilt, sw, knx_txd, knx_rxd, saveb, supply_off = (
        Net(n) for n in ("KNX_P", "KNX_P_PROT", "VFILT", "BUCK5_SW", "KNX_TXD", "KNX_RXD",
                         "NCN_SAVEB", "DALI_SUPPLY_OFF"))
    for net in (gnd, v3, v5, knx_p, knx_p_prot, vfilt):
        power_flag(net)

    block("knx_in")
    bus = knx.knx_pin_strip("KNX bus (red/black)", "J1")
    bus[1] += knx_p
    bus[2] += gnd
    knx.input_protection(gnd, knx_p, knx_p_prot)
    knx.buck_5v(gnd, vfilt, v5, sw)

    dali_tx, dali_rx = Net("DALI_TX"), Net("DALI_RX")
    dali_nets = dali.build(gnd, v3, include_interface=False, terminals=False, input_stage=True,
                           bus_side=True, supply_off=supply_off)
    da1, da2, gndd, aux_in, gnda = (dali_nets[k] for k in ("da1", "da2", "gndd", "aux_in", "gnda"))
    block("aux_in")
    aux = knx.knx_pin_strip("AUX 20-32 V (yellow/white)", "J2")
    aux[1] += aux_in
    aux[2] += gnda
    power_flag(aux_in)
    power_flag(gndd)

    ui = mcu_xiao.build(gnd, v3, v5, knx_txd, knx_rxd, dali_tx, dali_rx, saveb, dali_supply_off=supply_off)
    power_flag(ui["led_dali"])

    nets = {"BTN_LADDER": ui["btn_ladder"], "LED_DATA": ui["led_prog"], "I2C_SDA": ui["sda"], "I2C_SCL": ui["scl"],
            "+5V": v5, "+3V3": v3, "GND": gnd, "GNDA": gnda, "GNDD": gndd,
            "KNX_P_PROT": knx_p_prot, "VFILT": vfilt, "KNX_TXD": knx_txd, "KNX_RXD": knx_rxd,
            "NCN_SAVEB": saveb, "DALI_TX": dali_tx, "DALI_RX": dali_rx, "DA1": da1, "DA2": da2}

    block("int_ui")
    wire(ffc("UI board", "J3", "8-pin 0.5 mm FFC to the UI board J1"), UI_LINK, nets)
    block("int_knx")
    wire(ffc("KNX board", "J4", "8-pin 0.5 mm FFC to the KNX board J1"), KNX_LINK, nets)
    block("int_dali_selv")
    wire(ffc("DALI logic", "J5", "8-pin 0.5 mm FFC to the DALI board J2 (logic)"), DALI_SELV_LINK, nets)
    block("int_dali_bus")
    wire(ffc("DALI bus", "J6", "8-pin 0.5 mm FFC to the DALI board J3 (current-limited DA1/DA2)"),
         DALI_BUS_LINK, nets)


NET_CLASSES = {
    "SELV-KNX": ["GND", "+3V3", "+5V_XIAO", "BTN_LADDER", "LED_DATA", "LED_DALI", "I2C_SDA", "I2C_SCL",
                 "KNX_TXD", "KNX_RXD", "DALI_TX", "DALI_RX", "NCN_SAVEB", "DALI_SUPPLY_OFF", "BUCK5_SW", "Net-*"],
    "KNX-Power": ["KNX_P", "KNX_P_PROT", "VFILT", "+5V"],
    "SELV-AUX": ["GNDA", "AUX_IN", "AUX_FUSED", "AUX_P"],
    "DALI": ["GNDD", "DA1", "DA2", "DALI_ISO15V", "DALI_SUPPLY_SW"],
}
