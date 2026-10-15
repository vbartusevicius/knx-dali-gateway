// ETS parameter-page script for the DALI commissioning page. Buttons call the
// handlers below; each sends a KNX function-property command to the gateway on
// object index 160 (OpenKNX convention) via online.invokeFunctionProperty.
// Command property ids: 1 scan, 2 assign (data[0]: 0 = unaddressed, 1 = all),
// 3 replace (data[0] = short address), 4 identify (data[0] = short address),
// 5 reset all DALI short addresses (no data), 6 scan input devices,
// 7 assign input devices (data[0]: 0 = new, 1 = all),
// 8 lamp finder (data[0] = short address 0..63 or 0xFF, data[1] = 0 stop,
// 1 blink on/off, 2 smooth pulse).
// Response codes: 0 accepted, 1 busy, 2 invalid argument.
// The status refresh reads function-property state id 1 through
// online.invokeFunctionPropertyState, which returns
// [phase, progress, found, last error, presence bitmap x8, fault bitmap x8].
// Input status uses state id 2: [presence bitmap x8, per-device byte x64];
// a device byte is only valid when the presence bit is set and packs
// bits 0-4 = instance count, bit 5 = has push button,
// bit 6 = has occupancy, bit 7 = has light sensor.

var GW_FUNCTION_PROPERTY_OBJECT = 160;
var GW_STATE_PROPERTY = 1;
var GW_INPUT_STATE_PROPERTY = 2;

function gwSendCommand(device, online, progress, propertyId, data, text) {
    progress.setText(text);
    online.connect();
    var resp = online.invokeFunctionProperty(GW_FUNCTION_PROPERTY_OBJECT, propertyId, data);
    online.disconnect();
    if (!resp || resp.length < 1) {
        throw new Error("No response from the gateway.");
    }
    if (resp[0] === 1) {
        throw new Error("The gateway is busy with another DALI job; try again later.");
    }
    if (resp[0] !== 0) {
        throw new Error("The gateway rejected the command (code " + resp[0] + ").");
    }
    progress.setText(text + " - command accepted");
}

function gwChannel(device) {
    var channel = device.getParameterByName("CH_DaliChannel");
    if (channel === null || channel === undefined) {
        channel = device.getParameterByName("DaliChannel");
    }
    if (channel === null || channel === undefined) {
        throw new Error("DALI channel parameter not found.");
    }
    return channel.value;
}

function daliScan(device, online, progress, context) {
    gwSendCommand(device, online, progress, 1, [], "Scanning the DALI bus");
}

function daliAssignNew(device, online, progress, context) {
    gwSendCommand(device, online, progress, 2, [0], "Assigning addresses to new devices");
}

function daliAssignAll(device, online, progress, context) {
    gwSendCommand(device, online, progress, 2, [1], "Re-addressing all devices");
}

function daliResetAddresses(device, online, progress, context) {
    gwSendCommand(device, online, progress, 5, [], "Resetting all DALI short addresses");
}

function daliIdentify(device, online, progress, context) {
    var channel = gwChannel(device);
    gwSendCommand(device, online, progress, 4, [channel], "Identifying DALI channel " + channel);
}

function gwContextChannel(device, context) {
    if (context && context.ch !== undefined && context.ch !== null) {
        var idx = Number(context.ch) - 1;
        if (idx >= 0 && idx <= 63) {
            return idx;
        }
    }
    return gwChannel(device);
}

function daliFinderBlink(device, online, progress, context) {
    var channel = gwChannel(device);
    gwSendCommand(device, online, progress, 8, [channel, 1],
                  "Lamp Finder (blink on/off) on DALI channel " + channel);
}

function daliFinderPulse(device, online, progress, context) {
    var channel = gwChannel(device);
    gwSendCommand(device, online, progress, 8, [channel, 2],
                  "Lamp Finder (smooth pulse) on DALI channel " + channel);
}

function daliFinderNext(device, online, progress, context) {
    var param = device.getParameterByName("CH_DaliChannel") || device.getParameterByName("DaliChannel");
    var next = param ? ((Number(param.value) + 1) & 0x3F) : 0;
    if (param) {
        param.value = next;
    }
    gwSendCommand(device, online, progress, 8, [next, 1],
                  "Lamp Finder stepped to DALI channel " + next + " (blinking)");
}

function daliFinderStop(device, online, progress, context) {
    gwSendCommand(device, online, progress, 8, [0xFF, 0], "Stopping Lamp Finder and restoring level");
}

function channelFinderBlink(device, online, progress, context) {
    var ch = gwContextChannel(device, context);
    gwSendCommand(device, online, progress, 8, [ch, 1], "Lamp Finder (blink on/off) on channel " + (ch + 1));
}

function channelFinderPulse(device, online, progress, context) {
    var ch = gwContextChannel(device, context);
    gwSendCommand(device, online, progress, 8, [ch, 2], "Lamp Finder (smooth pulse) on channel " + (ch + 1));
}

function channelReplace(device, online, progress, context) {
    var ch = gwContextChannel(device, context);
    gwSendCommand(device, online, progress, 3, [ch], "Replacing ballast on channel " + (ch + 1));
}

function daliReplace(device, online, progress, context) {
    var channel = gwChannel(device);
    gwSendCommand(device, online, progress, 3, [channel], "Replacing DALI channel " + channel);
}

function gwPhaseName(phase) {
    var names = ["Idle", "Scanning", "Addressing", "Replacing", "Identifying", "Done", "Failed"];
    return phase < names.length ? names[phase] : "Unknown(" + phase + ")";
}

function gwCountBits(resp, offset) {
    var count = 0;
    for (var i = 0; i < 8; ++i) {
        var b = resp[offset + i];
        for (var bit = 0; bit < 8; ++bit) {
            if ((b & (1 << bit)) !== 0) {
                ++count;
            }
        }
    }
    return count;
}

function daliStatus(device, online, progress, context) {
    progress.setText("Reading commissioning status");
    online.connect();
    var resp = online.invokeFunctionPropertyState(GW_FUNCTION_PROPERTY_OBJECT, GW_STATE_PROPERTY, []);
    online.disconnect();
    if (!resp || resp.length < 20) {
        throw new Error("No status response from the gateway.");
    }
    var present = gwCountBits(resp, 4);
    var faulty = gwCountBits(resp, 12);
    progress.setText("Phase " + gwPhaseName(resp[0]) + ", " + resp[1] + "% done, " + resp[2] +
                     " found, last error " + resp[3] + " - " + present + " present, " + faulty +
                     " faulty");
}

function inputScan(device, online, progress, context) {
    gwSendCommand(device, online, progress, 6, [], "Scanning for input devices");
}

function inputAssignNew(device, online, progress, context) {
    gwSendCommand(device, online, progress, 7, [0], "Addressing new input devices");
}

function inputAssignAll(device, online, progress, context) {
    gwSendCommand(device, online, progress, 7, [1], "Addressing all input devices");
}

function inputStatus(device, online, progress, context) {
    progress.setText("Reading input device status");
    online.connect();
    var resp = online.invokeFunctionPropertyState(GW_FUNCTION_PROPERTY_OBJECT, GW_INPUT_STATE_PROPERTY, []);
    online.disconnect();
    if (!resp || resp.length < 72) {
        throw new Error("No input status response from the gateway.");
    }
    var devices = 0;
    var instances = 0;
    var buttons = 0;
    var occupancy = 0;
    var light = 0;
    for (var i = 0; i < 64; ++i) {
        if ((resp[i >> 3] & (1 << (i & 7))) === 0) {
            continue;
        }
        var summary = resp[8 + i];
        ++devices;
        instances += summary & 0x1F;
        if (summary & 0x20) ++buttons;
        if (summary & 0x40) ++occupancy;
        if (summary & 0x80) ++light;
    }
    progress.setText(devices + " input devices, " + instances + " instances - " +
                     buttons + " with buttons, " + occupancy + " with occupancy, " +
                     light + " with light sensors");
}
