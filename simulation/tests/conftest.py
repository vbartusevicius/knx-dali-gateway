"""Session wiring: simulator control client, xknx routing client, and the
one-time device programming the whole suite depends on."""

from __future__ import annotations

import asyncio
import logging
import os

import pytest_asyncio
from xknx.telegram.address import IndividualAddress
from xknx.telegram.apci import DeviceDescriptorRead, DeviceDescriptorResponse

from knx_programmer import programmer

from . import scenario
from .helpers import KnxClient, SimClient

logger = logging.getLogger(__name__)

SIM_HOST = os.environ.get("DALI_SIM_HOST", "dali-sim")
SIM_CONTROL_PORT = int(os.environ.get("DALI_SIM_CONTROL_PORT", "5556"))

READY_TIMEOUT = 60.0


async def _wait_device_online(knx: KnxClient, address: str, timeout: float) -> None:
    """Poll device-descriptor reads until the stack answers."""
    deadline = asyncio.get_event_loop().time() + timeout
    while True:
        try:
            async with knx.xknx.management.connection(
                IndividualAddress(address)
            ) as conn:
                await conn.request(
                    DeviceDescriptorRead(), expected=DeviceDescriptorResponse
                )
                return
        except Exception as exc:  # any failure means "not up yet"
            if asyncio.get_event_loop().time() > deadline:
                raise TimeoutError(
                    f"device {address} did not come online within {timeout}s"
                ) from exc
            await asyncio.sleep(0.5)


async def _wait_state_property(knx: KnxClient, address: str, timeout: float) -> None:
    """The commissioning state property only answers once configured() and
    the application callbacks are live - a good end-of-startup probe."""
    from .helpers import commissioning_state

    deadline = asyncio.get_event_loop().time() + timeout
    while True:
        try:
            await commissioning_state(knx, address)
            return
        except Exception as exc:
            if asyncio.get_event_loop().time() > deadline:
                raise TimeoutError(
                    f"commissioning state never answered on {address}"
                ) from exc
            await asyncio.sleep(0.5)


@pytest_asyncio.fixture(scope="session", loop_scope="session")
async def lab():
    sim = SimClient(SIM_HOST, SIM_CONTROL_PORT)
    await sim.connect()
    await sim.request("reset")

    knx = KnxClient(programmer.routing_connection(scenario.TESTER_ADDRESS))
    await knx.xknx.start()

    address = IndividualAddress(scenario.DEVICE_ADDRESS)
    await programmer.program(
        knx.xknx,
        address,
        group_addresses=scenario.group_addresses(),
        associations=scenario.associations(),
        parameters=scenario.parameters(),
    )
    await programmer.restart(knx.xknx, address)
    await _wait_state_property(knx, scenario.DEVICE_ADDRESS, READY_TIMEOUT)
    knx.startup_telegrams = knx.collect_all()
    await sim.frames(clear=True)

    yield knx, sim

    knx.drain()
    await sim.close()
    await knx.xknx.stop()


@pytest_asyncio.fixture(loop_scope="session")
async def knx(lab) -> KnxClient:
    client, _ = lab
    client.drain()
    return client


@pytest_asyncio.fixture(loop_scope="session")
async def sim(lab) -> SimClient:
    _, client = lab
    await client.frames(clear=True)
    return client
