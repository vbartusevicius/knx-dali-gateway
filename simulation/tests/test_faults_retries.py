"""Fault detection (missing gear, lamp failure) and transport retry behaviour."""

import asyncio
import time

from xknx.telegram.apci import DPTBinary

from .helpers import commissioning_command, commissioning_state
from .scenario import CHANNELS, DEVICE_ADDRESS

PHASE_DONE = 5


async def test_fault_object_on_removed_gear(knx, sim):
    """Removing gear 3 makes the background poll flag the fault, which must
    appear as a group telegram on the channel's fault object GA."""
    await sim.request("remove_gear", short=3)
    await knx.wait_telegram(CHANNELS[3]["fault"], lambda v: v == 1, timeout=30)


async def test_lamp_failure_sets_fault(knx, sim):
    await sim.request("lamp_failure", short=2, value=True)
    await knx.wait_telegram(CHANNELS[2]["fault"], lambda v: v == 1, timeout=30)


async def test_lamp_failure_clears(knx, sim):
    await sim.request("lamp_failure", short=2, value=False)
    await knx.wait_telegram(CHANNELS[2]["fault"], lambda v: v == 0, timeout=30)


async def test_single_injected_timeout_still_succeeds(knx, sim):
    """Reads retry once on timeout: a scan survives one injected timeout."""
    await sim.request("inject", timeouts=1)
    response = await commissioning_command(knx, DEVICE_ADDRESS, 1, b"")
    assert response == [0]
    deadline = time.monotonic() + 60
    while True:
        state = await commissioning_state(knx, DEVICE_ADDRESS)
        if state["phase"] == PHASE_DONE:
            break
        assert time.monotonic() < deadline, "scan did not finish"
        await asyncio.sleep(0.3)
    # Gear 0..2 still present after the scan (3 was removed above).
    for short in (0, 1, 2):
        assert state["present"][short // 8] & (1 << (short % 8))


async def test_persistent_timeout_marks_gear_faulty(knx, sim):
    """Gear that never answers becomes faulty and reports on its fault GA."""
    await sim.request("remove_gear", short=0)
    # Scans reflect the missing gear in the presence bitmap...
    await commissioning_command(knx, DEVICE_ADDRESS, 1, b"")
    deadline = time.monotonic() + 60
    while True:
        state = await commissioning_state(knx, DEVICE_ADDRESS)
        if state["phase"] == PHASE_DONE:
            break
        assert time.monotonic() < deadline, "scan did not finish"
        await asyncio.sleep(0.3)
    assert not (state["present"][0] & 0x01)
    # ...and the fault bit was raised on the bus.
    await knx.wait_telegram(CHANNELS[0]["fault"], lambda v: v == 1, timeout=30)


async def test_injected_collision_retried(knx, sim):
    """Collision-injected frames are retried: the command still lands and the
    frame log shows each collided frame followed by a successful retry."""
    await knx.write(CHANNELS[1]["switch"], DPTBinary(0))
    await sim.wait_gear(lambda g: g["short"] == 1 and g["level"] == 0)
    await sim.frames(clear=True)

    await sim.request("inject", collisions=2)
    await knx.write(CHANNELS[1]["switch"], DPTBinary(1))
    await sim.wait_gear(lambda g: g["short"] == 1 and g["level"] > 0)
    await knx.wait_telegram(CHANNELS[1]["switch_status"], lambda v: v == 1)

    frames = await sim.frames()
    collided = [f for f in frames if f["status"] == "collision"]
    assert len(collided) == 2, f"expected 2 collisions in log, got {collided}"
    for entry in collided:
        assert any(
            f["frame"] == entry["frame"]
            and f["status"] == "ok"
            and f["ts"] > entry["ts"]
            for f in frames
        ), f"frame {entry['frame']:#06x} not retried after collision"


async def test_restore_removed_gear(knx, sim):
    """Re-add the gear removed above and rescan so later tests find a full
    bus again (and the fault flags clear)."""
    # Restore the original bus: gear 0 was in group 0, gear 3 in none.
    await sim.request("add_gear", short=0, groups=[0])
    await sim.request("add_gear", short=3, groups=[])
    await commissioning_command(knx, DEVICE_ADDRESS, 1, b"")
    deadline = time.monotonic() + 60
    while True:
        state = await commissioning_state(knx, DEVICE_ADDRESS)
        if state["phase"] == PHASE_DONE:
            break
        assert time.monotonic() < deadline, "scan did not finish"
        await asyncio.sleep(0.3)
    for short in (0, 1, 2, 3):
        assert state["present"][short // 8] & (1 << (short % 8))
    await knx.wait_telegram(CHANNELS[0]["fault"], lambda v: v == 0, timeout=30)
