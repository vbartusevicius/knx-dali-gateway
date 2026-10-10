"""ETS parameters: fixed switch-on level and group-membership reconciliation."""

from xknx.telegram.apci import DPTBinary

from .scenario import CHANNELS


async def test_group_membership_reconciled_on_startup(sim):
    """Gear 0 started without group 0 in the simulator, but the ETS
    parameters mark channel 0 a member; startup reconciliation must write
    the membership to the gear."""
    gear = await sim.wait_gear(
        lambda g: g["short"] == 0 and 0 in g["groups"], timeout=20
    )
    assert 0 in gear["groups"]
    gear1 = await sim.wait_gear(lambda g: g["short"] == 1 and 0 in g["groups"])
    assert 0 in gear1["groups"]


async def test_fixed_switch_on_level(knx, sim):
    """Channel 2 is configured FixedLevel 50%: a switch-on lands near arc
    127 instead of the last/full level."""
    gear_before = await sim.gear_by_short(2)
    assert gear_before is not None and gear_before["level"] == 0

    await knx.write(CHANNELS[2]["switch"], DPTBinary(1))
    gear = await sim.wait_gear(
        lambda g: g["short"] == 2 and 115 <= g["level"] <= 140, timeout=15
    )
    assert 115 <= gear["level"] <= 140
