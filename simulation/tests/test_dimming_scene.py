"""Relative dimming (DPT 3.007) and scene recall/store (DPT 18.001)."""

import asyncio
import time

import dali.address
import dali.gear.general as gear_cmd
from xknx.telegram.apci import DPTArray, DPTBinary

from .scenario import BROADCAST, CHANNELS

DPT3_UP = 0x09  # control bit set, any non-zero step code starts dimming up
DPT3_STOP = 0x00


def command_frame(short: int, opcode: int) -> int:
    return ((short << 1) | 1) << 8 | opcode


async def test_relative_dimming_steps_until_stop(knx, sim):
    ch = CHANNELS[0]
    await knx.write(ch["brightness"], DPTArray((30,)))  # start near bottom
    await sim.wait_gear(lambda g: g["short"] == 0 and g["level"] > 0)
    await sim.frames(clear=True)

    await knx.write(ch["rel_dimming"], DPTBinary(DPT3_UP))
    # UP frames (opcode 0x01) repeat roughly every 200 ms.
    async def up_frames():
        frames = await sim.frames()
        return [f for f in frames if f["frame"] == command_frame(0, 0x01)]

    deadline = time.monotonic() + 5
    while len(await up_frames()) < 2:
        assert time.monotonic() < deadline, "expected repeated UP frames while dimming"
        await asyncio.sleep(0.05)
    await knx.write(ch["rel_dimming"], DPTBinary(DPT3_STOP))
    ups = await up_frames()
    gap = ups[1]["ts"] - ups[0]["ts"]
    assert 0.1 <= gap <= 0.5, f"dim step interval {gap}s not ~200ms"

    # Stop triggers a readback and a final status telegram.
    await knx.wait_telegram(ch["brightness_status"], timeout=10)
    gear = await sim.gear_by_short(0)
    assert gear["level"] > 0


async def test_scene_store_and_recall(knx, sim):
    # Level all gear via broadcast brightness (~78%).
    await knx.write(BROADCAST["brightness"], DPTArray((200,)))
    for short in (0, 1, 2, 3):
        await sim.wait_gear(lambda g, s=short: g["short"] == s and g["level"] > 150)

    # Store scene 2: STORE ACTUAL LEVEL (0x21) then STORE DTR AS SCENE (0x42).
    await knx.write(BROADCAST["scene"], DPTArray((0x80 | 2,)))
    await sim.wait_frame(
        lambda f: f["frame"] == command_frame(0x7F, 0x42),
        description="broadcast STORE DTR AS SCENE 2",
    )
    gear = await sim.gear_by_short(0)
    stored = gear["scenes"][2]
    assert stored != 255

    # Broadcast off, then recall scene 2.
    await knx.write(BROADCAST["switch"], DPTBinary(0))
    for short in (0, 1, 2, 3):
        await sim.wait_gear(lambda g, s=short: g["short"] == s and g["level"] == 0)

    await knx.write(BROADCAST["scene"], DPTArray((2,)))
    await sim.wait_frame(
        lambda f: f["frame"] == command_frame(0x7F, 0x12),
        description="broadcast GO TO SCENE 2",
    )
    for short in (0, 1, 2, 3):
        await sim.wait_gear(
            lambda g, s=short, lvl=stored: g["short"] == s and g["level"] == lvl
        )
