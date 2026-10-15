"use strict";

let ws = null;
let lampFilter = "all";
let latestState = null;
window.lastLogData = { entries: [] };
window.lastTraceData = { entries: [] };

function initTabs() {
  document.querySelectorAll(".tabbar .tab").forEach((btn) => {
    btn.addEventListener("click", () => {
      document.querySelectorAll(".tabbar .tab").forEach((b) => b.classList.remove("active"));
      document.querySelectorAll(".panel").forEach((p) => p.classList.remove("active"));
      btn.classList.add("active");
      const target = document.getElementById("panel-" + btn.dataset.tab);
      if (target) target.classList.add("active");
      if (btn.dataset.tab === "configuration") loadConfig();
      if (btn.dataset.tab === "logs") {
        loadDebugTrace();
        loadErrorLogs();
        loadErrorCatalog();
      }
    });
  });

  const scenesBar = document.getElementById("scenes-bar");
  for (let s = 0; s < 16; s++) {
    const b = document.createElement("button");
    b.className = "btn btn-sm mono";
    b.textContent = "S" + String(s + 1).padStart(2, "0");
    b.title = "Click: Recall Scene #" + (s + 1) + " | Shift+Click: Store Current Levels to Scene #" + (s + 1);
    b.onclick = (ev) => {
      if (ev.shiftKey) {
        sendCmd({ action: "store_scene", scene: s });
      } else {
        sendCmd({ action: "recall_scene", scene: s });
      }
    };
    scenesBar.appendChild(b);
  }

  document.getElementById("bcast-level").addEventListener("input", (e) => {
    document.getElementById("bcast-level-val").textContent = e.target.value;
  });
  document.getElementById("bcast-cct").addEventListener("input", (e) => {
    document.getElementById("bcast-cct-val").textContent = e.target.value + "K";
  });
  document.getElementById("btn-oled-page").onclick = () => sendCmd({ action: "oled_page" });
  document.getElementById("btn-oled-geom").onclick = () => sendCmd({ action: "oled_toggle" });
}

function formatIa(ia) {
  if (!ia || ia === 0xffff) return "UNPROG";
  return ((ia >> 12) & 0xf) + "." + ((ia >> 8) & 0xf) + "." + (ia & 0xff);
}

function formatUptime(sec) {
  const d = Math.floor(sec / 86400);
  const h = Math.floor((sec % 86400) / 3600);
  const m = Math.floor((sec % 3600) / 60);
  const s = sec % 60;
  if (d > 0) return `${d}d ${String(h).padStart(2, "0")}h`;
  return `${String(h).padStart(2, "0")}:${String(m).padStart(2, "0")}:${String(s).padStart(2, "0")}`;
}

function formatTimestamp(epochMs, uptimeMs) {
  if (epochMs && epochMs > 1700000000000) {
    return new Date(epochMs).toISOString().replace("T", " ").slice(0, 19) + " UTC";
  }
  return `T+${(uptimeMs / 1000).toFixed(2)}s`;
}

function renderOledCanvas(oled) {
  if (!oled || !oled.hex) return;
  const canvas = document.getElementById("oled-canvas");
  const w = oled.w || 128;
  const h = oled.h || 32;
  if (canvas.width !== w || canvas.height !== h) {
    canvas.width = w;
    canvas.height = h;
    canvas.style.width = (w * 2) + "px";
    canvas.style.height = (h * 2) + "px";
  }
  const ctx = canvas.getContext("2d");
  const img = ctx.createImageData(w, h);
  const hex = oled.hex;
  const fg = h === 64 ? [56, 189, 248] : [248, 250, 252];
  for (let page = 0; page < h / 8; page++) {
    for (let x = 0; x < w; x++) {
      const idx = (page * w + x) * 2;
      const byte = parseInt(hex.slice(idx, idx + 2), 16) || 0;
      for (let bit = 0; bit < 8; bit++) {
        const y = page * 8 + bit;
        const p = (y * w + x) * 4;
        const on = (byte & (1 << bit)) !== 0;
        img.data[p] = on ? fg[0] : 5;
        img.data[p + 1] = on ? fg[1] : 7;
        img.data[p + 2] = on ? fg[2] : 11;
        img.data[p + 3] = 255;
      }
    }
  }
  ctx.putImageData(img, 0, 0);
  document.getElementById("oled-meta").textContent =
    `SSD1306 ${w}×${h} • Page ${(oled.page || 0) + 1}/5`;
}

function setLampFilter(filter, btn) {
  lampFilter = filter;
  document.querySelectorAll(".filter-bar .btn").forEach((b) => b.classList.remove("active"));
  if (btn) btn.classList.add("active");
  if (latestState) renderLamps(latestState.channels || []);
}

function decodeStatusBits(st) {
  const flags = [];
  if (st & 0x01) flags.push("GEAR_FAIL");
  if (st & 0x02) flags.push("LAMP_FAIL");
  if (st & 0x08) flags.push("LIMIT_ERR");
  if (st & 0x10) flags.push("FADING");
  if (st & 0x20) flags.push("RESET");
  if (st & 0x80) flags.push("PWR_CYC");
  return flags;
}

function renderLamps(channels) {
  const grid = document.getElementById("lamps-grid");
  grid.innerHTML = "";
  channels.forEach((ch, idx) => {
    const [present, faulty, level, source, dt8, kelvin, r, g, b, statusByte, groupsMask, devType] = ch;
    if (lampFilter === "present" && !present) return;
    if (lampFilter === "on" && level === 0) return;
    if (lampFilter === "fault" && !faulty && (statusByte & 0x03) === 0) return;

    const pct = Math.round((level * 100) / 254);
    const card = document.createElement("div");
    card.className =
      "lamp-card" + (level > 0 ? " on" : "") + (faulty ? " fault" : "") + (!present ? " absent" : "");

    const statusFlags = decodeStatusBits(statusByte);
    const dtLabel = dt8 ? "DT8" : devType === 6 ? "DT6" : `DT${devType}`;
    const addrStr = "A" + String(idx).padStart(2, "0");

    let colourControls = "";
    if (dt8 & 1) {
      colourControls += `
        <div class="lamp-slider-row">
          <span class="mono" style="font-size:11px">CCT</span>
          <input type="range" min="2000" max="6500" step="100" value="${kelvin || 4000}"
            onchange="sendCmd({action:'set_cct',target:'ch',index:${idx},kelvin:Number(this.value)})">
          <span class="mono" style="font-size:11px">${kelvin || 4000}K</span>
        </div>`;
    }

    card.innerHTML = `
      <div class="lamp-top">
        <span class="lamp-title">${addrStr}</span>
        <div class="badges">
          <span class="badge ${present ? "ok" : ""}">${present ? "PRESENT" : "ABSENT"}</span>
          <span class="badge">${dtLabel}</span>
          <span class="badge ${source === "readback" ? "ok" : "warn"}">${source}</span>
          ${faulty ? '<span class="badge err">FAULT</span>' : ""}
        </div>
      </div>
      <div class="lamp-slider-row">
        <button class="btn btn-sm" onclick="sendCmd({action:'${level > 0 ? "switch_off" : "switch_on"}',target:'ch',index:${idx}})">${level > 0 ? "OFF" : "ON"}</button>
        <input type="range" min="0" max="254" value="${level}"
          onchange="sendCmd({action:'set_level',target:'ch',index:${idx},level:Number(this.value)})">
        <span class="mono" style="min-width:44px;text-align:right">${pct}%</span>
      </div>
      ${colourControls}
      <div class="lamp-top" style="margin-top:2px">
        <span class="mono" style="font-size:11px;color:#8b9bb4">
          ARC:${level} GRP:0x${(groupsMask || 0).toString(16).padStart(4, "0").toUpperCase()}
          ${statusFlags.length ? " • " + statusFlags.join(",") : ""}
        </span>
        <div class="btn-group">
          <button class="btn btn-sm" title="Lamp Finder: Blink ON/OFF (30s, restores previous level)" onclick="sendCmd({action:'find_lamp',target:'ch',index:${idx},mode:'blink'})">Blink</button>
          <button class="btn btn-sm" title="Lamp Finder: Smooth Breathing Pulse (30s, restores previous level)" onclick="sendCmd({action:'find_lamp',target:'ch',index:${idx},mode:'pulse'})">Pulse</button>
          <button class="btn btn-sm" title="Ballast Inspector &amp; Tuning" onclick="openGearModal(${idx})">Tune</button>
        </div>
      </div>
    `;
    grid.appendChild(card);
  });
}

function renderGroups(groups) {
  const grid = document.getElementById("groups-grid");
  grid.innerHTML = "";
  groups.forEach((lvl, idx) => {
    const pct = Math.round((lvl * 100) / 254);
    const el = document.createElement("div");
    el.className = "lamp-card" + (lvl > 0 ? " on" : "");
    el.innerHTML = `
      <div class="lamp-top">
        <span class="lamp-title">Group G${String(idx).padStart(2, "0")}</span>
        <span class="badge">${pct}% (${lvl})</span>
      </div>
      <div class="lamp-slider-row">
        <button class="btn btn-sm" onclick="sendCmd({action:'switch_on',target:'grp',index:${idx}})">ON</button>
        <button class="btn btn-sm" onclick="sendCmd({action:'switch_off',target:'grp',index:${idx}})">OFF</button>
        <input type="range" min="0" max="254" value="${lvl}"
          onchange="sendCmd({action:'set_level',target:'grp',index:${idx},level:Number(this.value)})">
      </div>
    `;
    grid.appendChild(el);
  });
}

function renderInputs(inputs) {
  const grid = document.getElementById("inputs-grid");
  grid.innerHTML = "";
  let any = false;
  inputs.forEach((item, idx) => {
    const [present, summary] = item;
    if (!present && idx >= 8) return;
    any = true;
    const instCount = summary & 0x1f;
    const types = [];
    if (summary & 0x20) types.push("301-Btn");
    if (summary & 0x40) types.push("303-Occ");
    if (summary & 0x80) types.push("304-Lux");
    const el = document.createElement("div");
    el.className = "lamp-card" + (!present ? " absent" : "");
    el.innerHTML = `
      <div class="lamp-top">
        <span class="lamp-title">DEV${String(idx).padStart(2, "0")}</span>
        <span class="badge ${present ? "ok" : ""}">${present ? instCount + " inst" : "Absent"}</span>
      </div>
      <div class="mono" style="font-size:11px;color:#8b9bb4">${types.join(" ") || "No instances"}</div>
    `;
    grid.appendChild(el);
  });
  if (!any) {
    grid.innerHTML = '<div class="muted">No DALI-2 input devices scanned yet.</div>';
  }
}

function handleLiveState(data) {
  latestState = data;
  const knxPill = document.getElementById("pill-knx");
  knxPill.textContent = `KNX: ${formatIa(data.knx.ia)} ${data.knx.prog_mode ? "[PROG]" : data.knx.bus_ok ? "OK" : "SAVEB!"}`;
  knxPill.className = "pill " + (data.knx.bus_ok ? "ok" : "err");

  const daliPill = document.getElementById("pill-dali");
  daliPill.textContent = `DALI: ${data.dali.present}/64 ON:${data.dali.active} FLT:${data.dali.faulty}`;
  daliPill.className = "pill " + (data.dali.faulty > 0 ? "err" : "ok");

  const mqttPill = document.getElementById("pill-mqtt");
  mqttPill.textContent = `MQTT: ${data.net.mqtt ? "ONLINE" : "OFF"} (${data.net.ip})`;
  mqttPill.className = "pill " + (data.net.mqtt ? "ok" : "");

  document.getElementById("pill-uptime").textContent = `UP: ${formatUptime(data.uptime_s)}`;
  document.getElementById("comm-status-badge").textContent =
    `Commissioning: ${data.dali.comm_phase.toUpperCase()} (${data.dali.comm_progress}%)`;

  if (data.leds) {
    data.leds.forEach((rgb, i) => {
      const dot = document.getElementById("led-" + i);
      if (dot) {
        const c = `rgb(${rgb[0]},${rgb[1]},${rgb[2]})`;
        dot.style.background = c;
        dot.style.boxShadow = `0 0 10px ${c}`;
      }
    });
  }

  renderOledCanvas(data.oled);
  renderLamps(data.channels || []);
  renderGroups(data.groups || []);
  renderInputs(data.inputs || []);

  document.getElementById("info-fw-ver").textContent = data.fw_version || "0.1.0";
  document.getElementById("info-part").textContent = data.partition || "ota_0";
  document.getElementById("info-heap").textContent =
    `${Math.round((data.free_heap || 0) / 1024)} KB free / ${Math.round((data.min_heap || 0) / 1024)} KB min`;
}

function connectWebSocket() {
  const proto = location.protocol === "https:" ? "wss:" : "ws:";
  const url = `${proto}//${location.host}/ws`;
  const wsPill = document.getElementById("pill-ws");
  try {
    ws = new WebSocket(url);
  } catch (_) {
    setTimeout(connectWebSocket, 2000);
    return;
  }
  ws.onopen = () => {
    wsPill.innerHTML = '<span class="dot"></span>WS: Live';
    wsPill.className = "pill ok";
    syncGatewayTime();
  };
  ws.onmessage = (ev) => {
    try {
      const msg = JSON.parse(ev.data);
      if (msg.type === "live") handleLiveState(msg);
    } catch (_) {}
  };
  ws.onclose = () => {
    wsPill.innerHTML = '<span class="dot"></span>WS: Reconnecting';
    wsPill.className = "pill err";
    setTimeout(connectWebSocket, 2000);
  };
}

async function sendCmd(payload) {
  if (ws && ws.readyState === WebSocket.OPEN && payload.action !== "query_gear") {
    ws.send(JSON.stringify(payload));
    return;
  }
  await fetch("/api/command", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  });
}

async function syncGatewayTime() {
  try {
    await fetch("/api/command", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ action: "sync_time", epoch_ms: Date.now() }),
    });
  } catch (_) {}
}

async function openGearModal(ch) {
  const modal = document.getElementById("gear-modal");
  const body = document.getElementById("modal-body");
  document.getElementById("modal-title").textContent =
    `Ballast Inspector & Parameter Tuning — A${String(ch).padStart(2, "0")}`;
  body.innerHTML = '<p class="muted">Querying DALI ballast registers...</p>';
  modal.style.display = "flex";

  await fetch("/api/command", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ action: "query_gear", index: ch }),
  });
  const res = await fetch(`/api/channel?ch=${ch}`);
  const d = await res.json();

  body.innerHTML = `
    <div class="sys-info-box">
      <div><strong>Status Byte (0x90):</strong> 0x${(d.status_byte || 0).toString(16).padStart(2, "0").toUpperCase()}
        [GearFail:${d.gear_failure} LampFail:${d.lamp_failure} ArcOn:${d.arc_on} LimitErr:${d.limit_error} Fade:${d.fade_running} Reset:${d.reset_state} PwrCyc:${d.power_cycle}]
      </div>
      <div><strong>Device Type / Version / Physical Min:</strong> DT${d.device_type} • v${d.version} • PhysMin=${d.physical_min}</div>
      <div><strong>Scene Levels (S01..S16):</strong> ${(d.scenes || []).map((v, i) => `S${i + 1}:${v === 255 ? "MASK" : v}`).join(" ")}</div>
    </div>
    <div class="field-row">
      <div class="field"><span>Min Level (1..254)</span><input type="number" id="gm-min" min="1" max="254" value="${d.min_level}"></div>
      <div class="field"><span>Max Level (1..254)</span><input type="number" id="gm-max" min="1" max="254" value="${d.max_level}"></div>
      <div class="field"><span>Power-On Level (0..254)</span><input type="number" id="gm-pon" min="0" max="254" value="${d.power_on_level}"></div>
    </div>
    <div class="field-row">
      <div class="field"><span>System-Failure Level (0..254)</span><input type="number" id="gm-fail" min="0" max="254" value="${d.system_failure_level}"></div>
      <div class="field"><span>Fade Time (0..15)</span><input type="number" id="gm-ft" min="0" max="15" value="${d.fade_time}"></div>
      <div class="field"><span>Fade Rate (1..15)</span><input type="number" id="gm-fr" min="1" max="15" value="${d.fade_rate}"></div>
    </div>
    <div class="btn-group" style="margin-top:10px">
      <button class="btn btn-primary" onclick="saveGearParams(${ch})">Write Parameters to Ballast (DTR0 + 2x Store)</button>
      <button class="btn" onclick="openGearModal(${ch})">Re-Query Registers</button>
    </div>
  `;
}

async function saveGearParams(ch) {
  await sendCmd({
    action: "configure_gear",
    index: ch,
    min_level: Number(document.getElementById("gm-min").value),
    max_level: Number(document.getElementById("gm-max").value),
    power_on_level: Number(document.getElementById("gm-pon").value),
    system_failure_level: Number(document.getElementById("gm-fail").value),
    fade_time: Number(document.getElementById("gm-ft").value),
    fade_rate: Number(document.getElementById("gm-fr").value),
  });
  closeGearModal();
}

function closeGearModal() {
  document.getElementById("gear-modal").style.display = "none";
}

async function loadConfig() {
  const res = await fetch("/api/config");
  const c = await res.json();
  document.getElementById("cfg-wifi-enabled").checked = !!c.wifi_enabled;
  document.getElementById("cfg-ap-fallback").checked = !!c.ap_fallback_enabled;
  document.getElementById("cfg-wifi-ps").checked = !!c.wifi_power_save;
  document.getElementById("cfg-wifi-ssid").value = c.wifi_ssid || "";
  document.getElementById("cfg-wifi-pass").value = c.wifi_password || "";
  document.getElementById("cfg-hostname").value = c.hostname || "knx-dali-gw";
  document.getElementById("cfg-mqtt-enabled").checked = !!c.mqtt_enabled;
  document.getElementById("cfg-ha-enabled").checked = !!c.ha_discovery_enabled;
  document.getElementById("cfg-mqtt-host").value = c.mqtt_host || "";
  document.getElementById("cfg-mqtt-port").value = c.mqtt_port || 1883;
  document.getElementById("cfg-mqtt-user").value = c.mqtt_username || "";
  document.getElementById("cfg-mqtt-pass").value = c.mqtt_password || "";
  document.getElementById("cfg-mqtt-base").value = c.mqtt_base_topic || "knx_dali";
  document.getElementById("cfg-ha-prefix").value = c.ha_discovery_prefix || "homeassistant";
  document.getElementById("cfg-oled-geom").value = String(c.oled_geometry ?? 0);
  document.getElementById("cfg-oled-sleep").value = c.oled_sleep_seconds ?? 60;
  document.getElementById("cfg-dali-psu").checked = !!c.internal_dali_psu_enabled;
}

async function saveConfig(ev) {
  ev.preventDefault();
  const payload = {
    wifi_enabled: document.getElementById("cfg-wifi-enabled").checked,
    ap_fallback_enabled: document.getElementById("cfg-ap-fallback").checked,
    wifi_power_save: document.getElementById("cfg-wifi-ps").checked,
    wifi_ssid: document.getElementById("cfg-wifi-ssid").value,
    wifi_password: document.getElementById("cfg-wifi-pass").value,
    hostname: document.getElementById("cfg-hostname").value,
    mqtt_enabled: document.getElementById("cfg-mqtt-enabled").checked,
    ha_discovery_enabled: document.getElementById("cfg-ha-enabled").checked,
    mqtt_host: document.getElementById("cfg-mqtt-host").value,
    mqtt_port: Number(document.getElementById("cfg-mqtt-port").value),
    mqtt_username: document.getElementById("cfg-mqtt-user").value,
    mqtt_password: document.getElementById("cfg-mqtt-pass").value,
    mqtt_base_topic: document.getElementById("cfg-mqtt-base").value,
    ha_discovery_prefix: document.getElementById("cfg-ha-prefix").value,
    oled_geometry: Number(document.getElementById("cfg-oled-geom").value),
    oled_sleep_seconds: Number(document.getElementById("cfg-oled-sleep").value),
    internal_dali_psu_enabled: document.getElementById("cfg-dali-psu").checked,
  };
  await fetch("/api/config", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  });
  const msg = document.getElementById("cfg-save-msg");
  msg.textContent = "Saved to NVS & applied.";
  setTimeout(() => (msg.textContent = ""), 3000);
}

async function loadDebugTrace() {
  const res = await fetch("/api/debug");
  const d = await res.json();
  window.lastTraceData = d;
  document.getElementById("dbg-autotrigger").checked = !!d.auto_trigger;
  document.getElementById("trace-stats").textContent =
    `Session: ${d.active ? "ACTIVE (Recording)" : "PAUSED"} • Total Frames: ${d.total_frames} • Collisions: ${d.total_collisions} • Bus Errors: ${d.total_errors}`;
  const tbody = document.getElementById("trace-tbody");
  tbody.innerHTML = "";
  (d.entries || []).forEach((e) => {
    const tr = document.createElement("tr");
    const hexW = e.bits === 24 ? 6 : 4;
    const rawHex = "0x" + Number(e.frame).toString(16).toUpperCase().padStart(hexW, "0");
    const stClass = e.status === "OK" ? "ok" : e.status === "TIMEOUT" ? "warn" : "err";
    tr.innerHTML = `
      <td class="mono">${e.seq}</td>
      <td class="mono">${formatTimestamp(e.epoch_ms, e.uptime_ms)}</td>
      <td class="mono">${(e.uptime_ms / 1000).toFixed(3)}s</td>
      <td class="mono">${e.bits}-bit</td>
      <td class="mono">${rawHex}</td>
      <td class="mono"><strong>${e.target}</strong></td>
      <td class="mono">${e.mnemonic}</td>
      <td class="mono">${e.detail}</td>
      <td><span class="badge ${stClass}">${e.status}</span></td>
    `;
    tbody.appendChild(tr);
  });
}

async function controlDebug(action) {
  const autoTrig = document.getElementById("dbg-autotrigger").checked;
  await fetch("/api/debug", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ action, auto_trigger: autoTrig }),
  });
  await loadDebugTrace();
}

async function loadErrorLogs() {
  const res = await fetch("/api/logs");
  const d = await res.json();
  window.lastLogData = d;
  const tbody = document.getElementById("logs-tbody");
  tbody.innerHTML = "";
  (d.entries || []).forEach((e) => {
    const tr = document.createElement("tr");
    const sevClass = e.severity === "INFO" ? "ok" : e.severity === "WARN" ? "warn" : "err";
    const addrStr = e.address === 65535 ? "SYS/BUS" : `A${String(e.address & 63).padStart(2, "0")}`;
    tr.innerHTML = `
      <td class="mono">${e.seq}</td>
      <td class="mono">${formatTimestamp(e.epoch_ms, e.uptime_ms)}</td>
      <td class="mono">${e.source}</td>
      <td><span class="badge ${sevClass}">${e.severity}</span></td>
      <td><strong>E${e.code}: ${e.title}</strong><br><span class="mono muted">${e.symbol}</span></td>
      <td class="mono">${addrStr}</td>
      <td class="mono">×${e.count}</td>
      <td><div>${e.detail}</div><div class="muted">${e.description} — <em>Remedy: ${e.remedy}</em></div></td>
    `;
    tbody.appendChild(tr);
  });
}

async function clearErrorLogs() {
  await fetch("/api/logs/clear", { method: "POST" });
  await loadErrorLogs();
}

async function loadErrorCatalog() {
  const tbody = document.getElementById("catalog-tbody");
  if (tbody.children.length > 0) return;
  const res = await fetch("/api/errors");
  const items = await res.json();
  items.forEach((c) => {
    if (c.code === 0) return;
    const tr = document.createElement("tr");
    tr.innerHTML = `
      <td class="mono"><strong>E${c.code}</strong></td>
      <td class="mono">${c.symbol}</td>
      <td><strong>${c.title}</strong></td>
      <td>${c.description}</td>
      <td>${c.remedy}</td>
    `;
    tbody.appendChild(tr);
  });
}

function exportJson(filename, obj) {
  const blob = new Blob([JSON.stringify(obj || {}, null, 2)], { type: "application/json" });
  const a = document.createElement("a");
  a.href = URL.createObjectURL(blob);
  a.download = filename;
  a.click();
}

function uploadOta(target) {
  const input = document.getElementById("file-" + target);
  const status = document.getElementById("status-" + target);
  const bar = document.getElementById("prog-" + target);
  if (!input.files || !input.files[0]) {
    status.textContent = "Please choose a .bin file first.";
    return;
  }
  const file = input.files[0];
  bar.style.width = "0%";
  status.textContent = `Uploading ${file.name} (${Math.round(file.size / 1024)} KB)...`;

  const xhr = new XMLHttpRequest();
  xhr.open("POST", `/api/ota/${target}`, true);
  xhr.setRequestHeader("Content-Type", "application/octet-stream");
  xhr.upload.onprogress = (e) => {
    if (e.lengthComputable) {
      const pct = Math.round((e.loaded * 100) / e.total);
      bar.style.width = pct + "%";
      status.textContent = `Uploading ${file.name}: ${pct}% (${Math.round(e.loaded / 1024)} / ${Math.round(e.total / 1024)} KB)`;
    }
  };
  xhr.onload = () => {
    if (xhr.status === 200) {
      bar.style.width = "100%";
      status.textContent =
        target === "firmware"
          ? "Firmware verified & flashed! Gateway is rebooting..."
          : "LittleFS partition flashed & mounted! Reloading UI...";
      if (target === "littlefs") setTimeout(() => location.reload(), 1500);
    } else {
      status.textContent = `Upload failed (HTTP ${xhr.status}): ${xhr.responseText}`;
    }
  };
  xhr.onerror = () => {
    status.textContent = "Network error during binary upload.";
  };
  xhr.send(file);
}

async function rebootDevice() {
  await sendCmd({ action: "reboot" });
}

window.addEventListener("DOMContentLoaded", () => {
  initTabs();
  connectWebSocket();
});
