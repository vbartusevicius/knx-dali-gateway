# SPDX-FileCopyrightText: The knx-dali-gateway contributors
# SPDX-License-Identifier: CERN-OHL-P-2.0
"""Upper board circuit: ESP32-C6-WROOM-1-N8, USB-C service connector with
ESD protection, PROG/RESET buttons, status LEDs, optional OLED header and
the 10-pin board interconnect.

Reference: Espressif ESP32-C6-WROOM-1 datasheet v1.4 (pinout, EN RC),
ESP32-C6-DevKitC-1 schematic v1.4 (USB-C CC resistors, ESD placement).
All values draft — needs electrical review.
"""

from builtins import NC  # injected by skidl's default Circuit

from skidl import Net

from common import block, part, power, power_flag, src

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C_ELEC = "Capacitor_THT:CP_Radial_D5.0mm_P2.00mm"
D_SOD123 = "Diode_SMD:D_SOD-123"
LED0603 = "LED_SMD:LED_0603_1608Metric"
USBC_FP = "Connector_USB:USB_C_Receptacle_G-Switch_GT-USB-7010ASV"
ESD_FP = "Package_TO_SOT_SMD:SOT-23-6"
TACT_6X6 = "Button_Switch_SMD:SW_SPST_Omron_B3FS-100xP"
SOT223 = "Package_TO_SOT_SMD:SOT-223-3_TabPin2"
JST_SH10 = "Connector_JST:JST_SH_SM10B-SRSS-TB_1x10-1MP_P1.00mm_Horizontal"
HDR_4 = "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical"
ESP32_FP = "gateway_fp:ESP32-C6-WROOM-1"
MOUNT = "MountingHole:MountingHole_3.2mm_M3"

# MCU pin assignment — also emitted into spec.yaml (derived.mcu_pins) by
# gen_circuits.py so a firmware board definition can be generated later.
MCU_PINS = {
    "knx_uart_rx": {"pin": "IO20", "net": "KNX_TXD",
                    "note": "NCN5130 SDO/TXD -> MCU RX"},
    "knx_uart_tx": {"pin": "IO21", "net": "KNX_RXD",
                    "note": "MCU TX -> NCN5130 SDI/RXD"},
    "dali_tx": {"pin": "IO18", "net": "DALI_TX",
                "note": "RMT TX channel -> TX optocoupler"},
    "dali_rx": {"pin": "IO19", "net": "DALI_RX",
                "note": "RX optocoupler -> RMT RX channel"},
    "button_prog": {"pin": "IO9", "net": "PROG_BTN",
                    "note": "BOOT strapping pin; also enters download mode"},
    "led_prog": {"pin": "IO10", "net": "LED_PROG", "note": "red, light pipe"},
    "led_knx": {"pin": "IO11", "net": "LED_KNX", "note": "green, light pipe"},
    "led_dali": {"pin": "IO2", "net": "LED_DALI", "note": "yellow, light pipe"},
    "i2c_sda": {"pin": "IO6", "net": "I2C_SDA", "note": "optional OLED"},
    "i2c_scl": {"pin": "IO7", "net": "I2C_SCL", "note": "optional OLED"},
    "usb_dm": {"pin": "IO12", "net": "USB_DM", "note": "native USB D-"},
    "usb_dp": {"pin": "IO13", "net": "USB_DP", "note": "native USB D+"},
    "ncn_saveb": {"pin": "IO8", "net": "INT_SPARE1",
                  "note": "spare interconnect pin 9 -> NCN5130 SAVEB"},
    "ncn_resetb": {"pin": "IO1", "net": "INT_SPARE2",
                   "note": "spare interconnect pin 10 -> NCN5130 RESETB"},
}


def res(value, fields=None, footprint=R0603):
    return part("Device", "R", value, footprint, fields)


def cap(value, fields=None, footprint=C0603):
    return part("Device", "C", value, footprint, fields)


def led(label, colour, net_anode, gnd):
    d = part("Device", "LED", f"{colour} {label}", LED0603,
             src(mpn="TBD", source="spec.yaml front_ui.leds, review"))
    r = res("1k", src(source="LED series resistor, review"))
    r[1] += net_anode
    d[1] += r[2]      # anode
    d[2] += gnd       # cathode


def build():
    gnd = power("GND")
    power_flag(gnd)

    v3 = Net("+3V3")
    int3v3 = Net("+3V3_MCU")        # from lower board via interconnect
    vbus = Net("+5V_USB")
    usb3v3 = Net("+3V3_USB")
    en = Net("EN")
    usb_dp = Net("USB_DP")
    usb_dm = Net("USB_DM")
    knx_txd = Net("KNX_TXD")
    knx_rxd = Net("KNX_RXD")
    dali_tx = Net("DALI_TX")
    dali_rx = Net("DALI_RX")
    spare1 = Net("INT_SPARE1")
    spare2 = Net("INT_SPARE2")
    prog_btn = Net("PROG_BTN")
    sda = Net("I2C_SDA")
    scl = Net("I2C_SCL")
    for n in (v3, int3v3, vbus, usb3v3, en):
        power_flag(n)

    # =======================================================================
    # interconnect (same pin order as the lower board)
    # =======================================================================
    block("interconnect")
    j1 = part("Connector", "Conn_01x10_Pin", "JST-SH 1x10", JST_SH10,
              src(mpn="SM10B-SRSS-TB", mfr="JST", lcsc="C160404",
                  source="spec.yaml boards.interconnect"))
    pins = [int3v3, gnd, knx_txd, knx_rxd, gnd, dali_tx, dali_rx, gnd,
            spare1, spare2]
    for i, net_ in enumerate(pins, start=1):
        j1[i] += net_

    # =======================================================================
    # USB-C service connector (ESP32-C6-DevKitC-1 schematic: 5.1k CC
    # pull-downs, ESD protection on D+/D-)
    # =======================================================================
    block("usbc")
    j2 = part("Connector", "USB_C_Receptacle_USB2.0_16P", "USB-C", USBC_FP,
              src(mpn="TBD USB-C 16P mid-mount",
                  source="service connector inside enclosure"))
    for p_ in ("A1", "A12", "B1", "B12", "SH"):
        j2[p_] += gnd
    for p_ in ("A4", "A9", "B4", "B9"):
        j2[p_] += vbus
    for p_ in ("A6", "B6"):
        j2[p_] += usb_dp
    for p_ in ("A7", "B7"):
        j2[p_] += usb_dm
    j2["A8"] += NC
    j2["B8"] += NC
    for cc in ("A5", "B5"):
        r = res("5.1k", src(source="USB-C CC pull-down, DevKitC-1 ref"))
        r[1] += j2[cc]
        r[2] += gnd

    block("usb_esd")
    u2 = part("Power_Protection", "USBLC6-2SC6", "TBD USB ESD", ESD_FP,
              src(mpn="TBD USBLC6-2SC6-class", mfr="ST",
                  source="USB D+/D- ESD protection, review"))
    u2[1] += usb_dm
    u2[6] += Net("USB_DM_MCU")
    u2[3] += usb_dp
    u2[4] += Net("USB_DP_MCU")
    u2[5] += vbus
    u2[2] += gnd

    # service power: USB 5V -> LDO -> diode-OR with interconnect 3V3
    block("usb_power")
    u3 = part("Regulator_Linear", "AMS1117-3.3", "TBD 3V3 LDO", SOT223,
              src(mpn="TBD AMS1117-3.3-class",
                  source="USB service power, review"))
    u3["VI"] += vbus
    u3["VO"] += usb3v3
    u3["GND"] += gnd
    for net_, val in ((vbus, "10u"), (usb3v3, "22u")):
        c = cap(val, src(source="AMS1117 datasheet caps, review"))
        c[1] += net_
        c[2] += gnd
    for net_ in (usb3v3, int3v3):
        d_or = part("Device", "D_Schottky", "TBD 20V 1A", D_SOD123,
                    src(source="3V3 supply OR-ing, review"))
        d_or[1] += net_
        d_or[2] += v3

    # =======================================================================
    # ESP32-C6-WROOM-1-N8 + EN RC + buttons + LEDs
    # =======================================================================
    block("mcu")
    u1 = part("gateway", "ESP32-C6-WROOM-1", "ESP32-C6-WROOM-1-N8", ESP32_FP,
              src(mpn="ESP32-C6-WROOM-1-N8", mfr="Espressif", lcsc="TBD",
                  source="Espressif datasheet v1.4 Table 3-1"))
    u1["3V3"] += v3
    u1["GND.1"] += gnd
    u1["GND.28"] += gnd
    u1["EPAD"] += gnd
    u1["EN"] += en

    r_en = res("10k", src(source="ESP32-C6 datasheet EN pull-up, review"))
    r_en[1] += v3
    r_en[2] += en
    c_en = cap("1u", src(source="ESP32-C6 datasheet EN RC, review"))
    c_en[1] += en
    c_en[2] += gnd

    for val in ("100n", "10u"):
        c = cap(val, src(source="module decoupling, review"))
        c[1] += v3
        c[2] += gnd

    # RESET button: EN to GND (reboot, configuration kept)
    block("btn_reset")
    sw2 = part("Switch", "SW_Push", "RESET", TACT_6X6,
               src(mpn="TBD tactile 6x6 recessed",
                   source="spec.yaml front_ui.buttons"))
    sw2[1] += en
    sw2[2] += gnd

    # PROG button on IO9 (BOOT strapping pin)
    block("btn_prog")
    sw1 = part("Switch", "SW_Push", "PROG", TACT_6X6,
               src(mpn="TBD tactile 6x6", source="spec.yaml front_ui.buttons"))
    sw1[1] += prog_btn
    sw1[2] += gnd
    r_prog = res("10k", src(source="external pull-up on strapping pin, review"))
    r_prog[1] += v3
    r_prog[2] += prog_btn
    u1["IO9"] += prog_btn

    # status LEDs
    block("led_prog")
    led("PROG", "red", Net("LED_PROG"), gnd)
    block("led_knx")
    led("KNX", "green", Net("LED_KNX"), gnd)
    block("led_dali")
    led("DALI", "yellow", Net("LED_DALI"), gnd)

    # MCU pin mapping from MCU_PINS table
    u1["IO20"] += knx_txd
    u1["IO21"] += knx_rxd
    u1["IO18"] += dali_tx
    u1["IO19"] += dali_rx
    u1["IO10"] += Net("LED_PROG")
    u1["IO11"] += Net("LED_KNX")
    u1["IO2"] += Net("LED_DALI")
    u1["IO6"] += sda
    u1["IO7"] += scl
    u1["IO12"] += Net("USB_DM_MCU")
    u1["IO13"] += Net("USB_DP_MCU")
    u1["IO8"] += spare1
    u1["IO1"] += spare2
    for pin in ("NC", "RXD0", "TXD0", "IO3", "IO0", "IO4", "IO5", "IO15",
                "IO22", "IO23"):
        u1[pin] += NC

    # =======================================================================
    # optional OLED I2C header (not populated in revision A)
    # =======================================================================
    block("oled")
    j3 = part("Connector", "Conn_01x04_Pin", "OLED", HDR_4,
              src(mpn="TBD 2.54mm header", lcsc="TBD",
                  source="spec.yaml front_ui.display — reserved, DNP"))
    j3[1] += gnd
    j3[2] += v3
    j3[3] += scl
    j3[4] += sda
    for net_ in (sda, scl):
        r = res("4.7k", src(source="I2C pull-up, review"))
        r[1] += v3
        r[2] += net_

    # Mounting holes are added directly on the PCB by the pcbnew placement
    # script (pin-less symbols crash SKiDL's schematic placer).


NET_CLASSES = {
    "SELV": ["GND", "+3V3", "+3V3_MCU", "+3V3_USB", "+5V_USB", "EN",
             "KNX_TXD", "KNX_RXD", "DALI_TX", "DALI_RX", "INT_SPARE1",
             "INT_SPARE2", "PROG_BTN", "LED_PROG", "LED_KNX", "LED_DALI",
             "I2C_SDA", "I2C_SCL", "USB_DP", "USB_DM", "USB_DP_MCU",
             "USB_DM_MCU"],
}
