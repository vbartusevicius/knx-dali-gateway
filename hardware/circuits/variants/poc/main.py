"""POC main board: KNX + DALI + MCU (XIAO) blocks + 10-pin interconnect
to the UI board.

Main board 49.5 x 76.5 mm, parallel to the rail at z~20.
  Front side: terminals (screws face front), XIAO on female headers, optos,
              small parts.
  Rear side: tall parts — storage cap, isolated DC-DC, pass transistor
             with copper pour.

Domains per spec.boards.main:
  SELV-KNX: bottom (KNX terminal, NCN5130, storage, 3V3 buck, XIAO, UI conn)
  DALI: top (DALI terminal, bridge, TX sink, RX threshold, supply output)
  SELV-AUX: top-right corner (aux terminal, protection, DC-DC primary)
"""


from blocks import dali, knx, mcu_xiao
from common import block, part, power, power_flag, src
from skidl import Net

JST_SH10 = "Connector_JST:JST_SH_BM10B-SRSS-TB_1x10-1MP_P1.00mm_Vertical"


def build():
    gnd = power("GND")
    power_flag(gnd)

    # +3V3 logic rail — sourced from XIAO's onboard regulator 3V3 output.
    # Created here so both DALI (RX opto pull-up) and MCU blocks can reference it.
    v3 = Net("+3V3")
    power_flag(v3)

    # KNX block: NCN5130 + storage + 5V buck
    knx_nets = knx.build(gnd)

    # DALI block: interface + isolated DC-DC + current limiter
    # v3 = XIAO 3V3 output for RX opto MCU-side pull-up
    dali_nets = dali.build(gnd, v3)

    # MCU block: XIAO on headers, LM66100 ideal diode (5V feed)
    # XIAO 3V3 pin outputs v3 for our logic (optos, LEDs, OLED, pull-ups)
    mcu_nets = mcu_xiao.build(
        gnd, v3, knx_nets["v5"],
        knx_nets["knx_txd"], knx_nets["knx_rxd"],
        dali_nets["dali_tx"], dali_nets["dali_rx"],
        knx_nets["saveb"])

    # 10-pin interconnect to UI board
    block("int_ui")
    j_int = part("Connector", "Conn_01x10_Pin", "JST-SH 1x10", JST_SH10,
                 src(mpn="BM10B-SRSS-TB(LF)(SN)", mfr="JST", lcsc="C160396",
                     source="spec.yaml boards.interconnects main->ui"), ref="J4")
    pins = [mcu_nets["v3"], gnd, gnd,
            mcu_nets["btn_ladder"], gnd,
            mcu_nets["led_prog"], knx_nets["v5"], mcu_nets["led_dali"],
            mcu_nets["sda"], mcu_nets["scl"]]
    for i, net_ in enumerate(pins, start=1):
        j_int[i] += net_


NET_CLASSES = {**knx.NET_CLASSES, **dali.NET_CLASSES, **mcu_xiao.NET_CLASSES}
