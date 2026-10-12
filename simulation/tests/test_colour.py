"""DT8 colour control end-to-end: IEC 62386-209 sequences on the sim bus.

Gear mix (see simulation/compose.yaml): short 0 = Tc DT8 (group 0),
short 1 = RGBWAF DT8 (group 0), short 2 = xy DT8, short 3 = non-DT8.
Extended opcodes and selectors verified against python-dali's
dali/gear/colour.py; KNX payloads against xknx's DPTColorRGBW/DPTColorXYY.
"""

import asyncio
import time

import pytest
from xknx.telegram.apci import DPTArray, DPTBinary

from .helpers import commissioning_state
from .scenario import BROADCAST, CHANNELS, DEVICE_ADDRESS, GROUP0

# IEC 62386-209 application-extended opcodes (colour.py _cmdval values).
EXT_SET_TEMP_X = 0xE0
EXT_SET_TEMP_Y = 0xE1
EXT_ACTIVATE = 0xE2
EXT_SET_TEMP_TC = 0xE7
EXT_TC_STEP_COOLER = 0xE8
EXT_TC_STEP_WARMER = 0xE9
EXT_SET_TEMP_RGB = 0xEB
EXT_SET_TEMP_WAF = 0xEC
EXT_COPY_REPORT = 0xEE
EXT_QUERY_COLOUR_STATUS = 0xF8
EXT_QUERY_COLOUR_VALUE = 0xFA

# IEC 62386-102 opcodes/specials used around the colour sequences.
QUERY_STATUS = 0x90
QUERY_CONTENT_DTR0 = 0x98
QUERY_ACTUAL_LEVEL = 0xA0
SPECIALS = (0xA3, 0xC1, 0xC3, 0xC5)  # DTR0, EDT8, DTR1, DTR2
EDT8 = 0xC108

PHASE_DONE = 5
DPT3_INCREASE = 0x09
DPT3_STOP = 0x00


def command_frame(short: int, opcode: int) -> int:
    return ((short << 1) | 1) << 8 | opcode


def group_frame(group: int, opcode: int) -> int:
    return (0x80 | group << 1 | 1) << 8 | opcode


def broadcast_frame(opcode: int) -> int:
    return 0xFF00 | opcode


def dapc_frame(short: int, level: int) -> int:
    return (short << 1) << 8 | level


def dtr0(value: int) -> int:
    return 0xA300 | value


def kelvin(value: int) -> DPTArray:
    return DPTArray((value >> 8, value & 0xFF))


def is_colour_frame(frame: int) -> bool:
    """DTR/special writes and application-extended commands."""
    if (frame >> 8) in SPECIALS:
        return True
    # Addressed command (odd address byte) with an extended opcode. DAPC
    # frames use even address bytes and cannot reach this branch.
    return ((frame >> 8) & 1) == 1 and (frame & 0xFF) >= 0xE0


def is_idle_query(frame: int) -> bool:
    """Background fault polling: addressed level/status queries only."""
    addressed = ((frame >> 8) & 1) == 1 and (frame >> 8) < 0x80
    return addressed and (frame & 0xFF) in (QUERY_STATUS, QUERY_ACTUAL_LEVEL)


async def wait_startup(knx) -> None:
    """Capabilities are probed during the startup scan; wait for it."""
    deadline = time.monotonic() + 60
    while True:
        state = await commissioning_state(knx, DEVICE_ADDRESS)
        if state["phase"] == PHASE_DONE:
            return
        assert time.monotonic() < deadline, "startup scan never reached Done"
        await asyncio.sleep(0.25)


async def test_colour_temperature_set_and_status(knx, sim):
    await wait_startup(knx)
    ch = CHANNELS[0]
    await sim.frames(clear=True)

    await knx.write(ch["colour_temperature"], kelvin(4000))
    # 4000 K = 250 mirek: DTR0=0xFA, DTR1=0, then SET TEMPORARY TC + ACTIVATE.
    await sim.wait_frame(
        lambda f: f["frame"] == command_frame(0, EXT_SET_TEMP_TC),
        description="SET TEMPORARY COLOUR TEMPERATURE",
    )
    await sim.wait_frame(
        lambda f: f["frame"] == command_frame(0, EXT_ACTIVATE),
        description="ACTIVATE",
    )
    telegram = await knx.wait_telegram(ch["colour_temperature_status"], timeout=15)
    raw = telegram.payload.value.value
    assert list(raw) == [0x0F, 0xA0]  # 4000 K big-endian

    gear = await sim.gear_by_short(0)
    assert gear["active_colour"] == "tc"
    assert gear["actual_tc_mirek"] == 250


async def test_relative_colour_temperature_steps_until_stop(knx, sim):
    await wait_startup(knx)
    ch = CHANNELS[0]
    before = (await sim.gear_by_short(0))["actual_tc_mirek"]
    await sim.frames(clear=True)

    # DPT 3.007 increase = higher Kelvin = COLOUR TEMPERATURE STEP COOLER.
    await knx.write(ch["rel_colour_temperature"], DPTBinary(DPT3_INCREASE))

    async def step_frames():
        frames = await sim.frames()
        return [f for f in frames if f["frame"] == command_frame(0, EXT_TC_STEP_COOLER)]

    deadline = time.monotonic() + 8
    while len(await step_frames()) < 2:
        assert time.monotonic() < deadline, "expected repeated STEP COOLER frames"
        await asyncio.sleep(0.05)
    await knx.write(ch["rel_colour_temperature"], DPTBinary(DPT3_STOP))
    steps = await step_frames()
    gap = steps[1]["ts"] - steps[0]["ts"]
    assert 0.1 <= gap <= 0.5, f"colour step interval {gap}s not ~200ms"

    # Stop triggers a colour readback and a fresh status telegram.
    await knx.wait_telegram(ch["colour_temperature_status"], timeout=10)
    after = (await sim.gear_by_short(0))["actual_tc_mirek"]
    assert after < before, "increase should step towards cooler (lower mirek)"
    assert (after - before) == -len(steps)


async def test_rgb_write_levels_and_status(knx, sim):
    await wait_startup(knx)
    ch = CHANNELS[1]
    await sim.frames(clear=True)

    await knx.write(ch["rgb"], DPTArray((255, 128, 64)))
    # round(v*254/255): 255 -> 254, 128 -> 127, 64 -> 64.
    await sim.wait_frame(
        lambda f: f["frame"] == command_frame(1, EXT_SET_TEMP_RGB),
        description="SET TEMPORARY RGB DIMLEVEL",
    )
    await sim.wait_frame(
        lambda f: f["frame"] == command_frame(1, EXT_ACTIVATE),
        description="ACTIVATE",
    )
    frames = await sim.frames()
    assert dtr0(254) in {f["frame"] for f in frames}, "DTR0 should hold red 254"
    # Brightness = max component -> DAPC at arc 254.
    assert dapc_frame(1, 254) in {f["frame"] for f in frames}

    gear = await sim.gear_by_short(1)
    assert gear["active_colour"] == "rgbwaf"
    assert gear["actual_rgb"] == [254, 127, 64]
    assert gear["level"] == 254

    # A 4-channel RGBWAF gear reports through the RGBW status object.
    telegram = await knx.wait_telegram(ch["rgbw_status"], timeout=15)
    assert list(telegram.payload.value.value) == [255, 128, 64, 0, 0, 0b1111]


async def test_rgbw_validity_mask_keeps_invalid_channels(knx, sim):
    await wait_startup(knx)
    ch = CHANNELS[1]
    await sim.frames(clear=True)

    # Only the white bit (bit 0) is valid: RGB stay "no change" 0xFF.
    await knx.write(ch["rgbw"], DPTArray((0, 0, 0, 200, 0, 0b0001)))
    await sim.wait_frame(
        lambda f: f["frame"] == command_frame(1, EXT_SET_TEMP_WAF),
        description="SET TEMPORARY WAF DIMLEVEL",
    )
    await sim.wait_gear(lambda g: g["short"] == 1 and g["actual_waf"][0] == 199)

    gear = await sim.gear_by_short(1)
    assert gear["actual_rgb"] == [254, 127, 64], "invalid RGB must not change"
    # Brightness = max valid component: white 200 -> arc 199.
    assert gear["level"] == 199

    # Readback rescales the stored colour to the new brightness.
    telegram = await knx.wait_telegram(ch["rgbw_status"], timeout=15)
    assert list(telegram.payload.value.value) == [200, 100, 50, 157, 0, 0b1111]


async def test_xyy_with_and_without_brightness(knx, sim):
    await wait_startup(knx)
    ch = CHANNELS[2]
    await sim.frames(clear=True)

    # x = 0x4000, y = 0x6000, brightness 150, both fields valid.
    await knx.write(ch["xyy"], DPTArray((0x40, 0x00, 0x60, 0x00, 150, 0b11)))
    await sim.wait_frame(
        lambda f: f["frame"] == command_frame(2, EXT_SET_TEMP_Y),
        description="SET TEMPORARY Y COORDINATE",
    )
    await sim.wait_frame(
        lambda f: f["frame"] == dapc_frame(2, 149),
        description="DAPC for brightness 150 -> arc 149",
    )
    gear = await sim.gear_by_short(2)
    assert gear["actual_x"] == 0x4000
    assert gear["actual_y"] == 0x6000
    assert gear["level"] == 149

    telegram = await knx.wait_telegram(ch["xyy_status"], timeout=15)
    assert list(telegram.payload.value.value) == [0x40, 0x00, 0x60, 0x00, 150, 0b11]

    # Brightness-only write: a DAPC and no colour frames at all.
    await sim.frames(clear=True)
    await knx.write(ch["xyy"], DPTArray((0, 0, 0, 0, 80, 0b01)))
    await sim.wait_frame(lambda f: f["frame"] == dapc_frame(2, 80))
    await asyncio.sleep(0.4)
    frames = await sim.frames()
    offenders = [hex(f["frame"]) for f in frames if is_colour_frame(f["frame"])]
    assert not offenders, f"unexpected colour frames: {offenders}"
    # The xyY status embeds the new brightness.
    telegram = await knx.wait_telegram(ch["xyy_status"], timeout=10)
    assert list(telegram.payload.value.value) == [0x40, 0x00, 0x60, 0x00, 80, 0b11]

    # xy-only write: colour frames, no DAPC.
    await sim.frames(clear=True)
    await knx.write(ch["xyy"], DPTArray((0x30, 0x00, 0x50, 0x00, 0, 0b10)))
    await sim.wait_frame(
        lambda f: f["frame"] == command_frame(2, EXT_SET_TEMP_X),
        description="SET TEMPORARY X COORDINATE",
    )
    await asyncio.sleep(0.4)
    frames = await sim.frames()
    assert not any(
        (f["frame"] >> 8) == (2 << 1) for f in frames
    ), "xy-only write must not produce a DAPC"
    gear = await sim.gear_by_short(2)
    assert gear["actual_x"] == 0x3000
    assert gear["level"] == 80  # unchanged


async def test_unsupported_colour_type_sends_no_frames(knx, sim):
    await wait_startup(knx)
    ch = CHANNELS[3]  # non-DT8 gear: probed empty capabilities
    await sim.frames(clear=True)
    knx.drain()

    await knx.write(ch["colour_temperature"], kelvin(4000))
    await asyncio.sleep(1.5)
    frames = await sim.frames()
    offenders = [
        hex(f["frame"]) for f in frames if not is_idle_query(f["frame"])
    ]
    assert not offenders, f"unsupported write produced bus frames: {offenders}"

    with pytest.raises(asyncio.TimeoutError):
        await knx.wait_telegram(ch["colour_temperature_status"], timeout=2)


async def test_group_colour_fanout(knx, sim):
    await wait_startup(knx)
    await sim.frames(clear=True)

    # Group 0 = gear 0 (Tc) + gear 1 (RGBWAF); group writes always fan out.
    await knx.write(GROUP0["colour_temperature"], kelvin(3000))  # 3000K = 333 mirek
    await sim.wait_frame(
        lambda f: f["frame"] == group_frame(0, EXT_SET_TEMP_TC),
        description="group SET TEMPORARY COLOUR TEMPERATURE",
    )
    await sim.wait_frame(
        lambda f: f["frame"] == group_frame(0, EXT_ACTIVATE),
        description="group ACTIVATE",
    )
    await sim.wait_gear(lambda g: g["short"] == 0 and g["actual_tc_mirek"] == 333)
    await sim.wait_gear(lambda g: g["short"] == 1 and g["actual_tc_mirek"] == 333)

    # Commanded (not read back) statuses for the group and both members.
    telegram = await knx.wait_telegram(GROUP0["colour_temperature_status"], timeout=15)
    assert list(telegram.payload.value.value) == [0x0B, 0xB8]  # 3000 K


async def test_colour_scene_store_and_recall(knx, sim):
    await wait_startup(knx)
    ch0 = CHANNELS[0]
    await knx.write(ch0["colour_temperature"], kelvin(4000))
    await sim.wait_gear(lambda g: g["short"] == 0 and g["actual_tc_mirek"] == 250)
    await sim.frames(clear=True)

    # Store scene 4: the colour-aware sequence must copy the reported colour
    # into the temporaries before STORE DTR AS SCENE.
    await knx.write(BROADCAST["scene"], DPTArray((0x80 | 4,)))
    await sim.wait_frame(
        lambda f: f["frame"] == broadcast_frame(EXT_COPY_REPORT),
        description="broadcast COPY REPORT TO TEMPORARY",
    )
    await sim.wait_frame(
        lambda f: f["frame"] == broadcast_frame(0x44),
        description="broadcast STORE DTR AS SCENE 4",
    )
    gear = await sim.gear_by_short(0)
    assert gear["scene_colours"][4] is True

    # Move the colour, then recall: the gear restores the snapshot.
    await knx.write(ch0["colour_temperature"], kelvin(3000))
    await sim.wait_gear(lambda g: g["short"] == 0 and g["actual_tc_mirek"] == 333)
    await sim.frames(clear=True)

    await knx.write(BROADCAST["scene"], DPTArray((4,)))
    await sim.wait_frame(
        lambda f: f["frame"] == broadcast_frame(0x14),
        description="broadcast GO TO SCENE 4",
    )
    await sim.wait_gear(lambda g: g["short"] == 0 and g["actual_tc_mirek"] == 250)
    # The recall readback republishes the restored colour.
    telegram = await knx.wait_telegram(ch0["colour_temperature_status"], timeout=15)
    assert list(telegram.payload.value.value) == [0x0F, 0xA0]
