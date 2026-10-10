"""Shared lab scenario: group addresses, object associations, parameters.

Object numbers come from knxprod's generated header contract (mirrored in
knx_programmer.tables): channels 1..960 in blocks of 15
(Switch, SwitchStatus, Brightness, BrightnessStatus, RelativeDimming, Fault,
ColourTemperature, ColourTemperatureStatus, RelativeColourTemperature, RGB,
RGBStatus, RGBW, RGBWStatus, xyY, xyYStatus), groups 961..1184 in blocks of 14
(same minus Fault), broadcast 1185..1193 (Switch, Brightness,
RelativeDimming, Scene, ColourTemperature, RelativeColourTemperature, RGB,
RGBW, xyY).
"""

from __future__ import annotations

from knx_programmer import tables

DEVICE_ADDRESS = "1.1.21"
TESTER_ADDRESS = "15.15.250"

# DALI gear provisioned by the simulator at startup. colour selects the DT8
# feature set: "tc" (colour temperature), "rgbwaf" (4 RGBWAF channels), "xy"
# (xy coordinates); anything else is a non-DT8 gear.
INITIAL_GEAR = [
    {"short": 0, "groups": [0], "colour": "tc"},
    {"short": 1, "groups": [0], "colour": "rgbwaf"},
    {"short": 2, "groups": [], "colour": "xy"},
    {"short": 3, "groups": []},
]

# Control devices provisioned at startup (SIM_DEVICES). instance types follow
# IEC 62386-103: 1 = push button, 3 = occupancy, 4 = light. Device 0's
# instance 3 and device 1's instance 1 are deliberately not bound to any
# input channel so tests can emit events the gateway must ignore.
INITIAL_DEVICES = [
    {"short": 0, "instances": [1, 1, 1, 1]},
    {"short": 1, "instances": [3, 3]},
    {"short": 2, "instances": [4], "resolutions": [5]},
]


def _chan(channel: int) -> dict[str, str]:
    return {
        "switch": f"0/1/{channel}",
        "switch_status": f"1/1/{channel}",
        "brightness": f"0/2/{channel}",
        "brightness_status": f"1/2/{channel}",
        "rel_dimming": f"0/3/{channel}",
        "fault": f"1/3/{channel}",
        "colour_temperature": f"0/6/{channel}",
        "colour_temperature_status": f"1/6/{channel}",
        "rel_colour_temperature": f"0/7/{channel}",
        # Middle > 7 is not representable in a 3-level GA (it aliases the
        # main field), so RGB/RGBW/xyY live on separate main groups.
        "rgb": f"2/0/{channel}",
        "rgb_status": f"3/0/{channel}",
        "rgbw": f"2/1/{channel}",
        "rgbw_status": f"3/1/{channel}",
        "xyy": f"2/2/{channel}",
        "xyy_status": f"3/2/{channel}",
    }


# KNX group addresses per channel function, tests use channels 0..3.
CHANNELS = {c: _chan(c) for c in range(4)}

GROUP0 = {
    "switch": "0/4/0",
    "switch_status": "1/4/0",
    "brightness": "0/4/1",
    "brightness_status": "1/4/1",
    "rel_dimming": "0/4/2",
    "colour_temperature": "0/4/3",
    "colour_temperature_status": "1/4/3",
    "rel_colour_temperature": "0/4/4",
    "rgb": "0/4/5",
    "rgb_status": "1/4/5",
    "rgbw": "0/4/6",
    "rgbw_status": "1/4/6",
    "xyy": "0/4/7",
    "xyy_status": "1/4/7",
}

BROADCAST = {
    "switch": "0/5/0",
    "brightness": "0/5/1",
    "rel_dimming": "0/5/2",
    "scene": "0/5/3",
    "colour_temperature": "0/5/4",
    "rel_colour_temperature": "0/5/5",
    "rgb": "0/5/6",
    "rgbw": "0/5/7",
    "xyy": "0/5/8",
}


def _input(channel: int) -> dict[str, str]:
    return {
        "switch": f"4/0/{channel}",
        "dimming": f"4/1/{channel}",
        "illuminance": f"4/2/{channel}",
        "scene": f"4/3/{channel}",
    }


# Input-channel group addresses; tests use channels 0..4.
INPUTS = {c: _input(c) for c in range(5)}

# ETS input-channel bindings (device short address, instance, type, function,
# scene, threshold %) for the channels above. Type: 1 push button,
# 2 occupancy, 3 light. Function: 0 toggle, 1 on, 2 off, 3 toggle+dim, 4 scene.
INPUT_BINDINGS = {
    0: {"device": 0, "instance": 0, "type": 1, "function": 0},
    1: {"device": 0, "instance": 1, "type": 1, "function": 3},
    2: {"device": 0, "instance": 2, "type": 1, "function": 4, "scene": 5},
    3: {"device": 1, "instance": 0, "type": 2},
    4: {"device": 2, "instance": 0, "type": 3, "threshold": 20},
}

# Function index within each block, shared by channels and groups.
CHANNEL_FN = {
    "switch": 0,
    "switch_status": 1,
    "brightness": 2,
    "brightness_status": 3,
    "rel_dimming": 4,
    "fault": 5,
    "colour_temperature": 6,
    "colour_temperature_status": 7,
    "rel_colour_temperature": 8,
    "rgb": 9,
    "rgb_status": 10,
    "rgbw": 11,
    "rgbw_status": 12,
    "xyy": 13,
    "xyy_status": 14,
}
GROUP_FN = {name: index - (1 if index > 5 else 0) for name, index in CHANNEL_FN.items()}
GROUP_FN.pop("fault")
BROADCAST_FN = {
    "switch": "BROADCAST_SWITCH",
    "brightness": "BROADCAST_BRIGHTNESS",
    "rel_dimming": "BROADCAST_REL_DIMMING",
    "scene": "BROADCAST_SCENE",
    "colour_temperature": "BROADCAST_COLOUR_TEMPERATURE",
    "rel_colour_temperature": "BROADCAST_REL_COLOUR_TEMPERATURE",
    "rgb": "BROADCAST_RGB",
    "rgbw": "BROADCAST_RGBW",
    "xyy": "BROADCAST_XYY",
}
INPUT_FN = {"switch": 0, "dimming": 1, "illuminance": 2, "scene": 3}


def _ga(raw: str) -> int:
    main, middle, sub = (int(p) for p in raw.split("/"))
    return (main << 11) | (middle << 8) | sub


def associations() -> list[tuple[int, int]]:
    """(tsap, asap) pairs for the whole scenario."""
    pairs: list[tuple[int, int]] = []
    gas: list[int] = []
    for channel, fns in CHANNELS.items():
        for name, ga_str in fns.items():
            ga = _ga(ga_str)
            gas.append(ga)
            pairs.append((ga, tables.channel_object(channel, CHANNEL_FN[name])))
    for name, ga_str in GROUP0.items():
        ga = _ga(ga_str)
        gas.append(ga)
        pairs.append((ga, tables.group_object(0, GROUP_FN[name])))
    for name, ga_str in BROADCAST.items():
        ga = _ga(ga_str)
        gas.append(ga)
        pairs.append((ga, getattr(tables, BROADCAST_FN[name])))
    for channel, fns in INPUTS.items():
        for name, ga_str in fns.items():
            ga = _ga(ga_str)
            gas.append(ga)
            pairs.append((ga, tables.input_object(channel, INPUT_FN[name])))
    # TSAPs index into the deduplicated address table (group_addresses()).
    tsap_of = {ga: i + 1 for i, ga in enumerate(group_addresses())}
    return [(tsap_of[ga], asap) for ga, asap in pairs]


def group_addresses() -> list[int]:
    return sorted(
        {_ga(ga) for ch in CHANNELS.values() for ga in ch.values()}
        | {_ga(ga) for ga in GROUP0.values()}
        | {_ga(ga) for ga in BROADCAST.values()}
        | {_ga(ga) for ch in INPUTS.values() for ga in ch.values()}
    )


PARAMETER_SIZE = 564  # MAIN_ParameterSize from the generated header


def parameters() -> bytes:
    """Parameter image: defaults + channel 2 fixed-on 50% + group 0 members
    + the input-channel bindings in INPUT_BINDINGS."""
    params = bytearray(PARAMETER_SIZE)
    # Channel blocks at offset 4, size 5:
    # [mode, percent, groups lo, groups hi, colour-control].
    # mode 0 = last level, 1 = fixed level; colour-control is ETS visibility
    # only and stays 0 (None) — the firmware probes gear capabilities.
    for channel in range(4):
        base = 4 + channel * 5
        if channel in (0, 1):
            params[base + 2] |= 0x01  # member of DALI group 0
        if channel == 2:
            params[base + 0] = 1  # SwitchOnPolicy::FixedLevel
            params[base + 1] = 50  # percent
    # Input-channel blocks at offset 372, size 6:
    # [device, instance, type, function, scene, threshold].
    for channel, binding in INPUT_BINDINGS.items():
        base = 372 + channel * 6
        params[base + 0] = binding["device"]
        params[base + 1] = binding["instance"]
        params[base + 2] = binding["type"]
        params[base + 3] = binding.get("function", 0)
        params[base + 4] = binding.get("scene", 1)
        params[base + 5] = binding.get("threshold", 0)
    return bytes(params)
