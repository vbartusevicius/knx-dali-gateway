"""KNX switch/brightness writes reach the DALI bus and produce statuses."""

import dali.address
import dali.gear.general as gear_cmd
from xknx.telegram.apci import DPTArray, DPTBinary

from .scenario import CHANNELS


def dapc_frame(short: int, level: int) -> int:
    return gear_cmd.DAPC(dali.address.GearShort(short), level).frame.as_integer


async def test_switch_on_sends_dapc_and_reports_status(knx, sim):
    """Switch-on restores the remembered last-active level (not a fixed 254)."""
    ch = CHANNELS[0]
    # Establish a known level, switch off, then on: the DAPC must restore it.
    await knx.write(ch["brightness"], DPTArray((200,)))
    gear = await sim.wait_gear(lambda g: g["short"] == 0 and g["level"] > 0)
    remembered = gear["level"]
    await knx.write(ch["switch"], DPTBinary(0))
    await sim.wait_gear(lambda g: g["short"] == 0 and g["level"] == 0)
    await sim.frames(clear=True)

    await knx.write(ch["switch"], DPTBinary(1))
    frame = await sim.wait_frame(
        lambda f: f["frame"] == dapc_frame(0, remembered),
        description=f"DAPC short 0 -> restored level {remembered}",
    )
    assert frame["status"] == "ok"

    await knx.wait_telegram(ch["switch_status"], lambda v: v == 1)
    await knx.wait_telegram(
        ch["brightness_status"], lambda v: v and abs(v[0] - 200) <= 4
    )

    gear = await sim.wait_gear(
        lambda g: g["short"] == 0 and g["level"] == remembered
    )
    assert gear["level"] == remembered


async def test_switch_off_sends_dapc_zero(knx, sim):
    ch = CHANNELS[0]
    await knx.write(ch["switch"], DPTBinary(0))

    await sim.wait_frame(
        lambda f: f["frame"] == dapc_frame(0, 0), description="DAPC short 0 -> 0"
    )
    await knx.wait_telegram(ch["switch_status"], lambda v: v == 0)


async def test_brightness_write_maps_to_arc_level(knx, sim):
    ch = CHANNELS[1]
    await knx.write(ch["brightness"], DPTArray((128,)))  # 50%

    # KNX scaling 128/255 maps to DALI arc 127 (linear mapping).
    gear = await sim.wait_gear(
        lambda g: g["short"] == 1 and 120 <= g["level"] <= 135
    )
    await knx.wait_telegram(
        ch["brightness_status"], lambda v: v and abs(v[0] - 128) <= 4
    )
