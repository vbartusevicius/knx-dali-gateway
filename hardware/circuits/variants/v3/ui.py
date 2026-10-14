"""V3 UI board (1 DIN front, 13.8 x 40.5 mm): Winstar WEA012832F 0.91" OLED
mounted vertically, two TS-1088 switches pressed through printed plungers in
the front wall, three SK6805 RGB LEDs; FFC J1 to the carrier."""

from blocks import ui
from common import block, power, power_flag
from skidl import Net

from variants.v3.main import UI_LINK, ffc, wire

WINSTAR_OLED_91 = {"mpn": "WEA012832FWPP3N00000", "mfr": "Winstar", "pins": ("GND", "VCC", "SCL", "SDA"),
                   "source": "Winstar WEA012832F (0.91in 128x32 SSD1306 I2C, 35.8 x 12.0 x 2.41 mm, pins 1 GND, "
                             "2 VIN, 3 SCL, 4 SDA; drawing p.3), mounted vertically face down in the cover pocket"}
BUTTON_TS1088 = {"footprint": "Button_Switch_SMD:SW_SPST_TS-1088-xR020", "mpn": "TS-1088-AR02016",
                 "mfr": "XUNPU", "lcsc": "C720477"}


def build():
    gnd = power("GND")
    power_flag(gnd)
    names = ("+3V3", "BTN_LADDER", "I2C_SDA", "I2C_SCL", "+5V", "LED_DATA")
    v3, btn_ladder, sda, scl, v5, led_data = (Net(n) for n in names)
    for net in (v3, v5):
        power_flag(net)
    block("int_main")
    wire(ffc("Carrier", "J1", "FFC to the carrier J4"), UI_LINK,
         {"+3V3": v3, "GND": gnd, "BTN_LADDER": btn_ladder, "LED_DATA": led_data, "+5V": v5, "I2C_SDA": sda,
          "I2C_SCL": scl})
    ui.build_ui_components(gnd, v3, v5, btn_ladder, led_data, sda, scl, oled=WINSTAR_OLED_91, button=BUTTON_TS1088)


NET_CLASSES = dict(ui.NET_CLASSES)
