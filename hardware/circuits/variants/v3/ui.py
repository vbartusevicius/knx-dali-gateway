"""V3 UI board (1 DIN front, 13.8 x 40.5 mm): Crystalfontz CFAL12832D-B 0.91"
128x32 SSD1306 OLED (30.0 x 11.5 x 1.45 mm, charge-pump caps on the flex tail)
connected via a JLC-assembled 12-pin 0.5 mm Flip-Top ZIF FPC connector J2 on
the back (zero hand-soldering), two TS-1088 switches pressed through printed
plungers, and three SK6805 RGB LEDs below the buttons; FFC J1 to the carrier."""

from blocks import ui
from common import block, power, power_flag
from skidl import Net

from variants.v3.main import UI_LINK, ffc, wire

CFAL12832_ZIF12 = {
    "footprint": "gateway_fp:FPC_XUNPU_FPC-05F-12PH20",
    "mpn": "FPC-05F-12PH20",
    "mfr": "XUNPU",
    "lcsc": "C2856799",
    "pins": ("GND", "SDA", "SDA", "SCL", "GND", "RES", "GND", "VCC", "GND", "VCC", "VCC", "GND"),
    "source": "12-pin 0.5 mm Flip-Top bottom-contact ZIF FPC connector on the back of the UI board receiving "
              "the folded flex tail of the Crystalfontz CFAL12832D-B 0.91in 128x32 SSD1306 OLED (datasheet p.5-7)",
}
BUTTON_B3U = {"footprint": "Button_Switch_SMD:SW_SPST_B3U-1000P", "mpn": "B3U-1000P",
              "mfr": "Omron", "lcsc": "C231329"}


def build():
    gnd = power("GND")
    power_flag(gnd)
    names = ("+3V3", "BTN_LADDER", "I2C_SDA", "I2C_SCL", "+5V", "LED_DATA")
    v3, btn_ladder, sda, scl, v5, led_data = (Net(n) for n in names)
    for net in (v3, v5):
        power_flag(net)
    block("int_main")
    wire(ffc("Carrier", "J1", "FFC to the carrier"), UI_LINK,
         {"+3V3": v3, "GND": gnd, "BTN_LADDER": btn_ladder, "LED_DATA": led_data, "+5V": v5, "I2C_SDA": sda,
          "I2C_SCL": scl})
    ui.build_ui_components(gnd, v3, v5, btn_ladder, led_data, sda, scl, oled=CFAL12832_ZIF12, button=BUTTON_B3U)


NET_CLASSES = dict(ui.NET_CLASSES)
