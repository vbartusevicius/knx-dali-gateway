"""Interactive local dev & scenario server for the KNX-DALI Gateway Web UI."""

from __future__ import annotations

import json
import sys
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parents[1]
DATA_DIR = ROOT / "data"
START_TIME = time.time()


class LabState:
    def __init__(self) -> None:
        self.scenario = "mixed"
        self.oled_geom = 2
        self.oled_page = 0
        self.knx_ia = (1 << 12) | (1 << 8) | 20
        self.knx_prog = False
        self.knx_bus_ok = True
        self.psu_on = True
        self.psu_short = False
        self.finder_active = False
        self.finder_ch = 0
        self.comm_phase = "idle"
        self.comm_progress = 0
        self.tx_frames = 14820
        self.collisions = 1
        self.errors = 2
        self.trace_active = True
        self.trace_auto = True
        self.trace_seq = 106
        self.trace_ring: list[dict] = []
        self.channels: list[list] = []
        self.details: dict[int, dict] = {}
        self.groups = [0] * 16
        self.inputs = [[0, 0] for _ in range(64)]
        self.config = {
            "wifi_enabled": True,
            "ap_fallback_enabled": True,
            "wifi_power_save": True,
            "wifi_ssid": "Office-IoT",
            "wifi_password": "********",
            "hostname": "knx-dali-gw",
            "mqtt_enabled": True,
            "mqtt_host": "192.168.1.10",
            "mqtt_port": 1883,
            "mqtt_username": "ha",
            "mqtt_password": "********",
            "mqtt_base_topic": "knx_dali",
            "ha_discovery_enabled": True,
            "ha_discovery_prefix": "homeassistant",
            "oled_geometry": 2,
            "oled_sleep_seconds": 60,
            "internal_dali_psu_enabled": True,
        }
        self.apply_scenario("mixed")

    def apply_scenario(self, name: str) -> None:
        self.scenario = name
        self.psu_short = False
        self.finder_active = False
        self.finder_ch = 0
        self.knx_bus_ok = True
        self.comm_phase = "idle"
        self.comm_progress = 0
        self.channels = []
        self.details = {}
        self.groups = [0] * 16
        self.inputs = [[0, 0] for _ in range(64)]

        for i in range(64):
            present = 0
            faulty = 0
            level = 0
            source = "readback"
            dt8 = 0
            kelvin = 4000
            r, g, b = 0, 0, 0
            status_byte = 0
            groups_mask = 0
            dev_type = 6
            d4i = 0
            manual_ovr = 0
            d4i_deciw = 0
            full_deciw = 150
            idle_centiw = 40
            pwr_cfg = 1
            sw_only = 0

            if name == "mixed":
                if i < 24:
                    present = 1
                    groups_mask = 1 << (i % 5)  # Populate G00..G04; G05..G15 are empty!
                    if i < 4:
                        # DT6 LED spots (12W)
                        dev_type = 6
                        full_deciw = 120
                        level = 210 if i % 2 == 0 else 0
                    elif i == 4:
                        # DT4 Incandescent phase-cut dimmer (60W)
                        dev_type = 4
                        full_deciw = 600
                        level = 180
                    elif i == 5:
                        # DT7 Relay Switch (On/Off non-dimmable exhaust fan / Contactor, 25W)
                        dev_type = 7
                        sw_only = 1
                        groups_mask = 0
                        full_deciw = 250
                        level = 254
                    elif i < 12:
                        # DT8 Tunable White panels (24W, D4i)
                        dev_type = 8
                        dt8 = 1
                        d4i = 1
                        full_deciw = 240
                        level = 230 if i in (6, 8, 9) else 0
                        kelvin = 3000 + (i - 6) * 500
                        d4i_deciw = 215 if level > 0 else 4
                    elif i < 18:
                        # DT8 RGBW LED strips (18W, D4i)
                        dev_type = 8
                        dt8 = 7
                        d4i = 1
                        full_deciw = 180
                        level = 190 if i in (12, 13, 15) else 0
                        palette = [
                            (56, 189, 248),
                            (16, 185, 129),
                            (255, 180, 90),
                            (168, 85, 247),
                            (244, 63, 94),
                            (255, 140, 20),
                        ]
                        r, g, b = palette[i - 12]
                        d4i_deciw = 148 if level > 0 else 4
                    elif i < 21:
                        # DT8 CIE xy wall washers (30W)
                        dev_type = 8
                        dt8 = 9
                        full_deciw = 300
                        level = 180 if i == 18 else 0
                    else:
                        # DT0 Fluorescent / DT2 Discharge arc lamp
                        dev_type = 0 if i < 23 else 2
                        full_deciw = 360 if dev_type == 0 else 700
                        level = 254 if i == 21 else 0
                if i == 7:
                    faulty = 1
                    status_byte = 0x02  # Lamp failure
                if i == 14:
                    faulty = 1
                    status_byte = 0x01  # Ballast fault
                self.inputs[0] = [1, 0x24, [1, 1, 1, 1]]  # 4 push buttons (I00..I03)
                self.inputs[1] = [1, 0xC2, [3, 4]]        # I00 Occupancy + I01 Light sensor
                self.inputs[2] = [1, 0x22, [2, 2]]        # I00..I01 Toggle switches

            elif name == "full64":
                present = 1
                groups_mask = 1 << (i % 16)
                dt8 = 1 if i % 2 == 0 else 7
                dev_type = 8
                d4i = 1
                level = 200 if i % 3 != 0 else 0
                kelvin = 3500
                r, g, b = (56, 189, 248) if dt8 == 7 else (0, 0, 0)
                d4i_deciw = 142 if level > 0 else 4

            elif name == "faults":
                if i < 32:
                    present = 1
                    level = 160 if i % 4 == 0 else 0
                    dt8 = 1
                if i in (2, 7, 14, 19, 25, 31):
                    faulty = 1
                    status_byte = 0x02 if i % 2 == 1 else 0x01
                self.psu_short = True

            elif name == "finder":
                if i < 16:
                    present = 1
                    dt8 = 1
                    level = 254 if i == 9 else 120
                self.finder_active = True
                self.finder_ch = 9
                self.comm_phase = "scanning"
                self.comm_progress = 65

            if level > 0:
                status_byte |= 0x04

            self.channels.append([
                present,
                faulty,
                level,
                source,
                dt8,
                kelvin,
                r,
                g,
                b,
                status_byte,
                groups_mask,
                dev_type,
                d4i,
                manual_ovr,
                d4i_deciw,
                full_deciw,
                idle_centiw,
                pwr_cfg,
                sw_only,
            ])
            self.details[i] = {
                "address": i,
                "present": bool(present),
                "faulty": bool(faulty),
                "level": level,
                "source": source,
                "status_byte": status_byte,
                "gear_failure": bool(status_byte & 0x01),
                "lamp_failure": bool(status_byte & 0x02),
                "arc_on": bool(status_byte & 0x04),
                "limit_error": False,
                "fade_running": False,
                "reset_state": False,
                "missing_short": False,
                "power_cycle": False,
                "device_type": dev_type,
                "version": 2,
                "physical_min": 1,
                "min_level": 10,
                "max_level": 254,
                "power_on_level": 254,
                "system_failure_level": 254,
                "fade_time": 4,
                "fade_rate": 7,
                "groups": groups_mask,
                "probed": True,
                "kelvin": kelvin,
                "r": r,
                "g": g,
                "b": b,
                "w": 0,
                "xy_x": 20480,
                "xy_y": 21500,
                "d4i_power_supported": bool(d4i),
                "use_manual_power_override": bool(manual_ovr),
                "d4i_active_power_deciwatts": d4i_deciw,
                "d4i_active_energy_wh": 1240 + i * 35 if d4i else 0,
                "full_power_deciwatts": full_deciw,
                "idle_power_centiwatts": idle_centiw,
                "power_configured": bool(pwr_cfg),
                "switch_only": bool(sw_only),
                "scenes": [254 if s == 0 else (128 if s == 1 else (64 if s == 2 else 255)) for s in range(16)],
            }

        for g in range(16):
            members = [c[2] for c in self.channels if c[0] and (c[10] & (1 << g))]
            self.groups[g] = max(members) if members else 0

        now_ms = int(time.time() * 1000)
        self.trace_ring = [
            {
                "seq": 104,
                "epoch_ms": now_ms - 2000,
                "uptime_ms": 540120,
                "bits": 16,
                "frame": 0x0EC8,
                "target": "A07",
                "mnemonic": "DAPC",
                "detail": "arc=200 (79%)",
                "status": "OK",
            },
            {
                "seq": 105,
                "epoch_ms": now_ms - 1500,
                "uptime_ms": 540620,
                "bits": 16,
                "frame": 0x0F90,
                "target": "A07",
                "mnemonic": "QUERY_STATUS",
                "detail": "reply=0x06 (lampFail=1, arcOn=1)",
                "status": "OK",
            },
            {
                "seq": 106,
                "epoch_ms": now_ms - 800,
                "uptime_ms": 541320,
                "bits": 24,
                "frame": 0x020155,
                "target": "D00/I00",
                "mnemonic": "PART103_EVENT",
                "detail": "Short press event",
                "status": "OK",
            },
        ]

    def _push_trace(self, target_str: str, mnemonic: str, detail: str, frame: int = 0x00FE, bits: int = 16) -> None:
        if not self.trace_active:
            return
        self.trace_seq += 1
        now_ms = int(time.time() * 1000)
        self.trace_ring.insert(
            0,
            {
                "seq": self.trace_seq,
                "epoch_ms": now_ms,
                "uptime_ms": int((time.time() - START_TIME) * 1000) + 541320,
                "bits": bits,
                "frame": frame,
                "target": target_str,
                "mnemonic": mnemonic,
                "detail": detail,
                "status": "OK",
            },
        )
        if len(self.trace_ring) > 1024:
            self.trace_ring.pop()

    def handle_command(self, cmd: dict) -> None:
        action = cmd.get("action", "")
        if action == "sync_time":
            return
        target = cmd.get("target", "ch")
        idx = int(cmd.get("index", 0))
        self.tx_frames += 1
        tgt_label = "BROADCAST" if target == "broadcast" else (f"G{idx:02d}" if target == "grp" else f"A{idx:02d}")
        self._push_trace(tgt_label, action.upper(), json.dumps(cmd))

        if action == "find_lamp":
            mode = cmd.get("mode", "blink")
            if mode == "stop":
                self.finder_active = False
            else:
                self.finder_active = True
                self.finder_ch = idx
            return
        if action == "reset_psu":
            self.psu_short = False
            return
        if action == "scan_bus":
            self.comm_phase = "scanning"
            self.comm_progress = 100
            return

        indices: list[int] = []
        if target == "broadcast":
            indices = [i for i in range(64) if self.channels[i][0]]
        elif target == "grp":
            indices = [i for i in range(64) if self.channels[i][0] and (self.channels[i][10] & (1 << idx))]
        elif 0 <= idx < 64:
            indices = [idx]

        for i in indices:
            ch = self.channels[i]
            if action == "switch_on":
                ch[2] = 254
            elif action == "switch_off":
                ch[2] = 0
            elif action == "set_level":
                lvl = int(cmd.get("level", 200))
                if ch[18] or ch[11] == 7:
                    ch[2] = 254 if lvl > 0 else 0
                else:
                    ch[2] = lvl
            elif action == "set_cct":
                ch[5] = int(cmd.get("kelvin", 4000))
                ch[6], ch[7], ch[8] = 0, 0, 0
                if ch[2] == 0:
                    ch[2] = 200
            elif action in ("set_rgb", "set_rgbw"):
                ch[6] = int(cmd.get("r", 255))
                ch[7] = int(cmd.get("g", 180))
                ch[8] = int(cmd.get("b", 100))
                if "w" in cmd:
                    self.details[i]["w"] = int(cmd.get("w", 0))
                if ch[2] == 0:
                    ch[2] = 200
            elif action == "configure_gear":
                d = self.details[i]
                for k in ("min_level", "max_level", "power_on_level", "system_failure_level", "fade_time", "fade_rate", "dimming_curve"):
                    if k in cmd:
                        d[k] = int(cmd[k])
                if "device_type" in cmd:
                    d["device_type"] = int(cmd["device_type"])
                    ch[11] = d["device_type"]
                if "dt8_mask" in cmd:
                    ch[4] = int(cmd["dt8_mask"]) & 0x0F
                if "groups_mask" in cmd:
                    d["groups"] = int(cmd["groups_mask"]) & 0xFFFF
                    ch[10] = d["groups"]
                    for g in range(16):
                        members = [c[2] for c in self.channels if c[0] and (c[10] & (1 << g))]
                        self.groups[g] = max(members) if members else 0
                if "switch_only" in cmd:
                    d["switch_only"] = bool(cmd["switch_only"]) or (ch[11] == 7)
                    ch[18] = 1 if d["switch_only"] else 0
                if "full_power_deciwatts" in cmd:
                    d["full_power_deciwatts"] = int(cmd["full_power_deciwatts"])
                    ch[15] = d["full_power_deciwatts"]
                    ch[17] = 1
                if "idle_power_centiwatts" in cmd:
                    d["idle_power_centiwatts"] = int(cmd["idle_power_centiwatts"])
                    ch[16] = d["idle_power_centiwatts"]
                    ch[17] = 1
                if "use_manual_power_override" in cmd:
                    d["use_manual_power_override"] = bool(cmd["use_manual_power_override"])
                    ch[13] = 1 if d["use_manual_power_override"] else 0

            if ch[12]:
                ch[14] = (
                    round((ch[15] * (ch[2] / 254.0)) + ch[16] / 10.0)
                    if ch[2] > 0
                    else round(ch[16] / 10.0)
                )

        # Recompute group levels from dimmable members (or all members if switch-only group)
        for g in range(16):
            dim_members = [c[2] for c in self.channels if c[0] and (c[10] & (1 << g)) and not (c[18] or c[11] == 7)]
            all_members = [c[2] for c in self.channels if c[0] and (c[10] & (1 << g))]
            if dim_members:
                self.groups[g] = dim_members[0] if target == "grp" and idx == g else max(dim_members)
            else:
                self.groups[g] = max(all_members) if all_members else 0

    def live_json(self) -> dict:
        present_cnt = sum(1 for c in self.channels if c[0])
        active_cnt = sum(1 for c in self.channels if c[0] and c[2] > 0)
        faulty_cnt = sum(1 for c in self.channels if c[1])
        return {
            "type": "live",
            "uptime_s": int(time.time() - START_TIME) + 14 * 86400 + 6 * 3600,
            "epoch_ms": int(time.time() * 1000),
            "free_heap": 234496,
            "min_heap": 212992,
            "fw_version": "26.10.15.1915",
            "partition": "ota_0",
            "chip_id": "f5aa11f",
            "knx": {
                "ia": self.knx_ia,
                "configured": True,
                "prog_mode": self.knx_prog,
                "bus_ok": self.knx_bus_ok,
            },
            "dali": {
                "present": present_cnt,
                "active": active_cnt,
                "faulty": faulty_cnt,
                "inputs": sum(1 for x in self.inputs if x[0]),
                "psu_on": self.psu_on,
                "psu_short": self.psu_short,
                "finder_active": self.finder_active,
                "finder_ch": self.finder_ch,
                "tx_frames": self.tx_frames,
                "collisions": self.collisions,
                "errors": faulty_cnt + self.collisions,
                "comm_phase": self.comm_phase,
                "comm_progress": self.comm_progress,
            },
            "net": {
                "wifi_state": 2,
                "ssid": self.config["wifi_ssid"],
                "ip": "192.168.1.42",
                "rssi": -58,
                "mqtt": True,
                "ha": True,
                "ws": 1,
            },
            "active_scenes": [0, 1, 2, 3],
            "groups": self.groups,
            "channels": self.channels,
            "inputs": self.inputs,
            "input_events": (
                [
                    [0, 2, 1, 0x04, 3],
                    [1, 0, 3, 0x0A, 18],
                    [2, 0, 4, 420, 45],
                ]
                if any(x[0] for x in self.inputs)
                else []
            ),
        }


LAB = LabState()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt: str, *args) -> None:
        pass

    def _send_json(self, obj: object, status: int = 200) -> None:
        raw = json.dumps(obj).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        path = parsed.path
        if path in ("/", "/index.html"):
            html = (DATA_DIR / "index.html").read_text(encoding="utf-8")
            # Inject dev-only scenario switcher + WebSocket bridge in memory so
            # gateway/data/ remains 100% pure production firmware code.
            dev_shim = """
<script>
(function() {
  class DevWebSocket {
    constructor() {
      this.readyState = 1;
      setTimeout(() => { if (this.onopen) this.onopen(); this._poll(); }, 10);
      setInterval(() => this._poll(), 1000);
    }
    async _poll() {
      try {
        const r = await fetch('/api/state');
        if (r.ok && this.onmessage) this.onmessage({ data: await r.text() });
      } catch (_) {}
    }
    async send(raw) {
      await fetch('/api/command', { method: 'POST', headers: {'Content-Type':'application/json'}, body: raw });
      await this._poll();
    }
  }
  DevWebSocket.OPEN = 1;
  window.WebSocket = DevWebSocket;
  window.applyScenario = async function(name) {
    const r = await fetch('/api/scenario', { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify({scenario: name}) });
    if (r.ok && typeof handleLiveState === 'function') {
      handleLiveState(await r.json());
      if (typeof loadErrorLogs === 'function') loadErrorLogs();
      if (typeof loadDebugTrace === 'function') loadDebugTrace();
    }
  };
  document.addEventListener('DOMContentLoaded', () => {
    const params = new URLSearchParams(location.search);
    if (!params.has('hide_dev_bar')) {
      const right = document.querySelector('.topbar-right');
      if (right) {
        const bar = document.createElement('div');
        bar.className = 'scenario-bar';
        bar.innerHTML = `<span class="scenario-label">Scenario:</span>
          <select onchange="applyScenario(this.value)">
            <option value="mixed">Mixed Lamps &amp; Types (24 Devices)</option>
            <option value="full64">Full 64 Lamps (All Addressed)</option>
            <option value="faults">Faults &amp; Bus Short Emergency</option>
            <option value="finder">Lamp Finder &amp; Scan Active</option>
            <option value="empty">First Boot (Uncommissioned)</option>
          </select>`;
        right.insertBefore(bar, right.firstChild);
      }
    }
    setTimeout(() => {
      if (params.has('tab')) {
        const btn = document.querySelector(`.nav-tab[data-tab="${params.get('tab')}"]`);
        if (btn) btn.click();
      }
      if (params.has('inptab') && typeof setInspectorTab === 'function') {
        setInspectorTab(params.get('inptab'));
      }
      if (params.has('logmode') && typeof setLogViewMode === 'function') {
        setLogViewMode(params.get('logmode'));
      }
      if (params.has('sec') && typeof selectSettingsSection === 'function') {
        const sBtn = document.querySelector(`.settings-nav-item[data-sec="${params.get('sec')}"]`);
        selectSettingsSection(params.get('sec'), sBtn);
      }
    }, 350);
  });
})();
</script>
"""
            data = html.replace("<script src=\"app.js\"></script>", dev_shim + "<script src=\"app.js\"></script>").encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            self.wfile.write(data)
            return
        if path == "/app.css":
            data = (DATA_DIR / "app.css").read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "text/css; charset=utf-8")
            self.end_headers()
            self.wfile.write(data)
            return
        if path == "/app.js":
            data = (DATA_DIR / "app.js").read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "application/javascript; charset=utf-8")
            self.end_headers()
            self.wfile.write(data)
            return
        if path == "/api/state":
            self._send_json(LAB.live_json())
            return
        if path == "/api/channel":
            qs = parse_qs(parsed.query)
            ch = int(qs.get("ch", ["0"])[0])
            self._send_json(LAB.details.get(ch, LAB.details[0]))
            return
        if path == "/api/config":
            self._send_json(LAB.config)
            return
        if path == "/api/logs":
            now_ms = int(time.time() * 1000)
            self._send_json({
                "entries": [
                    {
                        "seq": 3,
                        "epoch_ms": now_ms - 180000,
                        "uptime_ms": 421000,
                        "count": 1,
                        "address": 7,
                        "source": "DALI",
                        "severity": "ERROR",
                        "code": 106,
                        "symbol": "DALI_LAMP_FAILURE",
                        "title": "Lamp Failure",
                        "detail": "QUERY_STATUS bit 1 set on A07",
                        "description": "The ballast at A07 reports an open or failed LED load.",
                        "remedy": "Check LED module wiring and replace lamp if worn.",
                    },
                    {
                        "seq": 2,
                        "epoch_ms": now_ms - 540000,
                        "uptime_ms": 260000,
                        "count": 1,
                        "address": 65535,
                        "source": "DALI",
                        "severity": "WARN",
                        "code": 103,
                        "symbol": "DALI_FRAME_COLLISION",
                        "title": "Bus Frame Collision",
                        "detail": "Simultaneous multi-master transmission detected on DA+/DA-",
                        "description": "Two devices transmitted on the DALI bus at the same time.",
                        "remedy": "Automatically retried by the gateway; check for noisy sensor wiring if frequent.",
                    },
                    {
                        "seq": 1,
                        "epoch_ms": now_ms - 920000,
                        "uptime_ms": 120000,
                        "count": 2,
                        "address": 14,
                        "source": "DALI",
                        "severity": "ERROR",
                        "code": 107,
                        "symbol": "DALI_GEAR_FAILURE",
                        "title": "Ballast Hardware Fault",
                        "detail": "QUERY_STATUS bit 0 set on A14",
                        "description": "The ballast at A14 reported an internal driver fault.",
                        "remedy": "Inspect ballast supply voltage or use Assign New Ballast.",
                    },
                ]
            })
            return
        if path == "/api/debug":
            self._send_json({
                "active": self.trace_active if hasattr(self, "trace_active") else LAB.trace_active,
                "auto_trigger": LAB.trace_auto,
                "capacity": 1024,
                "buffered": len(LAB.trace_ring),
                "total_frames": LAB.tx_frames,
                "total_collisions": LAB.collisions,
                "total_errors": 2,
                "entries": LAB.trace_ring,
            })
            return
        self.send_error(404)

    def do_POST(self) -> None:
        length = int(self.headers.get("Content-Length", "0"))
        body = json.loads(self.rfile.read(length).decode("utf-8")) if length > 0 else {}
        if self.path == "/api/debug":
            act = body.get("action", "")
            if act == "start":
                LAB.trace_active = True
            elif act == "stop":
                LAB.trace_active = False
            elif act == "clear":
                LAB.trace_ring.clear()
            if "auto_trigger" in body:
                LAB.trace_auto = bool(body["auto_trigger"])
            self._send_json({"ok": True})
            return
        if self.path == "/api/scenario":
            LAB.apply_scenario(body.get("scenario", "mixed"))
            self._send_json(LAB.live_json())
            return
        if self.path == "/api/command":
            LAB.handle_command(body)
            self._send_json({"ok": True})
            return
        if self.path == "/api/config":
            LAB.config.update(body)
            self._send_json({"ok": True})
            return
        self._send_json({"ok": True})


def main() -> None:
    port = int(sys.argv[1]) if len(sys.argv[1:]) >= 1 else 8090
    server = HTTPServer(("127.0.0.1", port), Handler)
    print(f"KNX-DALI Web UI Dev & Scenario Server listening on http://127.0.0.1:{port}")
    server.serve_forever()


if __name__ == "__main__":
    main()
