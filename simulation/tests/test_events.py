"""Unsolicited 24-bit control-device frames pushed by the simulator.

The simulator's ``event`` control op injects a raw 24-bit frame onto every
gateway connection (the DALI-2 input-device channel). The lab transport
queues each one as a ``BusEvent`` for the worker and acknowledges it back
(``event_ack``), so the simulator's ``last_event`` record proves the frame
crossed the wire and was decoded end-to-end.
"""

from __future__ import annotations

import asyncio
import time

from .helpers import SimClient

# Arbitrary 24-bit value standing in for a control-device event frame.
EVENT_FRAME = 0xFE41AB

POLL_TIMEOUT = 10.0


async def test_gateway_acknowledges_pushed_event(sim: SimClient) -> None:
    pushed = await sim.request("event", frame=EVENT_FRAME)
    assert pushed["delivered"] >= 1, "no gateway connection received the push"

    deadline = time.monotonic() + POLL_TIMEOUT
    while time.monotonic() < deadline:
        last = await sim.request("last_event")
        if last["frame"] == EVENT_FRAME:
            return
        await asyncio.sleep(0.2)
    raise TimeoutError(
        f"gateway never acknowledged the pushed event; last ack: {last['frame']}"
    )
