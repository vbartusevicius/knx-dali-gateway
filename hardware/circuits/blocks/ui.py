"""UI block: button ladder (PRG/RST + FUNC), I2C RGB status LEDs, OLED header
and the 10-pin interconnect from the main board.

Button ladder on BTN_LADDER (XIAO D2, ADC): 3V3 -> 10k pull-up, 100 nF filter.
  idle 3.30 V; PRG/RST (20k) 2.20 V; FUNC (10k) 1.65 V; both (6.67k) 1.32 V.
  The closest pair is 0.33 V apart, so both-pressed is a reliable state.

Status LEDs: three SK6805-EC15 addressable RGB LEDs (5 V, 3.5-5.5 V) in one
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

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
LED_RGB = "gateway_fp:LED_SK6805-EC15_1.5x1.5mm"
SOT23_5 = "Package_TO_SOT_SMD:SOT-23-5"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
STATUS_LEDS = ("STATUS", "KNX", "DALI")
TACT_6X6 = "Button_Switch_SMD:SW_SPST_PTS645Sx43SMTR92"
JST_SH10 = "Connector_JST:JST_SH_BM10B-SRSS-TB_1x10-1MP_P1.00mm_Vertical"
HDR_4 = "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical"


def res(value, fields=None, footprint=R0603):
    return part("Device", "R", value, footprint, fields)


def cap(value, fields=None, footprint=C0603):
    return part("Device", "C", value, footprint, fields)


def status_leds(gnd, v5, led_data):
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
    series = res("330R", src(source="SK6805-EC15 datasheet: series resistor at DIN"))
    buffer[4] += series[1]
    data = series[2]
    for i, name in enumerate(STATUS_LEDS):
        led = part("gateway", "SK6805-EC15", f"RGB {name}", LED_RGB,
                   src(mpn="SK6805-EC15", mfr="OPSCO", lcsc="C2890035",
                       source="SK6805-EC15 datasheet p.4: 1 DIN, 2 VDD, 3 DOUT, 4 GND"),
                   ref=f"D{i + 1}")
        led["DIN"] += data
        led["VDD"] += v5
        led["GND"] += gnd
        c = cap("100n 50V", src(source="SK6805-EC15 datasheet: decoupling per LED"))
        c[1] += v5
        c[2] += gnd
        data = Net(f"LED_CHAIN{i + 1}") if i < len(STATUS_LEDS) - 1 else None
        if data is not None:
            led["DOUT"] += data


def build_interconnect_side(signals):
    """Create the 10-pin interconnect connector on the UI board."""
    block("int_main")
    j1 = part("Connector", "Conn_01x10_Pin", "JST-SH 1x10", JST_SH10,
              src(mpn="BM10B-SRSS-TB(LF)(SN)", mfr="JST", lcsc="C160396",
                  source="spec.yaml boards.interconnects main->ui"))
    pins = [signals["v3"], signals["gnd"], signals["gnd"],
            signals["btn_ladder"], signals["gnd"],
            signals["led_data"], signals["v5"], signals["led_dali"],
            signals["sda"], signals["scl"]]
    for i, net_ in enumerate(pins, start=1):
        j1[i] += net_


WINSTAR_OLED = {"mpn": "WEA012864DWPP3N00003", "mfr": "Winstar", "pins": ("VCC", "GND", "SCL", "SDA"),
                "source": "Winstar WEA012864D-03 (white; L = yellow, B = sky blue), 27.3x27.3 mm, SSD1306 I2C, "
                          "drawing WEA012864D-03 p.3; lay it face down in the cover pocket, push the UI board "
                          "over its pins, then solder them from the back"}


def build_ui_components(gnd, v3, v5, btn_ladder, led_data, sda, scl, oled=WINSTAR_OLED):
    """Create button ladder, LEDs, and OLED header on the UI board."""
    block("btn_ladder")
    r_pull = res("10k", src(source="Ladder pull-up; no-button = 3V3"))
    r_pull[1] += v3
    r_pull[2] += btn_ladder
    c_ladder = cap("100n 50V", src(source="Ladder debounce/filter cap"))
    c_ladder[1] += btn_ladder
    c_ladder[2] += gnd
    sw1 = part("Switch", "SW_Push", "PRG/RST", TACT_6X6,
               src(mpn="PTS645SL70SMTR92LFS", mfr="C&K", lcsc="C285485",
                   source="tap: reboot; hold 5 s: KNX programming mode"))
    r_prog = res("20k", src(source="PRG/RST ladder: 3V3*20k/30k=2.2V"))
    sw1[1] += btn_ladder
    sw1[2] += r_prog[1]
    r_prog[2] += gnd
    sw2 = part("Switch", "SW_Push", "FUNC", TACT_6X6,
               src(mpn="PTS645SL70SMTR92LFS", mfr="C&K", lcsc="C285485",
                   source="tap: display page / wake; hold 5 s: WiFi setup"))
    r_reset = res("10k", src(source="FUNC ladder: 3V3*10k/20k=1.65V; both 1.32V"))
    sw2[1] += btn_ladder
    sw2[2] += r_reset[1]
    r_reset[2] += gnd

    status_leds(gnd, v5, led_data)

    block("oled")
    j3 = part("Connector", "Conn_01x04_Pin", "OLED", HDR_4,
              src(mpn=oled["mpn"], mfr=oled.get("mfr", ""), hand=True, source=oled["source"]), ref="J2")
    nets = {"GND": gnd, "VCC": v3, "SCL": scl, "SDA": sda}
    for i, name in enumerate(oled["pins"], start=1):
        j3[i] += nets[name]


NET_CLASSES = {
    "SELV-KNX": ["GND", "+3V3", "+5V", "BTN_LADDER", "LED_DATA", "LED_DALI",
                 "LED_CHAIN1", "LED_CHAIN2", "I2C_SDA", "I2C_SCL", "Net-*"],
}
