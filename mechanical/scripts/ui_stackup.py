"""UI board depth (z) from the front-face requirements.

  - OLED glass flush with the front face (0.1 mm below it), its panel passing
    through a window in the front wall, the module board in a pocket behind it
  - PRG/RST and FUNC caps protruding through round holes, so they can be
    pressed with a finger
  - SMD RGB LEDs on the board shining through a thin skin of the front wall,
    each inside its own baffle tube so no light reaches a neighbour's window

Module stack above the UI board, from a typical 0.96" SSD1306 I2C module
(no manufacturer drawing obtained; measure before printing): header spacer
2.5 + module board 1.6 + OLED panel 1.4.
"""

from dataclasses import dataclass

FRONT_OUTER_Z = 61.0
WALL = 1.6
FRONT_INNER_Z = FRONT_OUTER_Z - WALL
BOARD_THICKNESS = 1.6

OLED_HEADER_SPACER = 2.5
OLED_MODULE_BOARD = 1.6
OLED_PANEL = 1.4
OLED_MODULE_STACK = OLED_HEADER_SPACER + OLED_MODULE_BOARD + OLED_PANEL
OLED_GLASS_TARGET_Z = FRONT_OUTER_Z - 0.1
OLED_GLASS_RANGE = (FRONT_OUTER_Z - 0.3, FRONT_OUTER_Z)
POCKET_CLEARANCE = 0.3

# C&K PTS645 SMD heights; only 7.0 mm (SL70, LCSC C285485) is in stock at JLCPCB
TACTILE_LENGTHS = [7.0]
BUTTON_PROTRUSION = (0.5, 2.0)

LED_HEIGHT = 0.65
LED_WINDOW_SKIN = 0.6
LED_SKIN_RANGE = (0.4, 0.8)
LED_BAFFLE_GAP = 0.2


@dataclass
class UIStackup:
    board_z: float
    board_top_z: float
    oled_glass_z: float
    prog_actuator_mm: float
    prog_cap_z: float
    reset_actuator_mm: float
    reset_cap_z: float
    led_skin_mm: float

    @property
    def oled_board_top_z(self):
        return self.oled_glass_z - OLED_PANEL

    @property
    def led_top_z(self):
        return self.board_top_z + LED_HEIGHT

    @property
    def baffle_bottom_z(self):
        return self.board_top_z + LED_BAFFLE_GAP

    @property
    def pocket_depth(self):
        """How far the cover's inner face is recessed over the module board."""
        return max(0.0, self.oled_board_top_z + POCKET_CLEARANCE - FRONT_INNER_Z)

    def fits(self):
        violations = []
        lo, hi = OLED_GLASS_RANGE
        if not lo <= self.oled_glass_z <= hi:
            violations.append(f"OLED glass z={self.oled_glass_z:.2f} outside [{lo:.2f}, {hi:.2f}]")
        if self.pocket_depth > WALL - 0.8:
            violations.append(f"pocket {self.pocket_depth:.2f} leaves less than 0.8 mm of front wall")
        for name, cap_z in (("PRG/RST", self.prog_cap_z), ("FUNC", self.reset_cap_z)):
            lo, hi = (FRONT_OUTER_Z + p for p in BUTTON_PROTRUSION)
            if not lo <= cap_z <= hi:
                violations.append(f"{name} cap z={cap_z:.2f} outside [{lo:.2f}, {hi:.2f}]")
        lo, hi = LED_SKIN_RANGE
        if not lo <= self.led_skin_mm <= hi:
            violations.append(f"LED window skin {self.led_skin_mm:.2f} mm outside [{lo}, {hi}]")
        if self.led_top_z >= FRONT_INNER_Z:
            violations.append("LEDs reach the front wall")
        return not violations, violations


def nearest(target, options):
    return min(options, key=lambda x: abs(x - target))


def compute_ui_stackup():
    board_top_z = OLED_GLASS_TARGET_Z - OLED_MODULE_STACK
    button = nearest(FRONT_OUTER_Z + sum(BUTTON_PROTRUSION) / 2 - board_top_z, TACTILE_LENGTHS)
    return UIStackup(
        board_z=board_top_z - BOARD_THICKNESS,
        board_top_z=board_top_z,
        oled_glass_z=board_top_z + OLED_MODULE_STACK,
        prog_actuator_mm=button,
        prog_cap_z=board_top_z + button,
        reset_actuator_mm=button,
        reset_cap_z=board_top_z + button,
        led_skin_mm=LED_WINDOW_SKIN,
    )


if __name__ == "__main__":
    s = compute_ui_stackup()
    ok, violations = s.fits()
    print(f"UI stack-up {'PASS' if ok else 'FAIL'}: board z {s.board_z:.2f}, top {s.board_top_z:.2f}, "
          f"glass {s.oled_glass_z:.2f}, buttons {s.prog_cap_z:.2f}, LED skin {s.led_skin_mm}, "
          f"pocket {s.pocket_depth:.2f}")
    for v in violations:
        print("  VIOLATION:", v)
