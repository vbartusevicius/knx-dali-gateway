"""Simulated DALI bus built on python-dali's test gear and devices.

Handles decoded 16-bit forward frames — including the Part 102 special
commands the gateway's own GearAddressing sequence is built from — and
24-bit control-device frames (Part 103 commands; SimDevice implements the
random-address search). Several units answering the same forward frame
collide into a framing error. Fault injection drops or corrupts the next N
frames.
"""

from __future__ import annotations

import copy
import time
from dataclasses import dataclass

import dali.command
import dali.device.general  # registers device decoding
import dali.device.light
import dali.device.occupancy
import dali.device.pushbutton
import dali.gear.general  # registers command decoding
from dali.frame import BackwardFrame, BackwardFrameError, ForwardFrame

from .device import SimDevice
from .gear import SimGear


@dataclass
class FrameLogEntry:
    frame: int
    command: str
    reply: int | None
    status: str
    timestamp: float

    def as_dict(self) -> dict:
        return {
            "frame": self.frame,
            "command": self.command,
            "reply": self.reply,
            "status": self.status,
            "ts": self.timestamp,
        }


class SimBus:
    """Collection of SimGear plus the addressing/commissioning sequence."""

    def __init__(
        self,
        gear: list[SimGear] | None = None,
        devices: list[SimDevice] | None = None,
    ) -> None:
        self.initial_gear = copy.deepcopy(list(gear or []))
        self.gear: list[SimGear] = list(gear or [])
        self.initial_devices = copy.deepcopy(list(devices or []))
        self.devices: list[SimDevice] = list(devices or [])
        self.inject_timeouts = 0
        self.inject_collisions = 0
        self.frame_log: list[FrameLogEntry] = []
        # ENABLE DEVICE TYPE arms the next application-extended command
        # (IEC 62386-102); used as the devicetype decode hint for from_frame.
        self._pending_devicetype = 0
        # Last pushed 24-bit event the gateway acknowledged (event_ack op).
        self.last_event_ack: int | None = None

    # -- frame dispatch ----------------------------------------------------

    def _send(self, cmd) -> BackwardFrame | None:
        """Send one command to all gear, collating backward frames.

        Mirrors dali.tests.fakes.Bus.send but keeps the raw backward frame so
        collisions stay detectable.
        """
        replies = [r for r in (g.send(cmd) for g in self.gear) if r is not None]
        if len(replies) > 1:
            return BackwardFrameError(replies[0])
        if replies:
            return BackwardFrame(replies[0])
        return None

    def _send_twice_aware(self, cmd, twice: bool) -> BackwardFrame | None:
        times = 2 if (twice or getattr(cmd, "sendtwice", False)) else 1
        reply = None
        for _ in range(times):
            reply = self._send(cmd)
        return reply

    def _send_devices(self, cmd) -> BackwardFrame | None:
        replies = [
            r for r in (d.send(cmd) for d in self.devices) if r is not None
        ]
        if len(replies) > 1:
            return BackwardFrameError(replies[0])
        if replies:
            return BackwardFrame(replies[0])
        return None

    def handle_frame(self, raw: int, twice: bool, reply: bool) -> dict:
        """Process one gateway frame request; returns the response dict."""
        if self.inject_timeouts > 0:
            self.inject_timeouts -= 1
            self._log(raw, None, None, "timeout")
            return {"status": "timeout", "reply": None}
        if self.inject_collisions > 0:
            self.inject_collisions -= 1
            self._log(raw, None, None, "collision")
            return {"status": "collision", "reply": None, "kind": "forward"}

        if raw > 0xFFFF:
            # 24-bit control-device frame (Part 103). The same double-send /
            # reply-collision rules apply; gear never see these.
            cmd = dali.command.from_frame(ForwardFrame(24, raw))
            times = 2 if (twice or getattr(cmd, "sendtwice", False)) else 1
            backward = None
            for _ in range(times):
                backward = self._send_devices(cmd)
            return self._frame_result(raw, cmd, backward, reply)

        devicetype = self._pending_devicetype
        self._pending_devicetype = 0
        if (raw >> 8) == 0xC1:
            # ENABLE DEVICE TYPE itself; decode normally and arm the next one.
            self._pending_devicetype = raw & 0xFF
            devicetype = 0
        cmd = dali.command.from_frame(ForwardFrame(16, raw), devicetype=devicetype)
        backward = self._send_twice_aware(cmd, twice)
        return self._frame_result(raw, cmd, backward, reply)

    def _frame_result(self, raw: int, cmd, backward, reply: bool) -> dict:

        status = "ok"
        reply_value: int | None = None
        if isinstance(backward, BackwardFrameError):
            # Several gear answered at once: their backward frames collide
            # on the wire, but the forward frame itself arrived — a reply
            # collision is not retransmitted on a real bus.
            status = "collision"
        elif backward is None:
            if reply:
                status = "timeout"
        else:
            reply_value = backward.as_integer
        self._log(raw, cmd, reply_value, status)
        response = {"status": status, "reply": reply_value}
        if status == "collision":
            response["kind"] = "reply"
        return response

    def _log(self, raw: int, cmd, reply: int | None, status: str) -> None:
        self.frame_log.append(
            FrameLogEntry(
                frame=raw,
                command=str(cmd) if cmd is not None else "",
                reply=reply,
                status=status,
                timestamp=time.monotonic(),
            )
        )

    # -- test control ------------------------------------------------------

    def record_event_ack(self, frame: int) -> dict:
        self.last_event_ack = frame
        return {"status": "ok"}

    def gear_state(self) -> list[dict]:
        return [
            {
                "index": i,
                "short": g.shortaddr,
                "level": g.level,
                "groups": sorted(g.groups),
                "scenes": list(g.scenes),
                "lamp_failure": g.lamp_failure,
                **g.colour_state(),
            }
            for i, g in enumerate(self.gear)
        ]

    def add_gear(
        self,
        short: int | None,
        groups: list[int] | None,
        lamp_failure: bool = False,
        colour: str | None = None,
    ) -> dict:
        self.gear.append(
            SimGear(
                shortaddr=short,
                groups=set(groups or ()),
                lamp_failure=lamp_failure,
                colour=colour,
            )
        )
        return {"status": "ok", "index": len(self.gear) - 1}

    def remove_gear(self, short: int) -> dict:
        for i, g in enumerate(self.gear):
            if g.shortaddr == short:
                del self.gear[i]
                return {"status": "ok", "removed": short}
        return {"status": "error", "reason": "no gear with that short address"}

    def set_lamp_failure(self, short: int, value: bool) -> dict:
        for g in self.gear:
            if g.shortaddr == short:
                g.lamp_failure = value
                return {"status": "ok"}
        return {"status": "error", "reason": "no gear with that short address"}

    def inject(self, timeouts: int = 0, collisions: int = 0) -> dict:
        self.inject_timeouts += timeouts
        self.inject_collisions += collisions
        return {"status": "ok"}

    # -- control devices -----------------------------------------------------

    def _device(self, short: int) -> SimDevice | None:
        return next(
            (
                d
                for d in self.devices
                if d.shortaddr is not None and d.shortaddr.address == short
            ),
            None,
        )

    def device_state(self) -> list[dict]:
        return [d.state() for d in self.devices]

    def add_device(
        self,
        short: int | None,
        instances: list[int] | None,
        resolutions: list[int] | None = None,
        random_address: int | None = None,
    ) -> dict:
        self.devices.append(
            SimDevice(
                shortaddr=short,
                instance_types=instances,
                resolutions=resolutions,
                random_address=random_address,
            )
        )
        return {"status": "ok", "index": len(self.devices) - 1}

    def remove_device(self, short: int) -> dict:
        for i, d in enumerate(self.devices):
            if d.shortaddr is not None and d.shortaddr.address == short:
                del self.devices[i]
                return {"status": "ok", "removed": short}
        return {"status": "error", "reason": "no device with that short address"}

    def device_event(
        self, short: int, instance: int, event: str, value: int = 0
    ) -> dict:
        """The event frame the device would put on the wire, honouring its
        quiescent / enable / filter state."""
        device = self._device(short)
        if device is None:
            return {"status": "error", "reason": "no device with that short address"}
        frame, reason = device.emit_event(instance, event, value)
        if frame is None:
            return {"status": "ok", "sent": False, "reason": reason}
        return {"status": "ok", "sent": True, "frame": frame}

    def log(self, clear: bool = False) -> dict:
        entries = [e.as_dict() for e in self.frame_log]
        if clear:
            self.frame_log.clear()
        return {"status": "ok", "frames": entries}

    def reset(self) -> dict:
        """Restore the startup gear/device set and clear faults and the log."""
        self.gear = copy.deepcopy(self.initial_gear)
        self.devices = copy.deepcopy(self.initial_devices)
        self.inject_timeouts = 0
        self.inject_collisions = 0
        self.frame_log.clear()
        self._pending_devicetype = 0
        self.last_event_ack = None
        return {"status": "ok"}
