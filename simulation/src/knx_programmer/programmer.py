"""ETS-like download over KNXnet/IP for the thelsing 0x57B0 lab device.

Procedure, derived from the pinned stack (bau_systemB.cpp,
table_object.cpp, application_layer.cpp):

1. Broadcast A_IndividualAddress_Write while the device is in programming
   mode (the lab enables prog mode automatically when the stored individual
   address is 0).
2. Point-to-point management connection (T_Connect) to the new address.
3. For each table object - 1 address table, 2 association table,
   3 group-object table, 4 application program (parameters):
   a. PropertyValueWrite PID_LOAD_STATE_CONTROL (5) = LE_START_LOADING (1).
   b. PropertyValueWrite PID_LOAD_STATE_CONTROL = LE_ADDITIONAL_LOAD_CONTROLS
      (3) + Data Relative Allocation (0x0B) + 32-bit size + doFill/fillByte.
   c. PropertyValueRead PID_TABLE_REFERENCE (7) -> 32-bit memory offset of
      the freshly allocated table.
   d. A_Memory_Write chunks of the table contents at that offset.
   e. PropertyValueWrite PID_LOAD_STATE_CONTROL = LE_LOAD_COMPLETED (2),
      which persists the memory image.
4. A_Restart: the process re-execs itself and boots configured.
"""

from __future__ import annotations

import asyncio
import logging

from xknx import XKNX
from xknx.io import ConnectionConfig, ConnectionType
from xknx.management import procedures
from xknx.telegram import apci
from xknx.telegram.address import IndividualAddress

from . import tables

logger = logging.getLogger(__name__)

PID_LOAD_STATE_CONTROL = 5
PID_TABLE_REFERENCE = 7

LE_START_LOADING = 1
LE_LOAD_COMPLETED = 2
LE_ADDITIONAL_LOAD_CONTROLS = 3
ALLOC_DATA_RELATIVE = 0x0B

# Interface object indices in the pinned Bau57B0.
OBJ_ADDRESS_TABLE = 1
OBJ_ASSOCIATION_TABLE = 2
OBJ_GROUP_OBJECT_TABLE = 3
OBJ_APPLICATION_PROGRAM = 4

MEMORY_WRITE_CHUNK = 48

# Commissioning function-property object (gateway/src/knx/ObjectMap).
COMMISSIONING_OBJECT_INDEX = 160


async def _load_state(conn, object_index: int, event: bytes) -> None:
    response = await conn.request(
        apci.PropertyValueWrite(
            object_index=object_index,
            property_id=PID_LOAD_STATE_CONTROL,
            count=1,
            start_index=1,
            data=event,
        ),
        expected=apci.PropertyValueResponse,
    )
    logger.debug("load state obj %d <- %s", object_index, response.payload)


async def _table_reference(conn, object_index: int) -> int:
    response = await conn.request(
        apci.PropertyValueRead(
            object_index=object_index,
            property_id=PID_TABLE_REFERENCE,
            count=1,
            start_index=1,
        ),
        expected=apci.PropertyValueResponse,
    )
    return int.from_bytes(response.payload.data[-4:], "big")


async def load_table(conn, object_index: int, data: bytes) -> None:
    """Run one System-B table load: allocate, memory-write, complete."""
    await _load_state(conn, object_index, bytes([LE_START_LOADING]))
    size = len(data)
    alloc = bytes(
        [
            LE_ADDITIONAL_LOAD_CONTROLS,
            ALLOC_DATA_RELATIVE,
            (size >> 24) & 0xFF,
            (size >> 16) & 0xFF,
            (size >> 8) & 0xFF,
            size & 0xFF,
            0x00,  # doFill
            0x00,  # fillByte
        ]
    )
    await _load_state(conn, object_index, alloc)
    base = await _table_reference(conn, object_index)
    logger.info("object %d: %d bytes at 0x%04x", object_index, size, base)
    for offset in range(0, size, MEMORY_WRITE_CHUNK):
        chunk = data[offset : offset + MEMORY_WRITE_CHUNK]
        # The device does not answer A_Memory_Write outside verify mode.
        await conn.send_data(apci.MemoryWrite(address=base + offset, data=chunk))
    await _load_state(conn, object_index, bytes([LE_LOAD_COMPLETED]))


async def program(
    xknx: XKNX,
    address: IndividualAddress,
    group_addresses: list[int],
    associations: list[tuple[int, int]],
    parameters: bytes,
) -> None:
    """Write the individual address and download all four tables."""
    await procedures.nm_individual_address_write(xknx, address)
    await asyncio.sleep(0.2)  # let the device apply the new IA
    async with xknx.management.connection(address) as conn:
        await load_table(
            conn, OBJ_ADDRESS_TABLE, tables.build_address_table(group_addresses)
        )
        await load_table(
            conn, OBJ_ASSOCIATION_TABLE, tables.build_association_table(associations)
        )
        await load_table(
            conn,
            OBJ_GROUP_OBJECT_TABLE,
            tables.build_group_object_table(tables.all_descriptors()),
        )
        await load_table(conn, OBJ_APPLICATION_PROGRAM, parameters)


async def restart(xknx: XKNX, address: IndividualAddress) -> None:
    """A_Restart; the lab process re-execs itself and reboots configured."""
    await procedures.dm_restart(xknx, address)


def routing_connection(individual_address: str = "15.15.255") -> XKNX:
    """XKNX on KNXnet/IP routing (multicast 224.0.23.12:3671, the default)."""
    return XKNX(
        connection_config=ConnectionConfig(
            connection_type=ConnectionType.ROUTING,
            individual_address=individual_address,
        )
    )


def ga(main: int, middle: int, sub: int) -> int:
    """3-level group address to raw 16-bit."""
    return (main << 11) | (middle << 8) | sub
