"""Newline-delimited JSON-over-TCP server for the DALI bus simulator.

Port SIM_PORT (default 5555) serves the gateway: one request per line,
{"op": "frame", "frame": <u16>, "twice": bool, "reply": bool}. The same
connection also carries unsolicited frames in the other direction: a
{"event": <u24>} push models a DALI-2 control-device event, which the
gateway acknowledges with {"op": "event_ack", "frame": <u24>}.

Port CONTROL_PORT (default 5556) serves the test suite: list, add_gear,
remove_gear, lamp_failure, inject, frame_log, event, last_event, reset.
"""

from __future__ import annotations

import asyncio
import json
import logging
import os

from .bus import SimBus
from .device import SimDevice
from .gear import SimGear

logger = logging.getLogger(__name__)


def _bad(reason: str) -> dict:
    return {"status": "error", "reason": reason}


# Gateway connections that receive pushed event frames.
_gateway_writers: set[asyncio.StreamWriter] = set()


def gateway_dispatch(bus: SimBus, request: dict) -> dict:
    op = request.get("op")
    if op == "frame":
        try:
            raw = int(request["frame"])
        except (KeyError, TypeError, ValueError):
            return _bad("frame request needs integer 'frame'")
        return bus.handle_frame(
            raw,
            twice=bool(request.get("twice", False)),
            reply=bool(request.get("reply", False)),
        )
    if op == "event_ack":
        bus.record_event_ack(int(request.get("frame", -1)))
        # Fire-and-forget: no response, so an ack racing an in-flight
        # frame request cannot be mistaken for that request's response.
        return None
    return _bad(f"unknown op {op!r}")


def _push_event(frame: int) -> int:
    line = json.dumps({"event": frame}).encode() + b"\n"
    delivered = 0
    for writer in list(_gateway_writers):
        writer.write(line)
        delivered += 1
    return delivered


def control_dispatch(bus: SimBus, request: dict) -> dict:
    op = request.get("op")
    if op == "list":
        return {"status": "ok", "gear": bus.gear_state()}
    if op == "add_gear":
        return bus.add_gear(
            short=request.get("short"),
            groups=request.get("groups"),
            lamp_failure=bool(request.get("lamp_failure", False)),
            colour=request.get("colour"),
        )
    if op == "remove_gear":
        return bus.remove_gear(int(request["short"]))
    if op == "lamp_failure":
        return bus.set_lamp_failure(int(request["short"]), bool(request["value"]))
    if op == "inject":
        return bus.inject(
            timeouts=int(request.get("timeouts", 0)),
            collisions=int(request.get("collisions", 0)),
        )
    if op == "frame_log":
        return bus.log(clear=bool(request.get("clear", False)))
    if op == "list_devices":
        return {"status": "ok", "devices": bus.device_state()}
    if op == "add_device":
        return bus.add_device(
            short=request.get("short"),
            instances=request.get("instances"),
            resolutions=request.get("resolutions"),
            random_address=request.get("random"),
        )
    if op == "remove_device":
        return bus.remove_device(int(request["short"]))
    if op == "device_event":
        # Emit an event the way the control device would: suppressed while
        # quiescent, disabled or filtered out.
        result = bus.device_event(
            int(request["short"]),
            int(request.get("instance", 0)),
            str(request["event"]),
            int(request.get("value", 0)),
        )
        if result.get("sent"):
            result["delivered"] = _push_event(result["frame"])
        return result
    if op == "event":
        # Push a raw unsolicited 24-bit frame; the payload is opaque to the
        # simulator (test hook bypassing the device model).
        try:
            frame = int(request["frame"])
        except (KeyError, TypeError, ValueError):
            return _bad("event needs integer 'frame'")
        return {"status": "ok", "delivered": _push_event(frame)}
    if op == "last_event":
        return {"status": "ok", "frame": bus.last_event_ack}
    if op == "reset":
        return bus.reset()
    return _bad(f"unknown op {op!r}")


async def _serve(
    bus: SimBus,
    dispatch,
    reader: asyncio.StreamReader,
    writer: asyncio.StreamWriter,
    is_gateway: bool = False,
) -> None:
    peer = writer.get_extra_info("peername")
    logger.info("client connected: %s", peer)
    if is_gateway:
        _gateway_writers.add(writer)
    try:
        while line := await reader.readline():
            try:
                request = json.loads(line)
            except json.JSONDecodeError:
                response = _bad("not JSON")
            else:
                response = dispatch(bus, request)
            if response is not None:
                writer.write(json.dumps(response).encode() + b"\n")
                await writer.drain()
    finally:
        _gateway_writers.discard(writer)
        writer.close()


async def run_server(bus: SimBus, gateway_port: int, control_port: int) -> None:
    gateway = await asyncio.start_server(
        lambda r, w: _serve(bus, gateway_dispatch, r, w, is_gateway=True),
        "0.0.0.0",
        gateway_port,
    )
    control = await asyncio.start_server(
        lambda r, w: _serve(bus, control_dispatch, r, w), "0.0.0.0", control_port
    )
    logger.info(
        "DALI sim up: gateway port %d, control port %d, %d gear",
        gateway_port,
        control_port,
        len(bus.gear),
    )
    async with gateway, control:
        await asyncio.gather(gateway.serve_forever(), control.serve_forever())


def gear_from_env() -> list[SimGear]:
    """SIM_GEAR: JSON list like [{"short": 0, "groups": [1]}, {"short": null}]."""
    spec = os.environ.get("SIM_GEAR", "")
    if spec:
        return [
            SimGear(
                shortaddr=g.get("short"),
                groups=set(g.get("groups") or ()),
                lamp_failure=bool(g.get("lamp_failure", False)),
                colour=g.get("colour"),
            )
            for g in json.loads(spec)
        ]
    count = int(os.environ.get("SIM_GEAR_COUNT", "4"))
    return [SimGear(shortaddr=i) for i in range(count)]


def devices_from_env() -> list[SimDevice]:
    """SIM_DEVICES: JSON list like
    [{"short": 0, "instances": [1,1], "resolutions": [0,0], "random": 4096}].
    """
    spec = os.environ.get("SIM_DEVICES", "")
    if not spec:
        return []
    return [
        SimDevice(
            shortaddr=d.get("short"),
            instance_types=d.get("instances"),
            resolutions=d.get("resolutions"),
            random_address=d.get("random"),
        )
        for d in json.loads(spec)
    ]


def main() -> None:
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(name)s %(message)s")
    bus = SimBus(gear_from_env(), devices_from_env())
    asyncio.run(
        run_server(
            bus,
            gateway_port=int(os.environ.get("SIM_PORT", "5555")),
            control_port=int(os.environ.get("CONTROL_PORT", "5556")),
        )
    )
