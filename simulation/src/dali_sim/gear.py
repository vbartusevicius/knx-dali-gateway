"""Simulated control gear.

Wraps python-dali's test Gear and adds QUERY STATUS (opcode 0x90), which the
upstream fake does not implement, a lamp-failure fault flag, and configurable
IEC 62386-209 (DT8) colour behaviour beyond the upstream Tc-only stub:
temporary x/y and RGBWAF registers, ACTIVATE, Tc steps, colour status, the
query-colour-value selectors the gateway uses, and colour snapshots in
scenes. Colour opcodes/features follow python-dali's dali/gear/colour.py.
"""

from __future__ import annotations

import dali.gear.colour as colour_cmd
import dali.gear.general as gear_cmd
from dali.gear.colour import QueryColourValueDTR
from dali.tests.fakes import Gear as _FakeGear

# IEC 62386-102 Table 21 statusInformation bits.
STATUS_CONTROL_GEAR_FAILURE = 0x01
STATUS_LAMP_FAILURE = 0x02
STATUS_LAMP_ON = 0x04
STATUS_MISSING_SHORT_ADDRESS = 0x40

# IEC 62386-209 command 249 answer bits.
FEATURE_XY = 0x01
FEATURE_TC = 0x02

# IEC 62386-209 command 248 answer bits.
COLOUR_STATUS_XY_ACTIVE = 0x10
COLOUR_STATUS_TC_ACTIVE = 0x20
COLOUR_STATUS_RGBWAF_ACTIVE = 0x80

# Dimlevel channel value meaning "leave unchanged".
NO_CHANGE = 0xFF

_COLOUR_KINDS = ("tc", "rgbwaf", "xy")


class SimGear(_FakeGear):
    """Fake gear with fault injection and configurable DT8 colour support.

    colour selects the reported colour type features: "tc" (Tc only),
    "rgbwaf" (4 RGBWAF channels), "xy" (xy coordinates) or None (not DT8).
    """

    def __init__(
        self,
        shortaddr: int | None = None,
        groups: set[int] | None = None,
        lamp_failure: bool = False,
        colour: str | None = None,
    ) -> None:
        super().__init__(
            shortaddr=shortaddr,
            groups=set(groups or ()),
            devicetypes=[8] if colour is not None else [],
        )
        self.lamp_failure = lamp_failure
        self.colour_kind = colour
        # Temporaries loaded by SET TEMPORARY * and applied by ACTIVATE.
        self.temp_x = 0
        self.temp_y = 0
        self.temp_rgb = [0, 0, 0]
        self.temp_waf = [0, 0, 0]
        self._temp_dirty: set[str] = set()
        # Activated colour values; actual_ct lives on the upstream gear.
        self.actual_ct = (self.ct_mired_min + self.ct_mired_max) // 2
        self.actual_x = 0
        self.actual_y = 0
        self.actual_rgb = [0, 0, 0]
        self.actual_waf = [0, 0, 0]
        self.active_colour: str | None = None
        # Colour snapshot taken at STORE DTR AS SCENE (None = no colour).
        self.scene_colours: list[dict | None] = [None] * 16

    @property
    def dt8(self) -> bool:
        return 8 in self.devicetypes

    def _features_byte(self) -> int:
        if self.colour_kind == "tc":
            return FEATURE_TC
        if self.colour_kind == "xy":
            return FEATURE_XY
        if self.colour_kind == "rgbwaf":
            return 4 << 5  # four RGBWAF channels
        return 0

    def _colour_status_byte(self) -> int:
        if self.active_colour == "tc":
            return COLOUR_STATUS_TC_ACTIVE
        if self.active_colour == "xy":
            return COLOUR_STATUS_XY_ACTIVE
        if self.active_colour == "rgbwaf":
            return COLOUR_STATUS_RGBWAF_ACTIVE
        return 0

    def status_byte(self) -> int:
        status = 0
        if self.lamp_failure:
            status |= STATUS_LAMP_FAILURE
        if self.level > 0:
            status |= STATUS_LAMP_ON
        if self.shortaddr is None:
            status |= STATUS_MISSING_SHORT_ADDRESS
        return status

    def colour_state(self) -> dict:
        """Colour fields exposed to the test control API."""
        return {
            "colour": self.colour_kind,
            "active_colour": self.active_colour,
            "actual_tc_mirek": self.actual_ct,
            "actual_x": self.actual_x,
            "actual_y": self.actual_y,
            "actual_rgb": list(self.actual_rgb),
            "actual_waf": list(self.actual_waf),
            "scene_colours": [s is not None for s in self.scene_colours],
        }

    # -- DT8 command handling ----------------------------------------------

    def _apply_channel(self, actual: list[int], temp: list[int]) -> None:
        for i, value in enumerate(temp):
            if value != NO_CHANGE:
                actual[i] = value

    def _activate(self) -> None:
        # ACTIVATE applies every pending temporary; the gateway writes one
        # colour type's temporaries at a time, so at most one kind is dirty.
        for kind in sorted(self._temp_dirty):
            if kind == "tc":
                self.actual_ct = min(max(self.temp_ct, self.ct_mired_min),
                                     self.ct_mired_max)
            elif kind == "xy":
                self.actual_x = self.temp_x
                self.actual_y = self.temp_y
            elif kind == "rgbwaf":
                self._apply_channel(self.actual_rgb, self.temp_rgb)
                self._apply_channel(self.actual_waf, self.temp_waf)
        if self._temp_dirty:
            self.active_colour = sorted(self._temp_dirty)[-1]
        self._temp_dirty.clear()

    def _temp_snapshot(self) -> dict:
        return {
            "tc": self.temp_ct,
            "x": self.temp_x,
            "y": self.temp_y,
            "rgb": list(self.temp_rgb),
            "waf": list(self.temp_waf),
            "active": self.active_colour,
        }

    def _query_colour_value(self) -> int | None:
        """QUERY COLOUR VALUE: reply is the MSB, LSB goes to DTR0 (Table 11)."""
        selector = self.dtr0
        word: int | None = None
        byte: int | None = None
        if selector == QueryColourValueDTR.XCoordinate:
            word = self.actual_x
        elif selector == QueryColourValueDTR.YCoordinate:
            word = self.actual_y
        elif selector == QueryColourValueDTR.ColourTemperatureTC:
            word = self.actual_ct
        elif selector in (
            QueryColourValueDTR.RedDimLevel,
            QueryColourValueDTR.GreenDimLevel,
            QueryColourValueDTR.BlueDimLevel,
        ):
            byte = self.actual_rgb[selector - QueryColourValueDTR.RedDimLevel]
        elif selector in (
            QueryColourValueDTR.WhiteDimLevel,
            QueryColourValueDTR.AmberDimLevel,
            QueryColourValueDTR.FreecolourDimLevel,
        ):
            byte = self.actual_waf[selector - QueryColourValueDTR.WhiteDimLevel]
        elif selector == QueryColourValueDTR.TemporaryXCoordinate:
            word = self.temp_x
        elif selector == QueryColourValueDTR.TemporaryYCoordinate:
            word = self.temp_y
        elif selector == QueryColourValueDTR.TemporaryColourTemperature:
            word = self.temp_ct
        elif selector == QueryColourValueDTR.ReportXCoordinate:
            word = self.actual_x
        elif selector == QueryColourValueDTR.ReportYCoordinate:
            word = self.actual_y
        elif selector == QueryColourValueDTR.ReportColourTemperatureTc:
            word = self.actual_ct
        elif selector in (
            QueryColourValueDTR.ReportRedDimLevel,
            QueryColourValueDTR.ReportGreenDimLevel,
            QueryColourValueDTR.ReportBlueDimLevel,
        ):
            byte = self.actual_rgb[selector - QueryColourValueDTR.ReportRedDimLevel]
        elif selector in (
            QueryColourValueDTR.ReportWhiteDimLevel,
            QueryColourValueDTR.ReportAmberDimLevel,
            QueryColourValueDTR.ReportFreecolourDimLevel,
        ):
            byte = self.actual_waf[
                selector - QueryColourValueDTR.ReportWhiteDimLevel
            ]
        if word is not None:
            self.dtr0 = word & 0xFF
            return (word >> 8) & 0xFF
        if byte is not None:
            return byte
        return NO_CHANGE  # MASK for unimplemented selectors

    def _copy_report_to_temporary(self) -> None:
        self.temp_ct = self.actual_ct
        self.temp_x = self.actual_x
        self.temp_y = self.actual_y
        self.temp_rgb = list(self.actual_rgb)
        self.temp_waf = list(self.actual_waf)

    def _colour_command(self, cmd) -> int | None:
        """One DT8 application-extended command; None = no reply."""
        if isinstance(cmd, colour_cmd.QueryColourTypeFeatures):
            return self._features_byte()
        if isinstance(cmd, colour_cmd.QueryColourStatus):
            return self._colour_status_byte()
        if isinstance(cmd, colour_cmd.QueryColourValue):
            return self._query_colour_value()
        if isinstance(cmd, colour_cmd.QueryExtendedVersionNumber):
            return 2
        if isinstance(cmd, colour_cmd.SetTemporaryXCoordinate):
            self.temp_x = self.dtr0 | self.dtr1 << 8
            self._temp_dirty.add("xy")
        elif isinstance(cmd, colour_cmd.SetTemporaryYCoordinate):
            self.temp_y = self.dtr0 | self.dtr1 << 8
            self._temp_dirty.add("xy")
        elif isinstance(cmd, colour_cmd.SetTemporaryColourTemperature):
            self.temp_ct = self.dtr0 | self.dtr1 << 8
            self._temp_dirty.add("tc")
        elif isinstance(cmd, colour_cmd.SetTemporaryRGBDimLevel):
            self.temp_rgb = [self.dtr0, self.dtr1, self.dtr2]
            self._temp_dirty.add("rgbwaf")
        elif isinstance(cmd, colour_cmd.SetTemporaryWAFDimLevel):
            self.temp_waf = [self.dtr0, self.dtr1, self.dtr2]
            self._temp_dirty.add("rgbwaf")
        elif isinstance(cmd, colour_cmd.Activate):
            self._activate()
        elif isinstance(cmd, colour_cmd.ColourTemperatureTcStepCooler):
            # Cooler = higher Kelvin = lower mirek; one mirek per step.
            self.actual_ct = max(self.ct_mired_min, self.actual_ct - 1)
            self.active_colour = "tc"
        elif isinstance(cmd, colour_cmd.ColourTemperatureTcStepWarmer):
            self.actual_ct = min(self.ct_mired_max, self.actual_ct + 1)
            self.active_colour = "tc"
        elif isinstance(cmd, colour_cmd.CopyReportToTemporary):
            self._copy_report_to_temporary()
        elif isinstance(cmd, colour_cmd.StoreColourTemperatureTcLimit):
            # DTR2 selects the limit; physical limits are factory data.
            limit = self.dtr0 | self.dtr1 << 8
            if self.dtr2 == 0:
                self.ct_mired_min = limit
            elif self.dtr2 == 1:
                self.ct_mired_max = limit
        # Unhandled extended commands are ignored, like unsupported gear.
        return None

    def _recall_scene_colour(self, scene: int) -> None:
        snapshot = self.scene_colours[scene]
        if snapshot is None:
            return
        self.actual_ct = snapshot["tc"]
        self.actual_x = snapshot["x"]
        self.actual_y = snapshot["y"]
        self.actual_rgb = list(snapshot["rgb"])
        self.actual_waf = list(snapshot["waf"])
        self.active_colour = snapshot["active"]

    def send(self, cmd):
        if isinstance(cmd, gear_cmd.QueryStatus) and self.valid_address(cmd):
            self.prev_cmd = cmd
            return self.status_byte()
        if isinstance(cmd, gear_cmd.StoreActualLevelInDTR0) and self.valid_address(
            cmd
        ):
            # Not implemented by the upstream fake: load DTR0 with the actual
            # level so STORE DTR AS SCENE persists it.
            self.dtr0 = self.level
            return None
        if isinstance(cmd, colour_cmd._ColourCommand):
            if self.dt8 and self.valid_address(cmd):
                self.prev_cmd = cmd
                return self._colour_command(cmd)
            # Non-DT8 gear ignores application-extended commands; upstream
            # does the same via its `8 in devicetypes` gate.
            return super().send(cmd)
        if self.dt8 and self.valid_address(cmd):
            if isinstance(cmd, gear_cmd.SetScene):
                self.scene_colours[cmd.param] = self._temp_snapshot()
            elif isinstance(cmd, gear_cmd.RemoveFromScene):
                self.scene_colours[cmd.param] = None
            elif isinstance(cmd, gear_cmd.GoToScene):
                self._recall_scene_colour(cmd.param)
        return super().send(cmd)
