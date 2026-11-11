"""UI block: button ladder (SCROLL left + RESET right), I2C RGB status LEDs, OLED header
and the 10-pin interconnect from the main board.

Button ladder on BTN_LADDER (XIAO D2, ADC): 3V3 -> 10k pull-up, 100 nF filter.
  idle 3.30 V; RESET (20k, right) 2.20 V; SCROLL (10k, left) 1.65 V; both (6.67k) 1.32 V.
  The closest pair is 0.33 V apart, so both-pressed is a reliable state.

Status LEDs: three WS2812B-MINI-V3J addressable RGB LEDs (5 V, legs past the body) in one
chain, D1 STATUS -> D2 KNX -> D3 DALI. The data line LED_DATA is XIAO D0; an SN74AHCT1G125 powered from 5 V lifts its 3.3 V level
(VIH 2.0 V) to 5 V, through 330R as the datasheet's series protection. 5 V
comes from the main board's KNX buck on the former LED_KNX wire; LED_DALI
arrives unused. Light shines through a thin spot in the front wall inside a
printed baffle per LED.

OLED module: SSD1306 128x64 I2C 0.96", 4-pin header GND, VCC (3V3), SCL, SDA;
its pins solder straight into J2.
"""

from common import block, part, src
from skidl import Net

R_SMD = "Resistor_SMD:R_0805_2012Metric"   # hand-solderable size (user, 2026-11-11)
C_SMD = "Capacitor_SMD:C_0805_2012Metric"
# Addressable RGB LED with gull-wing legs (hand soldering, user 2026-11-11):
# 1 VDD, 2 DOUT, 3 GND, 4 DIN.
LED_RGB = {"mpn": "WS2812B-MINI-V3J", "mfr": "Worldsemi", "lcsc": "C22371519",
           "footprint": "gateway_fp:LED_WS2812B-MINI-V3J_3.5x3.5mm",
           "source": "WS2812B-MINI-V3J (EasyEDA C22371519): 1 VDD, 2 DOUT, 3 GND, 4 DIN; 3.5 x 3.5 x 1.57 mm, "
                     "gull-wing legs, 6.2 mm span"}
SOT23_5 = "Package_TO_SOT_SMD:SOT-23-5"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
STATUS_LEDS = ("STATUS", "KNX", "DALI")
JST_SH10 = "Connector_JST:JST_SH_BM10B-SRSS-TB_1x10-1MP_P1.00mm_Vertical"
HDR_4 = "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical"


def res(value, fields=None, footprint=R_SMD):
    return part("Device", "R", value, footprint, fields)


def cap(value, fields=None, footprint=C_SMD):
    return part("Device", "C", value, footprint, fields)


def status_leds(gnd, v5, led_data, led=LED_RGB):
    block("status_leds")
    bulk = cap("1u 50V", src(source="5 V entry from the cable"), footprint=C0805)
    bulk[1] += v5
    bulk[2] += gnd
    buffer = part("74xGxx", "74AHCT1G125", "74AHCT1G125", SOT23_5,
                  src(mpn="SN74AHCT1G125DBVR", mfr="TI", lcsc="C7484",
                      source="TI SN74AHCT1G125 DBV: 1 OE, 2 A, 3 GND, 4 Y, 5 VCC"))
    buffer[1] += gnd
    buffer[2] += led_data
    buffer[3] += gnd
    buffer[5] += v5
    c_buf = cap("100n 50V", src(source="74AHCT1G125 decoupling"))
    c_buf[1] += v5
    c_buf[2] += gnd
    series = res("330R", src(source="WS2812B-MINI-V3J: series resistor at DIN"))
    buffer[4] += series[1]
    data = series[2]
    for i, name in enumerate(STATUS_LEDS):
        d = part("gateway", "WS2812B-MINI", f"RGB {name}", led["footprint"],
                 src(mpn=led["mpn"], mfr=led["mfr"], lcsc=led["lcsc"], source=led["source"]), ref=f"D{i + 1}")
        d["DIN"] += data
        d["VDD"] += v5
        d["GND"] += gnd
        c = cap("100n 50V", src(source="WS2812B-MINI-V3J: decoupling per LED"))
        c[1] += v5
        c[2] += gnd
        data = Net(f"LED_CHAIN{i + 1}") if i < len(STATUS_LEDS) - 1 else None
        if data is not None:
            d["DOUT"] += data


def build_interconnect_side(signals):
    """Create the 10-pin interconnect connector on the UI board."""
    block("int_main")
    j1 = part("Connector", "Conn_01x10_Pin", "JST-SH 1x10", JST_SH10,
              src(mpn="BM10B-SRSS-TB(LF)(SN)", mfr="JST", lcsc="C160396",
                  source="spec.yaml boards.interconnects main->ui"))
    pins = [signals["v3"], signals["gnd"], signals["gnd"],
            signals["btn_ladder"], signals["gnd"],
            signals["led_data"], signals["v5"], signals["gnd"],
            signals["sda"], signals["scl"]]
    for i, net_ in enumerate(pins, start=1):
        j1[i] += net_


WINSTAR_OLED = {"mpn": "WEA012864DWPP3N00003", "mfr": "Winstar", "pins": ("VCC", "GND", "SCL", "SDA"),
                "source": "Winstar WEA012864D-03 (white; L = yellow, B = sky blue), 27.3x27.3 mm, SSD1306 I2C, "
                          "drawing WEA012864D-03 p.3; lay it face down in the cover pocket, push the UI board "
                          "over its pins, then solder them from the back"}


# Omron B3U-1000P (1.6 mm): pressed through a C-shaped spring tab in the cover front with a plunger
BUTTON_B3U = {"footprint": "Button_Switch_SMD:SW_SPST_B3U-1000P", "mpn": "B3U-1000P",
              "mfr": "Omron", "lcsc": "C231329"}


def build_ui_components(gnd, v3, v5, btn_ladder, led_data, sda, scl, oled=WINSTAR_OLED, led=LED_RGB):
    """Create button ladder, LEDs, and OLED header on the UI board."""
    block("btn_ladder")
    r_pull = res("10k", src(source="Ladder pull-up; no-button = 3V3"))
    r_pull[1] += v3
    r_pull[2] += btn_ladder
    c_ladder = cap("100n 50V", src(source="Ladder debounce/filter cap"))
    c_ladder[1] += btn_ladder
    c_ladder[2] += gnd
    # SW1 is the left button (SCROLL, dot bump), SW2 the right one (RESET,
    # cross bump); user 2026-11-11.
    sw1 = part("Switch", "SW_Push", "SCROLL", BUTTON_B3U["footprint"],
               src(mpn=BUTTON_B3U["mpn"], mfr=BUTTON_B3U["mfr"], lcsc=BUTTON_B3U["lcsc"],
                   source="left, dot: tap: display page / wake; hold 5 s: WiFi setup"))
    r_scroll = res("10k", src(source="SCROLL ladder: 3V3*10k/20k=1.65V; both 1.32V"))
    sw1[1] += btn_ladder
    sw1[2] += r_scroll[1]
    r_scroll[2] += gnd
    sw2 = part("Switch", "SW_Push", "RESET", BUTTON_B3U["footprint"],
               src(mpn=BUTTON_B3U["mpn"], mfr=BUTTON_B3U["mfr"], lcsc=BUTTON_B3U["lcsc"],
                   source="right, cross: tap: reboot; hold 5 s: KNX programming mode"))
    r_reset = res("20k", src(source="RESET ladder: 3V3*20k/30k=2.2V"))
    sw2[1] += btn_ladder
    sw2[2] += r_reset[1]
    r_reset[2] += gnd

    status_leds(gnd, v5, led_data, led)

    block("oled")
    n_pins = len(oled["pins"])
    if "lcsc" in oled:
        j3 = part("Connector", f"Conn_01x{n_pins:02d}_Socket", "OLED ZIF", oled["footprint"],
                  src(mpn=oled["mpn"], mfr=oled["mfr"], lcsc=oled["lcsc"], source=oled["source"]), ref="J2")
    else:
        j3 = part("Connector", f"Conn_01x{n_pins:02d}_Pin", "OLED", oled.get("footprint", HDR_4),
                  src(mpn=oled["mpn"], mfr=oled.get("mfr", ""), hand=True, source=oled["source"]), ref="J2")
    nets = {"GND": gnd, "VCC": v3, "SCL": scl, "SDA": sda}
    if "RES" in oled["pins"]:
        oled_res = Net("OLED_RES")
        r_res = res("10k", src(source="OLED RES# pull-up"))
        c_res = cap("100n 50V", src(source="OLED RES# RC power-on reset"))
        r_res[1] += v3
        r_res[2] += oled_res
        c_res[1] += oled_res
        c_res[2] += gnd
        nets["RES"] = oled_res
    if "C1P" in oled["pins"]:
        c1p, c1n, c2p, c2n = Net("OLED_C1P"), Net("OLED_C1N"), Net("OLED_C2P"), Net("OLED_C2N")
        iref, vcomh, vout = Net("OLED_IREF"), Net("OLED_VCOMH"), Net("OLED_VOUT")
        c_fly1 = cap("1u 50V", src(source="SSD1312 C1P-C1N charge-pump flying cap"))
        c_fly1[1] += c1p
        c_fly1[2] += c1n
        c_fly2 = cap("1u 50V", src(source="SSD1312 C2P-C2N charge-pump flying cap"))
        c_fly2[1] += c2p
        c_fly2[2] += c2n
        r_iref = res("390k", src(source="SSD1312 IREF segment current reference (390k to GND)"))
        r_iref[1] += iref
        r_iref[2] += gnd
        c_vcomh = cap("1u 50V", src(source="SSD1312 VCOMH deselect decoupling cap"))
        c_vcomh[1] += vcomh
        c_vcomh[2] += gnd
        c_vout = cap("1u 50V", src(source="SSD1312 VCC charge-pump output stabilization cap"))
        c_vout[1] += vout
        c_vout[2] += gnd
        nets.update({"C1P": c1p, "C1N": c1n, "C2P": c2p, "C2N": c2n,
                     "IREF": iref, "VCOMH": vcomh, "VOUT": vout})
    for i, name in enumerate(oled["pins"], start=1):
        if name != "NC":
            j3[i] += nets[name]


NET_CLASSES = {
    "SELV-KNX": ["GND", "+3V3", "+5V", "BTN_LADDER", "LED_DATA", 
                 "LED_CHAIN1", "LED_CHAIN2", "I2C_SDA", "I2C_SCL", "Net-*"],
}
