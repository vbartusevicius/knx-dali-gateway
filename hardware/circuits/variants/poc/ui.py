"""POC UI board: 10-pin interconnect from main + button ladder + addressable RGB status LEDs + OLED.

UI board 49.5 x 40.5 mm, in the front protrusion at z~57.
"""

from blocks import ui
from common import power, power_flag
from skidl import Net


def build():
    gnd = power("GND")
    power_flag(gnd)
    v3 = Net("+3V3")
    btn_ladder = Net("BTN_LADDER")
    sda = Net("I2C_SDA")
    scl = Net("I2C_SCL")
    v5 = Net("+5V")
    led_data = Net("LED_DATA")
    led_dali = Net("LED_DALI")
    for n in (v3, v5):
        power_flag(n)

    signals = {"v3": v3, "v5": v5, "gnd": gnd, "btn_ladder": btn_ladder,
               "led_data": led_data, "led_dali": led_dali, "sda": sda, "scl": scl}

    ui.build_interconnect_side(signals)
    ui.build_ui_components(gnd, v3, v5, btn_ladder, led_data, sda, scl)


NET_CLASSES = dict(ui.NET_CLASSES)
