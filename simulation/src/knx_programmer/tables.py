"""Build the thelsing 0x57B0 table contents from a lab scenario.

Verified against the pinned stack sources:

- AddressTableObject: uint16 BE count, then count 16-bit group addresses
  (address_table_object.cpp entryCount()/getTsap()).
- AssociationTableObject: uint16 BE count, then count (TSAP, ASAP) pairs of
  uint16 BE, TSAP = 1-based index into the address table, ASAP = group object
  number (association_table_object.cpp getTSAP()/getASAP()).
- GroupObjectTableObject: uint16 BE count, then one 16-bit descriptor per
  group object: bit15 update, bit14 transmit, bit13 read-on-init, bit12 write,
  bit11 read, bit10 communication enable, bits 9:8 priority,
  low byte = KNX size code (group_object.cpp, knx spec 3.5.1).
  Verified against the pinned stack: GroupObject::priority() reads
  (descriptor >> 6) & 0xC, i.e. word bits 9:8; sizeInTelegram() consumes the
  whole low byte, so priority bits must not touch it.
- ApplicationProgramObject: raw parameter bytes; knx.paramData(addr) reads
  TableObject::data() + addr.
"""

from __future__ import annotations

# Group object descriptor flags (bit positions in the BE descriptor word).
FLAG_UPDATE = 0x8000
FLAG_TRANSMIT = 0x4000
FLAG_READ_ON_INIT = 0x2000
FLAG_WRITE = 0x1000
FLAG_READ = 0x0800
FLAG_COMMUNICATION = 0x0400
PRIORITY_LOW = 0x0300  # thelsing Priority::LowPriority (0xC) in bits 9:8

# KNX size codes for the low descriptor byte (knx spec 3.5.1 p.178).
SIZE_1BIT = 0x00
SIZE_4BIT = 0x03
SIZE_1BYTE = 0x07
SIZE_2BYTE = 0x08
SIZE_3BYTE = 0x09
SIZE_6BYTE = 0x0B  # code 11 -> 6 bytes; code 12 is 8 bytes (group_object.cpp)


def _u16(value: int) -> bytes:
    return value.to_bytes(2, "big")


def build_address_table(group_addresses: list[int]) -> bytes:
    """group_addresses: raw 16-bit GA values (e.g. 0x0801 for 1/0/1)."""
    out = bytearray(_u16(len(group_addresses)))
    for ga in sorted(group_addresses):
        out += _u16(ga)
    return bytes(out)


def build_association_table(associations: list[tuple[int, int]]) -> bytes:
    """associations: (tsap, asap) pairs, tsap 1-based address-table index."""
    out = bytearray(_u16(len(associations)))
    for tsap, asap in associations:
        out += _u16(tsap) + _u16(asap)
    return bytes(out)


def build_group_object_table(descriptors: list[int]) -> bytes:
    """descriptors[i] is the descriptor word for group object i+1."""
    out = bytearray(_u16(len(descriptors)))
    for descriptor in descriptors:
        out += _u16(descriptor)
    return bytes(out)


def descriptor(flags: int, size_code: int) -> int:
    return flags | PRIORITY_LOW | size_code


# --- object model, mirrors gateway/src/knx/ObjectMap + generated header ----

CHANNEL_COUNT = 64
GROUP_COUNT = 16
CHANNEL_BLOCK = 15
GROUP_BLOCK = 14
CHANNEL_OFFSET = 1
GROUP_OFFSET = 961
BROADCAST_SWITCH = 1185
BROADCAST_BRIGHTNESS = 1186
BROADCAST_REL_DIMMING = 1187
BROADCAST_SCENE = 1188
BROADCAST_COLOUR_TEMPERATURE = 1189
BROADCAST_REL_COLOUR_TEMPERATURE = 1190
BROADCAST_RGB = 1191
BROADCAST_RGBW = 1192
BROADCAST_XYY = 1193
INPUT_COUNT = 32
INPUT_BLOCK = 4
INPUT_OFFSET = BROADCAST_XYY + 1  # 1194
OBJECT_COUNT = 1321

_WRITE = FLAG_COMMUNICATION | FLAG_WRITE
_STATUS = FLAG_COMMUNICATION | FLAG_READ | FLAG_TRANSMIT

# (function index within channel block) -> (flags, size code)
_CHANNEL_LAYOUT = [
    (_WRITE, SIZE_1BIT),  # 0 Switch DPT 1.001
    (_STATUS, SIZE_1BIT),  # 1 Switch status
    (_WRITE, SIZE_1BYTE),  # 2 Brightness DPT 5.001
    (_STATUS, SIZE_1BYTE),  # 3 Brightness status
    (_WRITE, SIZE_4BIT),  # 4 Relative dimming DPT 3.007
    (_STATUS, SIZE_1BIT),  # 5 Fault DPT 1.005
    (_WRITE, SIZE_2BYTE),  # 6 Colour temperature DPT 7.600
    (_STATUS, SIZE_2BYTE),  # 7 Colour temperature status
    (_WRITE, SIZE_4BIT),  # 8 Relative colour temperature DPT 3.007
    (_WRITE, SIZE_3BYTE),  # 9 RGB DPT 232.600
    (_STATUS, SIZE_3BYTE),  # 10 RGB status
    (_WRITE, SIZE_6BYTE),  # 11 RGBW DPT 251.600
    (_STATUS, SIZE_6BYTE),  # 12 RGBW status
    (_WRITE, SIZE_6BYTE),  # 13 xyY DPT 242.600
    (_STATUS, SIZE_6BYTE),  # 14 xyY status
]
_GROUP_LAYOUT = [
    (_WRITE, SIZE_1BIT),  # Switch
    (_STATUS, SIZE_1BIT),  # Switch status
    (_WRITE, SIZE_1BYTE),  # Brightness
    (_STATUS, SIZE_1BYTE),  # Brightness status
    (_WRITE, SIZE_4BIT),  # Relative dimming
    (_WRITE, SIZE_2BYTE),  # Colour temperature
    (_STATUS, SIZE_2BYTE),  # Colour temperature status
    (_WRITE, SIZE_4BIT),  # Relative colour temperature
    (_WRITE, SIZE_3BYTE),  # RGB
    (_STATUS, SIZE_3BYTE),  # RGB status
    (_WRITE, SIZE_6BYTE),  # RGBW
    (_STATUS, SIZE_6BYTE),  # RGBW status
    (_WRITE, SIZE_6BYTE),  # xyY
    (_STATUS, SIZE_6BYTE),  # xyY status
]
_BROADCAST = {
    BROADCAST_SWITCH: (_WRITE, SIZE_1BIT),
    BROADCAST_BRIGHTNESS: (_WRITE, SIZE_1BYTE),
    BROADCAST_REL_DIMMING: (_WRITE, SIZE_4BIT),
    BROADCAST_SCENE: (_WRITE, SIZE_1BYTE),  # DPT 18.001
    BROADCAST_COLOUR_TEMPERATURE: (_WRITE, SIZE_2BYTE),
    BROADCAST_REL_COLOUR_TEMPERATURE: (_WRITE, SIZE_4BIT),
    BROADCAST_RGB: (_WRITE, SIZE_3BYTE),
    BROADCAST_RGBW: (_WRITE, SIZE_6BYTE),
    BROADCAST_XYY: (_WRITE, SIZE_6BYTE),
}
# Input channels: switch is C/W/T (KNX writes feed the channel's toggle
# state back), dimming C/T, illuminance C/R/T, scene C/T.
_INPUT_LAYOUT = [
    (_WRITE | FLAG_TRANSMIT, SIZE_1BIT),  # Switch/Occupancy DPT 1.001
    (FLAG_COMMUNICATION | FLAG_TRANSMIT, SIZE_4BIT),  # Dimming DPT 3.007
    (_STATUS, SIZE_2BYTE),  # Illuminance DPT 9.004
    (FLAG_COMMUNICATION | FLAG_TRANSMIT, SIZE_1BYTE),  # Scene DPT 17.001
]


def channel_object(channel: int, function: int) -> int:
    return CHANNEL_OFFSET + channel * CHANNEL_BLOCK + function


def group_object(group: int, function: int) -> int:
    return GROUP_OFFSET + group * GROUP_BLOCK + function


def input_object(channel: int, function: int) -> int:
    return INPUT_OFFSET + channel * INPUT_BLOCK + function


def all_descriptors() -> list[int]:
    """Descriptor for every object 1..OBJECT_COUNT, flags per function."""
    out: list[int] = []
    for channel in range(CHANNEL_COUNT):
        for flags, size in _CHANNEL_LAYOUT:
            out.append(descriptor(flags, size))
    for group in range(GROUP_COUNT):
        for flags, size in _GROUP_LAYOUT:
            out.append(descriptor(flags, size))
    for asap in range(GROUP_OFFSET + GROUP_COUNT * GROUP_BLOCK, INPUT_OFFSET):
        flags, size = _BROADCAST[asap]
        out.append(descriptor(flags, size))
    for channel in range(INPUT_COUNT):
        for flags, size in _INPUT_LAYOUT:
            out.append(descriptor(flags, size))
    assert len(out) == OBJECT_COUNT
    return out
