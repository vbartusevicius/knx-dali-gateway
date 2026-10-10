"""Group and broadcast fan-out: one KNX telegram -> group/broadcast DALI
frame, member channel statuses go out on their own status GAs."""

import dali.address
import dali.gear.general as gear_cmd
from xknx.telegram.apci import DPTArray, DPTBinary

from .scenario import BROADCAST, CHANNELS, GROUP0


def dapc_group(group: int, level: int) -> int:
    return gear_cmd.DAPC(dali.address.GearGroup(group), level).frame.as_integer


def dapc_broadcast(level: int) -> int:
    return gear_cmd.DAPC(dali.address.Broadcast(), level).frame.as_integer


async def test_group_switch_fans_out_to_members(knx, sim):
    """Group switch-on restores the remembered group level: establish it with
    a brightness write first so the expected DAPC value is known."""
    await knx.write(GROUP0["brightness"], DPTArray((150,)))
    gear = await sim.wait_gear(lambda g: g["short"] == 0 and g["level"] > 0)
    level = gear["level"]
    await knx.write(GROUP0["switch"], DPTBinary(0))
    await sim.wait_gear(lambda g: g["short"] == 0 and g["level"] == 0)
    await sim.frames(clear=True)
    # Channel 2 is not a member; record its level to prove it is untouched.
    gear2_before = await sim.gear_by_short(2)

    await knx.write(GROUP0["switch"], DPTBinary(1))

    await sim.wait_frame(
        lambda f: f["frame"] == dapc_group(0, level),
        description=f"DAPC group 0 -> restored level {level}",
    )
    # Channels 0 and 1 are members of DALI group 0 (parameter + gear config).
    for channel in (0, 1):
        await sim.wait_gear(
            lambda g, c=channel: g["short"] == c and g["level"] == level
        )
    # Group status + member channel statuses.
    await knx.wait_telegram(GROUP0["switch_status"], lambda v: v == 1)
    for channel in (0, 1):
        await knx.wait_telegram(
            CHANNELS[channel]["switch_status"], lambda v: v == 1
        )
    # Channel 2 is not a member and keeps whatever level it had.
    gear2 = await sim.gear_by_short(2)
    assert gear2 is not None and gear2["level"] == gear2_before["level"]


async def test_broadcast_switches_all_channels(knx, sim):
    """Broadcast switch-on restores the remembered broadcast level."""
    await knx.write(BROADCAST["brightness"], DPTArray((200,)))
    gear = await sim.wait_gear(lambda g: g["short"] == 2 and g["level"] > 0)
    level = gear["level"]
    await knx.write(BROADCAST["switch"], DPTBinary(0))
    await sim.wait_gear(lambda g: g["short"] == 2 and g["level"] == 0)
    await sim.frames(clear=True)

    await knx.write(BROADCAST["switch"], DPTBinary(1))

    await sim.wait_frame(
        lambda f: f["frame"] == dapc_broadcast(level),
        description=f"broadcast DAPC -> restored level {level}",
    )
    for short in (0, 1, 2, 3):
        await sim.wait_gear(
            lambda g, s=short: g["short"] == s and g["level"] == level
        )
    await knx.wait_telegram(CHANNELS[2]["switch_status"], lambda v: v == 1)


async def test_broadcast_off(knx, sim):
    await knx.write(BROADCAST["switch"], DPTBinary(0))
    await sim.wait_frame(
        lambda f: f["frame"] == dapc_broadcast(0), description="broadcast DAPC 0"
    )
    for short in (0, 1, 2, 3):
        await sim.wait_gear(lambda g, s=short: g["short"] == s and g["level"] == 0)
