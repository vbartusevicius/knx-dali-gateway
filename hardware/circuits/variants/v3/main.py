"""V3 main board (vertical along the left wall of the 1-DIN case, 39.8 x 78.0 mm,
4 layers, 1.2 mm, two-sided).

Eliminates all hidden sub-boards by using both sides of the vertical main PCB:
- Front (+x, 11.4 mm clearance to the right wall): tall parts — J1 (KNX red/black
  pin strip, bottom-front, flush with y=0), J2 (AUX 20-32 V yellow/white pin
  strip, bottom-rear, flush with y=0), J3 (DALI push-pin strip, top-front, flush
  with y=81), C4 (470 uF SMD storage cap), L1 (22 uH KNX inductor), U5 (XIAO on
  5 mm sockets with the 5 V buck underneath), U2 (REM3-2415S/A/CTRL), JP1 (DALI
  supply jumper), Q1 (MJD45H11 DPAK), the three SO-6L optocouplers (with milled
  isolation slots), and J4 (8-pin 0.5 mm FFC to the UI board).
- Back (-x, 2.0 mm clearance to the left wall, inside the shadow of the THT
  solder tails): low SMD parts (<= 1.8 mm) — NCN5130 QFN-40 + crystal Y1 + KNX
  passives, AUX protection, and DALI bridge/TX/RX passives & transistors.
"""

from builtins import NC

from blocks import dali, knx, mcu_xiao
from common import block, part, power, power_flag, src
from skidl import Net

FPC_8P = "gateway_fp:FPC_Kinghelm_KH-FPC0.5-H1.2SMT-8P"
UI_LINK = ("+3V3", "GND", "BTN_LADDER", "LED_DATA", "+5V", "I2C_SDA", "I2C_SCL", "GND")


def ffc(value, ref, source):
    return part("Connector", "Conn_01x08_Socket", value, FPC_8P,
                src(mpn="KH-FPC0.5-H1.2SMT-8P-QCHF", mfr="Kinghelm", lcsc="C48532769", source=source), ref=ref)


def wire(conn, names, nets):
    for pin, name in enumerate(names, start=1):
        conn[pin] += NC if name is None else nets[name]


def build():
    gnd = power("GND")
    v3 = power("+3V3")
    supply_off = Net("DALI_SUPPLY_OFF")

    knx_nets = knx.build(gnd, smd_storage=True, thermal_vias=False)
    v5, knx_txd, knx_rxd, saveb = (knx_nets[k] for k in ("v5", "knx_txd", "knx_rxd", "saveb"))

    dali_nets = dali.build(gnd, v3, terminals=False, supply_off=supply_off)
    da1, da2, gndd, aux_in, gnda = (dali_nets[k] for k in ("da1", "da2", "gndd", "aux_in", "gnda"))
    dali_tx, dali_rx = dali_nets["dali_tx"], dali_nets["dali_rx"]

    block("aux_in")
    aux = knx.knx_pin_strip("AUX 20-32 V (yellow/white)", "J2")
    aux[1] += aux_in
    aux[2] += gnda
    power_flag(aux_in)

    block("dali_terminal")
    dali_conn = part("Connector", "Screw_Terminal_01x02", "DALI bus (push-in)",
                     "gateway_fp:TerminalBlock_Kefa_KF142R-5.08-2P",
                     src(mpn="KF142R-5.08-2P", mfr="Cixi Kefa Elec", lcsc="C475144",
                         source="2-pole 5.08 mm right-angle push-in spring-clamp terminal block (250 V, 15 A, 1.5 mm2)"),
                     ref="J3")
    dali_conn[1] += da1
    dali_conn[2] += da2
    power_flag(gndd)

    ui = mcu_xiao.build(gnd, v3, v5, knx_txd, knx_rxd, dali_tx, dali_rx, saveb, dali_supply_off=supply_off)

    nets = {"BTN_LADDER": ui["btn_ladder"], "LED_DATA": ui["led_prog"], "I2C_SDA": ui["sda"], "I2C_SCL": ui["scl"],
            "+5V": v5, "+3V3": v3, "GND": gnd}
    block("int_ui")
    wire(ffc("UI board", "J4", "8-pin 0.5 mm FFC to the UI board J1"), UI_LINK, nets)


NET_CLASSES = {
    "SELV-KNX": ["GND", "+3V3", "+5V_XIAO", "BTN_LADDER", "LED_DATA", "I2C_SDA", "I2C_SCL",
                 "KNX_TXD", "KNX_RXD", "DALI_TX", "DALI_RX", "NCN_SAVEB", "DALI_SUPPLY_OFF", "BUCK5_SW",
                 "+3V3_TRX", "+20V_SW", "VDDD", "VDDA", "CEQ1", "CEQ2", "XTAL1", "XTAL2", "XSEL", "Net-*"],
    "KNX-Power": ["KNX_P", "KNX_P_PROT", "VFILT", "+5V"],
    "SELV-AUX": ["GNDA", "AUX_IN", "AUX_FUSED", "AUX_P"],
    "DALI": ["GNDD", "DA1", "DA2", "DA_P", "DALI_ISO15V", "DALI_SUPPLY_SW", "DALI_BOOT", "DALI_GATE",
             "DALI_GATE_SW", "DALI_SENSE", "RX_LIM_IN"],
}
