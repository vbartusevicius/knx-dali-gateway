"""Simulated DALI-2 control device (IEC 62386-103 + parts 301/303/304).

Wraps python-dali's test Device and adds what the upstream fake does not
model: the Part 103 special commands behind random-address commissioning
(INITIALISE / RANDOMISE / COMPARE / WITHDRAW / PROGRAM / VERIFY), quiescent
mode, per-instance enable flags, the full 24-bit event filter (the upstream
fake stores only DTR0's low byte), instance resolutions, and event emission
that honours quiescent, enable and filter state like a real device.
"""

from __future__ import annotations

import random

import dali.device.general as dev_cmd
import dali.device.light as light_cmd
import dali.device.occupancy as occ_cmd
import dali.device.pushbutton as pb_cmd
from dali import address
from dali.tests.fakes import Device as _FakeDevice

_yes = 0xFF
_mask = 0xFF  # "MASK" answer: no short address set


class SimDevice(_FakeDevice):
    """Fake control device with configurable instances and commissioning.

    instance_types: per-instance IEC 62386-103 instance type codes
    (1 push button, 3 occupancy, 4 light). resolutions is per-instance too
    (light instances report their measurement resolution). random_address
    fixes the value RANDOMISE draws so tests stay deterministic.
    """

    def __init__(
        self,
        shortaddr: int | None = None,
        instance_types: list[int] | None = None,
        resolutions: list[int] | None = None,
        random_address: int | None = None,
    ) -> None:
        super().__init__(
            shortaddr=None if shortaddr is None else address.DeviceShort(shortaddr)
        )
        types = list(instance_types) if instance_types is not None else [1] * 4
        # Shadow the class-level default (4 push buttons, device/instance
        # scheme): each SimDevice owns its own instance list.
        self._instances = [
            _FakeDevice.Instance(
                inst_type=t,
                scheme=int(dev_cmd.EventScheme.device_instance),
                filter=0,
                enabled=True,
            )
            for t in types
        ]
        self.resolutions = list(resolutions or [0] * len(types))
        self.resolutions += [0] * (len(types) - len(self.resolutions))
        self.pending_random = random_address
        self.random_address: int | None = None
        self.search_address = 0
        self.initialising = False
        self.withdrawn = False
        self.quiescent = False

    # -- event emission ------------------------------------------------------

    # Event name -> (event constructor args builder, filter bit mask).
    _BUTTON_EVENTS = {
        "released": (pb_cmd.ButtonReleased, 0x01),
        "pressed": (pb_cmd.ButtonPressed, 0x02),
        "short_press": (pb_cmd.ShortPress, 0x04),
        "double_press": (pb_cmd.DoublePress, 0x08),
        "long_press_start": (pb_cmd.LongPressStart, 0x10),
        "long_press_repeat": (pb_cmd.LongPressRepeat, 0x20),
        "long_press_stop": (pb_cmd.LongPressStop, 0x40),
        "button_free": (pb_cmd.ButtonFree, 0x80),
        "button_stuck": (pb_cmd.ButtonStuck, 0x80),
    }
    _OCCUPANCY_EVENTS = {
        "occupied": (0b0010, 0x01),
        "vacant": (0b0000, 0x02),
        "movement": (0b0001, 0x08),
        "no_movement": (0b0000, 0x10),
    }

    def emit_event(self, instance: int, event: str, value: int = 0):
        """Build the event frame a real device would transmit, or None when
        quiescent / disabled / filtered out (which is what the lab's
        suppression tests check)."""
        if self.shortaddr is None or instance >= len(self._instances):
            return None, "no such instance"
        inst = self._instances[instance]
        if self.quiescent:
            return None, "quiescent"
        if not inst.enabled:
            return None, "disabled"

        frame = None
        if event in self._BUTTON_EVENTS and inst.inst_type == 1:
            cls, mask = self._BUTTON_EVENTS[event]
            if not inst.filter & mask:
                return None, "filtered"
            frame = cls(
                short_address=self.shortaddr.address, instance_number=instance
            ).frame.as_integer
        elif event in self._OCCUPANCY_EVENTS and inst.inst_type == 3:
            bits, mask = self._OCCUPANCY_EVENTS[event]
            if not inst.filter & mask:
                return None, "filtered"
            data = occ_cmd.OccupancyEvent.EventData(
                movement=bool(bits & 0b0001),
                occupied=bool(bits & 0b0010),
                repeat=bool(bits & 0b0100),
                sensor_type="movement" if bits & 0b1000 else "presence",
            )
            frame = occ_cmd.OccupancyEvent(
                short_address=self.shortaddr.address,
                instance_number=instance,
                data=data,
            ).frame.as_integer
        elif event == "illuminance" and inst.inst_type == 4:
            if not inst.filter & 0x01:
                return None, "filtered"
            frame = light_cmd.LightEvent(
                short_address=self.shortaddr.address,
                instance_number=instance,
                data=value & 0x3FF,
            ).frame.as_integer
        else:
            return None, f"no {event!r} on instance type {inst.inst_type}"
        return frame, None

    # -- command handling ------------------------------------------------------

    def _handle_special(self, cmd: dev_cmd._SpecialDeviceCommand):
        """Part 103 special commands the upstream fake does not model."""
        if isinstance(cmd, dev_cmd.Terminate):
            self.initialising = False
            self.withdrawn = False
        elif isinstance(cmd, dev_cmd.Initialise):
            param = cmd.param
            if (
                param == 0x00
                or (param == 0xFF and self.shortaddr is None)
                or (
                    self.shortaddr is not None
                    and (param >> 1) & 0x3F == self.shortaddr.address
                )
            ):
                self.initialising = True
                self.withdrawn = False
        elif isinstance(cmd, dev_cmd.Randomise):
            self.random_address = (
                self.pending_random
                if self.pending_random is not None
                else random.randrange(0, 0x1000000)
            )
        elif isinstance(cmd, dev_cmd.Compare):
            if (
                self.initialising
                and not self.withdrawn
                and self.random_address is not None
                and self.random_address <= self.search_address
            ):
                return _yes
        elif isinstance(cmd, dev_cmd.Withdraw):
            if self.initialising and self.random_address == self.search_address:
                self.withdrawn = True
        elif isinstance(cmd, dev_cmd.SearchAddrH):
            self.search_address = (self.search_address & 0x00FFFF) | (cmd.param << 16)
        elif isinstance(cmd, dev_cmd.SearchAddrM):
            self.search_address = (self.search_address & 0xFF00FF) | (cmd.param << 8)
        elif isinstance(cmd, dev_cmd.SearchAddrL):
            self.search_address = (self.search_address & 0xFFFF00) | cmd.param
        elif isinstance(cmd, dev_cmd.ProgramShortAddress):
            if self.initialising and self.random_address == self.search_address:
                self.shortaddr = address.DeviceShort((cmd.param >> 1) & 0x3F)
        elif isinstance(cmd, dev_cmd.VerifyShortAddress):
            if (
                self.initialising
                and self.shortaddr is not None
                and self.shortaddr.address == (cmd.param >> 1) & 0x3F
            ):
                return _yes
        elif isinstance(cmd, dev_cmd.QueryShortAddress):
            if self.shortaddr is not None:
                return (self.shortaddr.address << 1) | 1
            return _mask
        return None

    _COMMISSIONING_SPECIALS = (
        dev_cmd.Terminate,
        dev_cmd.Initialise,
        dev_cmd.Randomise,
        dev_cmd.Compare,
        dev_cmd.Withdraw,
        dev_cmd.SearchAddrH,
        dev_cmd.SearchAddrM,
        dev_cmd.SearchAddrL,
        dev_cmd.ProgramShortAddress,
        dev_cmd.VerifyShortAddress,
        dev_cmd.QueryShortAddress,
    )

    def send(self, cmd):
        if isinstance(cmd, self._COMMISSIONING_SPECIALS):
            return self._handle_special(cmd)

        # Device-wide standard commands handled before the upstream table.
        if isinstance(cmd, dev_cmd.StartQuiescentMode) and self.valid_address(cmd):
            self.quiescent = True
            return None
        if isinstance(cmd, dev_cmd.StopQuiescentMode) and self.valid_address(cmd):
            self.quiescent = False
            return None
        if isinstance(cmd, dev_cmd.QueryQuiescentMode) and self.valid_address(cmd):
            return _yes if self.quiescent else None

        # Instance-number-addressed commands the upstream fake lacks.
        if (
            self.valid_address(cmd)
            and hasattr(cmd, "instance")
            and isinstance(cmd.instance, address.InstanceNumber)
            and cmd.instance.value < len(self._instances)
        ):
            inst = self._instances[cmd.instance.value]
            if isinstance(cmd, dev_cmd.EnableInstance):
                inst.enabled = True
                return None
            if isinstance(cmd, dev_cmd.DisableInstance):
                inst.enabled = False
                return None
            if isinstance(cmd, dev_cmd.SetEventFilter):
                inst.filter = self.dtr0 | (self.dtr1 << 8) | (self.dtr2 << 16)
                return None
            if isinstance(cmd, dev_cmd.QueryEventFilterEightToFifteen):
                return (inst.filter >> 8) & 0xFF
            if isinstance(cmd, dev_cmd.QueryEventFilterSixteenToTwentyThree):
                return (inst.filter >> 16) & 0xFF
            if isinstance(cmd, dev_cmd.QueryResolution):
                return self.resolutions[cmd.instance.value]

        return super().send(cmd)

    # -- introspection for the control API -------------------------------------

    def state(self) -> dict:
        return {
            "short": None if self.shortaddr is None else self.shortaddr.address,
            "quiescent": self.quiescent,
            "instances": [
                {
                    "type": i.inst_type,
                    "scheme": i.scheme,
                    "filter": i.filter,
                    "enabled": i.enabled,
                }
                for i in self._instances
            ],
        }
