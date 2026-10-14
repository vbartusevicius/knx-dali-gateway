"""V3 KNX board, horizontal, plugged onto the carrier: NCN5130 with its
datasheet parts on top, FFC J1 to the carrier, the 470 uF storage
capacitor underneath. The 5 V buck sits on the carrier (VFILT crosses J1)."""

from blocks import knx
from common import block, power, power_flag

from variants.v3.main import KNX_LINK, ffc, wire


def build():
    gnd = power("GND")
    power_flag(gnd)
    nets = knx.build(gnd, smd_storage=True, terminal=False, input_stage=False, buck=False, thermal_vias=False)
    by_name = {"GND": gnd, "KNX_P_PROT": nets["knx_p_prot"], "VFILT": nets["vfilt"], "KNX_TXD": nets["knx_txd"],
               "KNX_RXD": nets["knx_rxd"], "NCN_SAVEB": nets["saveb"]}
    block("int_main")
    wire(ffc("Carrier", "J1", "FFC to the carrier J1"), KNX_LINK, by_name)

NET_CLASSES = dict(knx.NET_CLASSES)
