"""Test-side helpers: simulator control client and KNX routing client."""

from __future__ import annotations

import asyncio
import json
import time
from typing import Any, Callable

from xknx import XKNX
from xknx.telegram import Telegram
from xknx.telegram.address import GroupAddress, IndividualAddress
from xknx.telegram.apci import GroupValueRead, GroupValueResponse, GroupValueWrite

# Commissioning function-property object index (gateway ObjectMap).
COMMISSIONING_OBJECT = 160


class SimClient:
    """JSONL control client for the DALI simulator."""

    def __init__(self, host: str, port: int = 5556) -> None:
        self.host = host
        self.port = port
        self._lock = asyncio.Lock()
        self._reader: asyncio.StreamReader | None = None
        self._writer: asyncio.StreamWriter | None = None

    async def connect(self) -> None:
        # The frame_log response is one long JSON line; raise the stream limit.
        self._reader, self._writer = await asyncio.open_connection(
            self.host, self.port, limit=8 * 1024 * 1024
        )

    async def request(self, op: str, **fields: Any) -> dict:
        async with self._lock:
            assert self._writer is not None and self._reader is not None
            self._writer.write(
                json.dumps({"op": op, **fields}).encode() + b"\n"
            )
            await self._writer.drain()
            try:
                line = await asyncio.wait_for(self._reader.readline(), timeout=15)
            except asyncio.TimeoutError:
                # A late response would poison the next request; reconnect.
                await self.close()
                await self.connect()
                raise
            if not line:
                raise ConnectionError("simulator closed the control connection")
            return json.loads(line)

    async def gear(self) -> list[dict]:
        return (await self.request("list"))["gear"]

    async def gear_by_short(self, short: int) -> dict | None:
        return next((g for g in await self.gear() if g["short"] == short), None)

    async def frames(self, clear: bool = False) -> list[dict]:
        return (await self.request("frame_log", clear=clear))["frames"]

    async def wait_frame(
        self,
        predicate: Callable[[dict], bool],
        timeout: float = 10.0,
        description: str = "",
    ) -> dict:
        """Poll the frame log until a matching entry appears."""
        deadline = time.monotonic() + timeout
        seen: list[dict] = []
        while True:
            seen = await self.frames()
            for entry in seen:
                if predicate(entry):
                    return entry
            if time.monotonic() > deadline:
                raise TimeoutError(
                    f"no matching DALI frame within {timeout}s ({description}); "
                    f"last frames: {[hex(f['frame']) for f in seen[-10:]]}"
                )
            await asyncio.sleep(0.05)

    async def wait_gear(
        self, predicate: Callable[[dict], bool], timeout: float = 10.0
    ) -> dict:
        deadline = time.monotonic() + timeout
        while True:
            gear = await self.gear()
            for g in gear:
                if predicate(g):
                    return g
            if time.monotonic() > deadline:
                raise TimeoutError(f"gear state not reached within {timeout}s: {gear}")
            await asyncio.sleep(0.1)

    async def devices(self) -> list[dict]:
        return (await self.request("list_devices"))["devices"]

    async def device_by_short(self, short: int) -> dict | None:
        return next((d for d in await self.devices() if d["short"] == short), None)

    async def device_event(
        self, short: int, instance: int, event: str, value: int = 0
    ) -> dict:
        return await self.request(
            "device_event", short=short, instance=instance, event=event, value=value
        )

    async def add_device(self, **fields: Any) -> dict:
        return await self.request("add_device", **fields)

    async def close(self) -> None:
        if self._writer:
            self._writer.close()


class KnxClient:
    """xknx routing client with an observer queue for inbound telegrams."""

    def __init__(self, xknx: XKNX) -> None:
        self.xknx = xknx
        self.incoming: asyncio.Queue[Telegram] = asyncio.Queue()
        xknx.telegram_queue.register_telegram_received_cb(
            lambda telegram: self.incoming.put_nowait(telegram)
        )

    async def write(self, ga: str, payload) -> None:
        await self.xknx.cemi_handler.send_telegram(
            Telegram(
                destination_address=GroupAddress(ga),
                payload=GroupValueWrite(payload),
            )
        )

    async def read(self, ga: str) -> None:
        await self.xknx.cemi_handler.send_telegram(
            Telegram(
                destination_address=GroupAddress(ga),
                payload=GroupValueRead(),
            )
        )

    def _matches(
        self, telegram: Telegram, ga: str, predicate: Callable[[Any], bool] | None
    ) -> bool:
        if str(telegram.destination_address) != ga:
            return False
        if not isinstance(telegram.payload, (GroupValueWrite, GroupValueResponse)):
            return False
        value = telegram.payload.value
        if predicate is None:
            return True
        raw = getattr(value, "value", value)
        if isinstance(raw, tuple):
            raw = list(raw)
        return predicate(raw)

    async def wait_telegram(
        self,
        ga: str,
        predicate: Callable[[Any], bool] | None = None,
        timeout: float = 10.0,
    ) -> Telegram:
        """Wait for a group telegram on ga matching predicate(payload value)."""
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f"no telegram on {ga} within {timeout}s")
            telegram = await asyncio.wait_for(self.incoming.get(), remaining)
            if self._matches(telegram, ga, predicate):
                return telegram

    def collect_all(self) -> list[Telegram]:
        telegrams: list[Telegram] = []
        while not self.incoming.empty():
            telegrams.append(self.incoming.get_nowait())
        return telegrams

    def drain(self) -> None:
        self.collect_all()


async def commissioning_command(knx: KnxClient, address: str, prop: int, data: bytes) -> list[int]:
    """FunctionPropertyCommand on the commissioning object; returns response."""
    from xknx.telegram.apci import FunctionPropertyCommand, FunctionPropertyStateResponse

    async with knx.xknx.management.connection(IndividualAddress(address)) as conn:
        response = await conn.request(
            FunctionPropertyCommand(
                object_index=COMMISSIONING_OBJECT, property_id=prop, data=data
            ),
            expected=FunctionPropertyStateResponse,
        )
    # Wire format: [object_index, property_id, return_code, data...] - our
    # response code is the return_code byte.
    return [response.payload.return_code]


async def commissioning_state(knx: KnxClient, address: str) -> dict:
    """Read commissioning state property 1: [phase, progress, found, error, bitmaps]."""
    from xknx.telegram.apci import FunctionPropertyStateRead, FunctionPropertyStateResponse

    async with knx.xknx.management.connection(IndividualAddress(address)) as conn:
        response = await conn.request(
            FunctionPropertyStateRead(
                object_index=COMMISSIONING_OBJECT, property_id=1, data=b""
            ),
            expected=FunctionPropertyStateResponse,
        )
    # The first payload byte is parsed as return_code by xknx; recombine.
    data = bytes([response.payload.return_code]) + response.payload.data
    return {
        "phase": data[0],
        "progress": data[1],
        "found": data[2],
        "error": data[3],
        "present": bytes(data[4:12]),
        "faulty": bytes(data[12:20]),
    }


async def input_state(knx: KnxClient, address: str) -> dict:
    """Read commissioning state property 2: [present bitmap(8), summary(64)].

    summary[d] packs bits 4:0 = instance count, bit 5 = has a push-button
    instance, bit 6 = has an occupancy instance, bit 7 = has a light
    instance (Commissioning::input_snapshot).
    """
    from xknx.telegram.apci import FunctionPropertyStateRead, FunctionPropertyStateResponse

    async with knx.xknx.management.connection(IndividualAddress(address)) as conn:
        response = await conn.request(
            FunctionPropertyStateRead(
                object_index=COMMISSIONING_OBJECT, property_id=2, data=b""
            ),
            expected=FunctionPropertyStateResponse,
        )
    data = bytes([response.payload.return_code]) + response.payload.data
    return {
        "present": bytes(data[0:8]),
        "summary": bytes(data[8:72]),
    }
