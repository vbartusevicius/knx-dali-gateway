"""DALI-2 control devices: commissioning, instance configuration, and event
mapping to KNX telegrams (IEC 62386-103 + parts 301/303/304).

The simulator's SimDevice models the Part 103 special commands, per-instance
scheme/filter/enable state and quiescent mode; event frames are built by
python-dali's own event classes so the wire encoding comes from the
independent implementation, not our decoder's assumptions.
"""

import asyncio
import time

from xknx.telegram.apci import DPTBinary

from .helpers import (
    commissioning_command,
    input_state,
)
from .scenario import DEVICE_ADDRESS, INPUTS

RESPONSE_ACCEPTED = 0

# CommandProperty / CommissionMode values (CommissioningProtocol.hpp).
PROP_SCAN_INPUTS = 6
PROP_ASSIGN_INPUTS = 7
MODE_UNADDRESSED = 0


def _event_frame(short: int, instance: int, info: int) -> int:
    """Device/instance-scheme event frame (bit15 = 1, bits 22:17 = device
    short address, bits 14:10 = instance, bits 9:0 = event info)."""
    return (short << 17) | (1 << 15) | (instance << 10) | info


def _value(telegram):
    value = telegram.payload.value
    raw = getattr(value, "value", value)
    if isinstance(raw, tuple):
        return list(raw)
    return raw


async def test_input_devices_discovered_and_configured_at_startup(knx, sim):
    """State property 2 reports the three provisioned devices; the fake
    shows the scheme/filter/enable the inventory applied."""
    state = await input_state(knx, DEVICE_ADDRESS)
    for short in (0, 1, 2):
        assert state["present"][short // 8] & (1 << (short % 8)), (
            f"device {short} not present in {state['present'].hex()}"
        )
    # summary = instance count | type flags (bit5 button, bit6 occ, bit7 lux)
    assert state["summary"][0] == 0x20 | 4
    assert state["summary"][1] == 0x40 | 2
    assert state["summary"][2] == 0x80 | 1

    devices = await sim.devices()
    by_short = {d["short"]: d for d in devices}
    # Device/instance scheme (2) everywhere; bound instances enabled,
    # unbound instances disabled (device 0 inst 3, device 1 inst 1).
    assert [i["scheme"] for i in by_short[0]["instances"]] == [2] * 4
    assert [i["enabled"] for i in by_short[0]["instances"]] == [
        True,
        True,
        True,
        False,
    ]
    assert [i["enabled"] for i in by_short[1]["instances"]] == [True, False]
    assert by_short[2]["instances"][0]["enabled"] is True
    # Type-default filters applied (301 = 0xF4, 303 = 0x1B, 304 = 0x01).
    assert by_short[0]["instances"][0]["filter"] == 0xF4
    assert by_short[1]["instances"][0]["filter"] == 0x1B
    assert by_short[2]["instances"][0]["filter"] == 0x01


async def test_scan_inputs_command(knx, sim):
    response = await commissioning_command(
        knx, DEVICE_ADDRESS, PROP_SCAN_INPUTS, b""
    )
    assert response == [RESPONSE_ACCEPTED]
    deadline = time.monotonic() + 15
    while True:
        state = await input_state(knx, DEVICE_ADDRESS)
        if state["present"][0] & 0x07 == 0x07:
            return
        assert time.monotonic() < deadline, f"input scan state {state}"


async def test_button_short_press_toggles_switch(knx, sim):
    ga = INPUTS[0]["switch"]
    first = await sim.device_event(0, 0, "short_press")
    assert first["sent"], first
    telegram = await knx.wait_telegram(ga, timeout=10)
    value1 = _value(telegram)

    await sim.device_event(0, 0, "short_press")
    telegram = await knx.wait_telegram(ga, lambda v: v != value1, timeout=10)


async def test_switch_feedback_changes_next_toggle(knx, sim):
    """A KNX write on the input's switch object becomes the channel's
    toggle state: after writing True the next press must emit False."""
    ga = INPUTS[0]["switch"]
    await knx.write(ga, DPTBinary(1))
    await asyncio.sleep(0.5)  # let the feedback command reach the worker
    knx.drain()
    await sim.device_event(0, 0, "short_press")
    await knx.wait_telegram(ga, lambda v: v == 0, timeout=10)


async def test_long_press_dims_with_alternating_direction(knx, sim):
    ga = INPUTS[1]["dimming"]
    await sim.device_event(0, 1, "long_press_start")
    t1 = await knx.wait_telegram(ga, timeout=10)
    start1 = _value(t1)

    await sim.device_event(0, 1, "long_press_stop")
    await knx.wait_telegram(ga, lambda v: v == 0, timeout=10)

    await sim.device_event(0, 1, "long_press_start")
    t2 = await knx.wait_telegram(ga, timeout=10)
    start2 = _value(t2)

    # DPT 3.007: bit 3 = direction, low bits = step code (1 = start).
    assert start1 & 0x07 == 1 and start2 & 0x07 == 1
    assert (start1 ^ start2) & 0x08, (
        f"direction did not alternate: {start1:#x}/{start2:#x}"
    )


async def test_scene_button_sends_scene_number(knx, sim):
    ga = INPUTS[2]["scene"]
    await sim.device_event(0, 2, "short_press")
    # Scene 5 configured; DPT 17.001 transmits scene - 1.
    await knx.wait_telegram(ga, lambda v: v == [4], timeout=10)


async def test_occupancy_events_map_to_switch(knx, sim):
    ga = INPUTS[3]["switch"]
    await sim.device_event(1, 0, "occupied")
    await knx.wait_telegram(ga, lambda v: v == 1, timeout=10)
    await sim.device_event(1, 0, "vacant")
    await knx.wait_telegram(ga, lambda v: v == 0, timeout=10)


async def test_light_reports_lux_and_threshold_suppresses(knx, sim):
    """Resolution 5 lux/step: raw 100 -> 500 lux -> DPT 9.004 0x2E1B.
    A 5 % change stays under the 20 % threshold; +100 % does not."""
    ga = INPUTS[4]["illuminance"]
    await sim.device_event(2, 0, "illuminance", 100)
    # 500 lux with the smallest exponent that fits the mantissa:
    # exponent 5, mantissa 1563 (0.01 * 1563 * 2^5 = 500.16 lux).
    await knx.wait_telegram(ga, lambda v: v == [0x2E, 0x1B], timeout=10)

    knx.drain()
    result = await sim.device_event(2, 0, "illuminance", 105)  # +5 %
    assert result["sent"], result
    await asyncio.sleep(1.0)
    leftovers = [
        t for t in knx.collect_all() if str(t.destination_address) == ga
    ]
    assert not leftovers, "small change crossed the send threshold"

    await sim.device_event(2, 0, "illuminance", 200)  # 1000 lux, +100 %
    # 1000 lux -> exponent 6, mantissa 1563 (0.01 * 1563 * 2^6 = 1000.32).
    await knx.wait_telegram(ga, lambda v: v == [0x36, 0x1B], timeout=10)


async def test_event_from_unconfigured_device_is_ignored(knx, sim):
    """A raw event frame from a device the gateway never discovered must
    produce no telegram. (Unbound instances of known devices are disabled
    by the inventory, so the device model suppresses them before the wire —
    this pushes the frame directly.)"""
    ga = INPUTS[0]["switch"]
    knx.drain()
    await sim.request("event", frame=_event_frame(9, 0, 2))  # dev 9 short press
    await asyncio.sleep(1.0)
    assert not [t for t in knx.collect_all() if str(t.destination_address) == ga]


async def test_assign_new_input_device(knx, sim):
    """Function property 7 (new): an unaddressed device gets the first free
    short address (3) and appears in the input presence map."""
    await sim.add_device(short=None, instances=[1], random=0x001234)
    response = await commissioning_command(
        knx, DEVICE_ADDRESS, PROP_ASSIGN_INPUTS, bytes([MODE_UNADDRESSED])
    )
    assert response == [RESPONSE_ACCEPTED]

    deadline = time.monotonic() + 30
    while True:
        state = await input_state(knx, DEVICE_ADDRESS)
        if state["present"][3 // 8] & (1 << 3):
            break
        assert time.monotonic() < deadline, f"device 3 absent in {state}"
        await asyncio.sleep(0.3)
    assert state["summary"][3] == 0x20 | 1

    device = await sim.device_by_short(3)
    assert device is not None
    # Bound to no channel: configured (scheme + filter set) but disabled.
    assert device["instances"][0]["scheme"] == 2
    assert device["instances"][0]["filter"] == 0xF4
    assert device["instances"][0]["enabled"] is False


async def test_quiescent_suppresses_events_during_gear_commissioning(knx, sim):
    """Gear addressing wraps the Part 102 sequence in START/STOP QUIESCENT
    MODE (Part 103): control devices must not transmit meanwhile. An
    unaddressed gear gives the assign-new job a real search window."""
    result = await sim.device_event(0, 0, "short_press")
    assert result["sent"], result  # not quiescent outside commissioning
    knx.drain()

    await sim.request("add_gear", short=None, groups=[])
    job = asyncio.create_task(
        commissioning_command(knx, DEVICE_ADDRESS, 2, bytes([0]))
    )
    suppressed = False
    deadline = time.monotonic() + 30
    while not suppressed and time.monotonic() < deadline:
        result = await sim.device_event(0, 0, "short_press")
        suppressed = result.get("reason") == "quiescent"
        if not suppressed and not result["sent"]:
            break  # disabled/filtered would mean a real bug
        await asyncio.sleep(0.05)
    await job
    assert suppressed, "device never went quiescent during gear addressing"

    # The command task returns on acceptance; wait until the addressing run
    # actually released quiescent mode before emitting again.
    deadline = time.monotonic() + 30
    while True:
        devices = await sim.devices()
        device0 = next(d for d in devices if d["short"] == 0)
        if not device0["quiescent"]:
            break
        assert time.monotonic() < deadline, "still quiescent after addressing"
        await asyncio.sleep(0.3)

    # Events flow again once commissioning finished.
    result = await sim.device_event(0, 0, "short_press")
    assert result["sent"], result
