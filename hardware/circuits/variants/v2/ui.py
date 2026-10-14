from blocks import mcu_xiao, ui
from common import block, part, power, power_flag, src
from skidl import Net


def build():
    gnd = power("GND")
    power_flag(gnd)
    nets = {name: Net(name) for name in ("+5V", "+3V3", "KNX_TXD", "KNX_RXD", "NCN_SAVEB",
                                         "DALI_TX", "DALI_RX", "DALI_SUPPLY_OFF")}
    for name in ("+5V", "+3V3"):
        power_flag(nets[name])
    block("int_main")
    connector = part("Connector", "Conn_01x10_Pin", "Controller cable", ui.JST_SH10,
                     src(mpn="BM10B-SRSS-TB(LF)(SN)", mfr="JST", lcsc="C160396",
                         source="V2 main-to-UI/controller, pin 1 to pin 1"), ref="J1")
    pins = [nets["+5V"], gnd, nets["+3V3"], nets["KNX_TXD"], nets["KNX_RXD"], nets["NCN_SAVEB"],
            nets["DALI_TX"], nets["DALI_RX"], nets["DALI_SUPPLY_OFF"], gnd]
    for i, net in enumerate(pins, start=1):
        connector[i] += net
    signals = mcu_xiao.build(gnd, nets["+3V3"], nets["+5V"], nets["KNX_TXD"], nets["KNX_RXD"],
                             nets["DALI_TX"], nets["DALI_RX"], nets["NCN_SAVEB"],
                             dali_supply_off=nets["DALI_SUPPLY_OFF"])
    ui.build_ui_components(gnd, nets["+3V3"], nets["+5V"], signals["btn_ladder"], signals["led_prog"],
                           signals["sda"], signals["scl"])


NET_CLASSES = {"SELV-KNX": ["*"]}
