"""Commissioning through the KNX function properties (object 160)."""

import asyncio
import time

import pytest

from .helpers import commissioning_command, commissioning_state
from .scenario import DEVICE_ADDRESS

# commissioning::Phase in the order defined by the C++ service.
PHASE_DONE = 5
PHASE_FAILED = 6

RESPONSE_ACCEPTED = 0


async def _wait_phase(knx, want=(PHASE_DONE,), timeout=30):
    deadline = time.monotonic() + timeout
    while True:
        state = await commissioning_state(knx, DEVICE_ADDRESS)
        if state["phase"] in want:
            return state
        if time.monotonic() > deadline:
            raise TimeoutError(f"phase {state['phase']} not in {want}: {state}")
        await asyncio.sleep(0.3)


async def test_startup_state(knx):
    """The worker scanned at boot: phase Done, all four gear present."""
    state = await commissioning_state(knx, DEVICE_ADDRESS)
    assert state["phase"] == PHASE_DONE
    for short in range(4):
        assert state["present"][short // 8] & (1 << (short % 8)), (
            f"gear {short} not present in {state['present'].hex()}"
        )


async def test_scan_command(knx, sim):
    response = await commissioning_command(knx, DEVICE_ADDRESS, 1, b"")
    assert response == [RESPONSE_ACCEPTED]
    state = await _wait_phase(knx)
    assert state["phase"] == PHASE_DONE


async def test_assign_unaddressed(knx, sim):
    await sim.request("add_gear", short=None, groups=[])
    response = await commissioning_command(knx, DEVICE_ADDRESS, 2, bytes([0]))
    assert response == [RESPONSE_ACCEPTED]
    state = await _wait_phase(knx, timeout=60)
    assert state["phase"] == PHASE_DONE
    assert state["found"] >= 1
    gear = await sim.wait_gear(lambda g: g["short"] == 4, timeout=10)


async def test_replace_missing_gear_keeps_channel(knx, sim):
    """Remove the gear on short address 1, add an unaddressed one, then
    Replace: the new gear must take over short address 1. A rescan first
    updates the gateway's presence bitmap (poll only flips the fault bit)."""
    await sim.request("remove_gear", short=1)
    # No group membership on the new gear - reconcile_groups must write it.
    await sim.request("add_gear", short=None, groups=[])

    response = await commissioning_command(knx, DEVICE_ADDRESS, 1, b"")
    assert response == [RESPONSE_ACCEPTED]
    state = await _wait_phase(knx, timeout=60)
    assert state["phase"] == PHASE_DONE
    assert not (state["present"][0] & 0x02), "gear 1 still marked present"

    response = await commissioning_command(knx, DEVICE_ADDRESS, 3, bytes([1]))
    assert response == [RESPONSE_ACCEPTED]
    state = await _wait_phase(knx, timeout=60)
    assert state["phase"] == PHASE_DONE

    gear = await sim.wait_gear(lambda g: g["short"] == 1, timeout=10)
    assert 0 in gear["groups"], "replaced gear should be re-added to group 0"


async def test_replace_present_address_rejected(knx, sim):
    response = await commissioning_command(knx, DEVICE_ADDRESS, 3, bytes([0]))
    # Accepted as a command; the job itself fails because address 0 is present.
    assert response == [RESPONSE_ACCEPTED]
    state = await _wait_phase(knx, want=(PHASE_DONE, PHASE_FAILED), timeout=30)
    assert state["phase"] == PHASE_FAILED
