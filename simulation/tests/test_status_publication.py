"""Status objects without a GA association must not emit telegrams.

Regression: an object written without an association-table entry is routed to
GA 0/0/0 by the stack (translateAsap -> -1). Startup recovery publishes status
for channels that have no association; those must be suppressed.
"""

from __future__ import annotations

import asyncio

from xknx.telegram.apci import GroupValueResponse, GroupValueWrite

from .helpers import KnxClient, SimClient

OBSERVE_SECONDS = 12.0


async def test_no_status_telegrams_to_unassociated_destination(
    knx: KnxClient, sim: SimClient
) -> None:
    telegrams = list(getattr(knx, "startup_telegrams", []))

    deadline = asyncio.get_event_loop().time() + OBSERVE_SECONDS
    while True:
        remaining = deadline - asyncio.get_event_loop().time()
        if remaining <= 0:
            break
        try:
            telegrams.append(await asyncio.wait_for(knx.incoming.get(), remaining))
        except asyncio.TimeoutError:
            break

    group_values = [
        t
        for t in telegrams
        if isinstance(t.payload, (GroupValueWrite, GroupValueResponse))
    ]
    assert group_values, "no group-value telegrams observed; statuses were not exercised"
    offenders = [
        t for t in group_values if str(t.destination_address) == "0/0/0"
    ]
    assert not offenders, (
        f"{len(offenders)} telegrams sent to 0/0/0 "
        f"(unassociated status objects); payloads: "
        f"{[t.payload.value for t in offenders[:5]]}"
    )
