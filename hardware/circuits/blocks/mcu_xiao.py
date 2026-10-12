"""MCU block: Seeed XIAO ESP32C6 on 2x7 female 2.54mm headers, powered
from the KNX 5V buck via a TI LM66100 ideal diode so a USB-powered XIAO
cannot back-feed the KNX side.

XIAO ESP32C6 pinout (Seeed wiki, verified against schematic v1.0):
  Edge header (2x7, 2.54mm pitch, 15.24mm row spacing), counter-clockwise:
    1=D0/GPIO0   2=D1/GPIO1    3=D2/GPIO2    4=D3/GPIO21
    5=D4/GPIO22  6=D5/GPIO23   7=D6/GPIO16   8=D7/GPIO17
    9=D8/GPIO19  10=D9/GPIO20  11=D10/GPIO18 12=3V3
    13=GND       14=5V
  EN/CHIP_PU is not on the castellated header (bottom pad only) and
  cannot be reached through female headers — pad 15 removed.

Power path: +5V (KNX buck) -> LM66100 -> XIAO 5V pin.
XIAO's onboard regulator produces +3V3 from 5V; the XIAO 3V3 pin
outputs +3V3 for our low-power logic (optos, LEDs, OLED, pull-ups).

Pin map (XIAO GPIO -> function):
  D0/GPIO0   = LED_DATA (addressable RGB LED chain on the UI board)
  D1/GPIO1   = spare (its cable wire now carries +5V to the UI board LEDs)
  D2/GPIO2   = ADC ladder (PROG + RESET buttons, on UI board)
  D3/GPIO21  = LED_DALI (yellow, on UI board)
  D4/GPIO22  = I2C SDA (OLED, on UI board)
  D5/GPIO23  = I2C SCL (OLED, on UI board)
  D6/GPIO16  = KNX UART TX (MCU -> NCN5130 SDI/RXD)
  D7/GPIO17  = KNX UART RX (NCN5130 SDO/TXD -> MCU)
  D8/GPIO19  = DALI RX
  D9/GPIO20  = NCN SAVEB
  D10/GPIO18 = DALI TX
  EN/CHIP_PU = not wired (bottom pad, unreachable via headers)
  NCN RESETB = NC (internal pull-up; SAVEB on D9 is sufficient)
  UI RESET   = firmware reboot (config kept); hard reset via XIAO button
               or power cycle
"""

from builtins import NC

from common import block, part, src
from skidl import Net

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
SC70_6 = "Package_TO_SOT_SMD:SOT-363_SC-70-6"
XIAO_FP = "gateway_fp:XIAO_ESP32C6_Header"

MCU_PINS = {
    "led_data": {"pin": "D0/GPIO0", "net": "LED_DATA", "note": "RMT, SK6805 chain via 5 V buffer on UI"},
    "button_ladder": {"pin": "D2/GPIO2", "net": "BTN_LADDER",
                      "note": "ADC ladder: PRG/RST + FUNC on UI board"},
    "led_dali": {"pin": "D3/GPIO21", "net": "LED_DALI", "note": "spare, unused on the UI board"},
    "i2c_sda": {"pin": "D4/GPIO22", "net": "I2C_SDA",
                "note": "OLED on UI board"},
    "i2c_scl": {"pin": "D5/GPIO23", "net": "I2C_SCL",
                "note": "OLED on UI board"},
    "knx_uart_tx": {"pin": "D6/GPIO16", "net": "KNX_RXD",
                    "note": "MCU TX -> NCN5130 SDI/RXD"},
    "knx_uart_rx": {"pin": "D7/GPIO17", "net": "KNX_TXD",
                    "note": "NCN5130 SDO/TXD -> MCU RX"},
    "dali_rx": {"pin": "D8/GPIO19", "net": "DALI_RX",
               "note": "RX optocoupler -> RMT RX"},
    "ncn_saveb": {"pin": "D9/GPIO20", "net": "NCN_SAVEB",
                  "note": "NCN5130 SAVEB (open-collector)"},
    "dali_tx": {"pin": "D10/GPIO18", "net": "DALI_TX",
                "note": "RMT TX -> TX optocoupler"},
}


def res(value, fields=None, footprint=R0603):
    return part("Device", "R", value, footprint, fields)


def cap(value, fields=None, footprint=C0603):
    return part("Device", "C", value, footprint, fields)


def build(gnd, v3, v5_knx, knx_txd, knx_rxd, dali_tx, dali_rx, saveb):
    """Build the MCU block.

    Args:
        gnd: KNX-domain ground
        v5_knx: +5V from the KNX buck (our rail)
        knx_txd, knx_rxd: NCN5130 UART signals
        dali_tx, dali_rx: DALI logic signals
        saveb: NCN5130 SAVEB signal

    Returns dict of UI-board signals: btn_ladder, led_prog (LED_DATA),
        led_dali, sda, scl, v3 (XIAO 3V3 output for logic)
    """
    v5 = Net("+5V_XIAO")
    btn_ladder = Net("BTN_LADDER")
    sda = Net("I2C_SDA")
    scl = Net("I2C_SCL")
    led_prog = Net("LED_DATA")
    led_dali = Net("LED_DALI")

    # --- LM66100 ideal diode (SC70-6): v5_knx -> v5 (XIAO 5V pin) ---
    # CE tied to GND for always-on (active-low); ST (status) NC.
    block("mcu_power")
    d_ideal = part("Power_Management", "LM66100DCK", "LM66100", SC70_6,
                   src(mpn="LM66100DCKR", mfr="TI", lcsc="C2869734",
                       source="TI LM66100 datasheet; SC70-6; CE active-low tied GND, ST NC"))
    d_ideal["VIN"] += v5_knx
    d_ideal["~{CE}"] += gnd
    d_ideal["GND"] += gnd
    d_ideal["VOUT"] += v5
    d_ideal["ST"] += NC
    c_v5 = cap("10u 10V", src(source="XIAO 5V decoupling"))
    c_v5[1] += v5
    c_v5[2] += gnd

    # --- XIAO ESP32C6 on female headers (counter-clockwise pin map) ---
    block("mcu")
    u1 = part("gateway", "XIAO_ESP32C6", "XIAO ESP32C6", XIAO_FP,
              src(mpn="Seeed XIAO ESP32C6", mfr="Seeed Studio", hand=True,
                  source="Seeed wiki/schematic v1.0; plugs into 2x 1x7 female "
                         "headers PM2.54-1x7PZZ-H8.5 (LCSC C54876729)"))
    u1["1"] += led_prog        # D0/GPIO0
    u1["3"] += btn_ladder      # D2/GPIO2 (ADC)
    u1["4"] += led_dali        # D3/GPIO21
    u1["5"] += sda             # D4/GPIO22
    u1["6"] += scl             # D5/GPIO23
    u1["7"] += knx_rxd         # D6/GPIO16 — MCU TX -> NCN5130
    u1["8"] += knx_txd         # D7/GPIO17 — NCN5130 -> MCU RX
    u1["9"] += dali_rx         # D8/GPIO19
    u1["10"] += saveb          # D9/GPIO20
    u1["11"] += dali_tx        # D10/GPIO18
    u1["12"] += v3             # 3V3 — XIAO onboard regulator OUTPUT
    u1["13"] += gnd            # GND
    u1["14"] += v5             # 5V — fed via LM66100 from KNX 5V buck

    # I2C pull-ups (on main board, near XIAO)
    for net_ in (sda, scl):
        r = res("4k7", src(source="I2C pull-up"))
        r[1] += v3
        r[2] += net_

    return {"v3": v3, "btn_ladder": btn_ladder,
            "led_prog": led_prog, "led_dali": led_dali,
            "sda": sda, "scl": scl}


NET_CLASSES = {
    "SELV-KNX": ["GND", "+3V3", "+5V_XIAO", "BTN_LADDER", "LED_DATA",
                 "LED_DALI", "I2C_SDA", "I2C_SCL", "KNX_TXD",
                 "KNX_RXD", "DALI_TX", "DALI_RX", "NCN_SAVEB", "Net-*"],
}
