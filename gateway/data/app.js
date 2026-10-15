"use strict";

let ws = null;
let mockMode = false;
let lampFilter = "all";
const DT8_COLOR_OPTIONS = [
  { mask: 1, label: "Tunable White (CCT)" },
  { mask: 2, label: "RGB" },
  { mask: 6, label: "RGBW" },
  { mask: 7, label: "CCT + RGBW" },
  { mask: 8, label: "CIE xy" },
  { mask: 9, label: "CCT + CIE xy" },
];
let selectedCh = 0;
let selectedSensor = 0;
let selectedGrp = 0;
let selectedScene = 0;
let inspectorTab = "control";
let renderedInspectorCh = -1;
let renderedInspectorSig = "";
let renderedGrpSig = "";
let selectedColorModeByCh = {};
let selectedColorModeByGrp = {};
let latestState = null;
let channelDetailsCache = {};
window.lastLogData = { entries: [] };
window.lastTraceData = { entries: [] };

// IEC 62386-201..209 DALI Device Types
const DEVICE_TYPE_NAMES = {
  0: "Fluorescent Lamp (DT0)",
  1: "Emergency Light (DT1)",
  2: "Discharge / Arc Lamp (DT2)",
  3: "Low-Voltage Halogen (DT3)",
  4: "Incandescent Dimmer (DT4)",
  5: "0–10 V Converter (DT5)",
  6: "LED Driver (DT6)",
  7: "Relay Switch On/Off (DT7)",
  8: "Color / Tunable LED (DT8)",
};

const DEVICE_TYPE_SHORT = {
  0: "Fluor",
  1: "Emerg",
  2: "Arc",
  3: "Halogen",
  4: "Incand",
  5: "0-10V",
  6: "LED",
  7: "Switch",
  8: "LED",
};

const FADE_TIME_LABELS = [
  "0 — No fade (<0.7 s)",
  "1 — 0.7 s",
  "2 — 1.0 s",
  "3 — 1.4 s",
  "4 — 2.0 s",
  "5 — 2.8 s",
  "6 — 4.0 s",
  "7 — 5.7 s",
  "8 — 8.0 s",
  "9 — 11.3 s",
  "10 — 16.0 s",
  "11 — 22.6 s",
  "12 — 32.0 s",
  "13 — 45.3 s",
  "14 — 64.0 s",
  "15 — 90.5 s",
];

const FADE_RATE_LABELS = [
  "",
  "1 — 358 steps/s (0.7 s)",
  "2 — 253 steps/s (1.0 s)",
  "3 — 179 steps/s (1.4 s)",
  "4 — 127 steps/s (2.0 s)",
  "5 — 89.4 steps/s (2.8 s)",
  "6 — 63.3 steps/s (4.0 s)",
  "7 — 44.7 steps/s (5.7 s)",
  "8 — 31.6 steps/s (8.0 s)",
  "9 — 22.4 steps/s (11.3 s)",
  "10 — 15.8 steps/s (16.0 s)",
  "11 — 11.2 steps/s (22.6 s)",
  "12 — 7.9 steps/s (32.0 s)",
  "13 — 5.6 steps/s (45.3 s)",
  "14 — 4.0 steps/s (64.0 s)",
  "15 — 2.8 steps/s (90.5 s)",
];

const COLOR_PRESETS = [
  { name: "Warm White", r: 255, g: 190, b: 110, hex: "#ffbe6e" },
  { name: "Daylight", r: 240, g: 248, b: 255, hex: "#f0f8ff" },
  { name: "Amber", r: 255, g: 140, b: 20, hex: "#ff8c14" },
  { name: "Coral Red", r: 244, g: 63, b: 94, hex: "#f43f5e" },
  { name: "Emerald", r: 16, g: 185, b: 129, hex: "#10b981" },
  { name: "Sky Blue", r: 56, g: 189, b: 248, hex: "#38bdf8" },
  { name: "Indigo", r: 99, g: 102, b: 241, hex: "#6366f1" },
  { name: "Violet", r: 168, g: 85, b: 247, hex: "#a855f7" },
];

function arcToPct(arc, minPct = 0) {
  if (!arc || arc <= 0) return minPct;
  if (arc >= 254) return 100;
  return Math.max(minPct, Math.round((arc * 100) / 254));
}

function pctToArc(pct, minArc = 0) {
  const p = Math.max(0, Math.min(100, Number(pct) || 0));
  if (p === 0) return minArc;
  if (p >= 100) return 254;
  return Math.max(minArc, Math.round((p * 254) / 100));
}

function daliArcToPowerRatio(arc) {
  if (!arc || arc <= 0) return 0.0;
  if (arc >= 254) return 1.0;
  return Math.pow(10, (arc - 254) / (253 / 3));
}

function kelvinToCss(k) {
  if (!k || k <= 2700) return "#ffb454";
  if (k <= 3500) return "#ffd180";
  if (k <= 4500) return "#fff1d0";
  return "#cfe8ff";
}

function rgbToHex(r, g, b) {
  const to2 = (v) => Math.max(0, Math.min(255, Number(v) || 0)).toString(16).padStart(2, "0");
  return "#" + to2(r) + to2(g) + to2(b);
}

function formatIa(ia) {
  if (!ia || ia === 0xffff) return "Unprogrammed";
  return ((ia >> 12) & 0xf) + "." + ((ia >> 8) & 0xf) + "." + (ia & 0xff);
}

function formatUptime(sec) {
  const d = Math.floor(sec / 86400);
  const h = Math.floor((sec % 86400) / 3600);
  const m = Math.floor((sec % 3600) / 60);
  if (d > 0) return `${d}d ${h}h`;
  if (h > 0) return `${h}h ${m}m`;
  return `${m}m`;
}

function formatTimestamp(epochMs, uptimeMs) {
  if (epochMs && epochMs > 1700000000000) {
    return new Date(epochMs).toISOString().replace("T", " ").slice(0, 19);
  }
  return `+${(uptimeMs / 1000).toFixed(1)}s`;
}

// Honors 0 W when powerConfigured is true!
function channelPowerBreakdown(ch, defaultFullW, defaultIdleW) {
  const [
    present,
    ,
    level,
    ,
    ,
    ,
    ,
    ,
    ,
    ,
    ,
    devType,
    d4iSupported,
    manualOverride,
    d4iPowerDeciwatts,
    fullPowerDeciwatts,
    idlePowerCentiwatts,
    powerConfigured,
    switchOnly,
  ] = ch;
  if (!present) return { totalW: 0, lampW: 0, idleW: 0, source: "off" };
  if (d4iSupported && !manualOverride) {
    const w = (d4iPowerDeciwatts || 0) / 10.0;
    return { totalW: w, lampW: w, idleW: 0, source: "D4i" };
  }
  const fullW = powerConfigured ? (fullPowerDeciwatts || 0) / 10.0 : defaultFullW;
  const idleW = powerConfigured ? (idlePowerCentiwatts || 0) / 100.0 : defaultIdleW;
  const isSwitch = Boolean(switchOnly || devType === 7);
  const ratio = level > 0 ? (isSwitch ? 1.0 : daliArcToPowerRatio(level)) : 0.0;
  const lampW = fullW * ratio;
  return { totalW: idleW + lampW, lampW, idleW, source: "calc" };
}

function decodeTypeShort(dt8, devType, switchOnly) {
  if (switchOnly || devType === 7) return "Switch";
  const base = DEVICE_TYPE_SHORT[devType] || `DT${devType}`;
  if (dt8 & 4) return `${base}·RGBW`;
  if (dt8 & 2) return `${base}·RGB`;
  if (dt8 & 1) return `${base}·CCT`;
  if (dt8 & 8) return `${base}·XY`;
  return base;
}

function initUi() {
  const urlParams = new URLSearchParams(location.search);
  if (urlParams.has("ch")) {
    const c = Number(urlParams.get("ch"));
    if (c >= 0 && c < 64) selectedCh = c;
  }

  document.querySelectorAll(".nav-tab").forEach((btn) => {
    btn.addEventListener("click", () => {
      document.querySelectorAll(".nav-tab").forEach((b) => b.classList.remove("active"));
      document.querySelectorAll(".tab-panel").forEach((p) => p.classList.remove("active"));
      btn.classList.add("active");
      const target = document.getElementById("panel-" + btn.dataset.tab);
      if (target) target.classList.add("active");
      if (btn.dataset.tab === "settings") loadConfig();
      if (btn.dataset.tab === "logs") {
        loadErrorLogs();
        loadDebugTrace();
      }
    });
  });
}

function setLampFilter(filter, btn) {
  lampFilter = filter;
  document.querySelectorAll(".matrix-header .pill-btn").forEach((b) => b.classList.remove("active"));
  if (btn) btn.classList.add("active");
  const lampsGrid = document.getElementById("lamps-grid");
  const inputsGrid = document.getElementById("inputs-grid");
  const actionBtns = document.getElementById("bus-action-btns");
  renderedInspectorCh = -1;
  if (filter === "sensors") {
    selectedCh = -1;
    lampsGrid.style.display = "none";
    inputsGrid.style.display = "grid";
    actionBtns.innerHTML = `
      <button class="btn btn-sm" onclick="sendCmd({action:'scan_inputs'})">Scan Sensors</button>
      <button class="btn btn-sm" onclick="sendCmd({action:'assign_inputs',mode:'new'})">Address New</button>
    `;
    if (latestState) {
      renderInputs(latestState.inputs || [], latestState.input_events || []);
      renderSensorInspector(selectedSensor);
    }
  } else {
    if (selectedCh < 0) selectedCh = 0;
    lampsGrid.style.display = "grid";
    inputsGrid.style.display = "none";
    actionBtns.innerHTML = `
      <button class="btn btn-sm" onclick="sendCmd({action:'scan_bus'})">Scan</button>
      <button class="btn btn-sm" onclick="sendCmd({action:'assign_addresses',mode:'new'})">Address New</button>
    `;
    if (latestState) {
      renderLampsGrid(latestState.channels || []);
      renderInspector(selectedCh, true);
    }
  }
}

function selectSensor(idx) {
  selectedSensor = idx;
  if (latestState) {
    renderInputs(latestState.inputs || [], latestState.input_events || []);
    renderSensorInspector(idx);
  }
}

function setInspectorTab(tab) {
  inspectorTab = tab;
  renderedInspectorCh = -1;
  renderInspector(selectedCh, true);
}

async function onProfileOrSwitchChange(chIdx) {
  const devType = Number(document.getElementById("gm-devtype")?.value ?? 6);
  const curDt8 = latestState?.channels?.[chIdx]?.[4] || 0;
  const dt8Mask = devType === 8 ? (curDt8 || 15) : 0;
  const swOnly = devType === 7;

  if (latestState && latestState.channels && latestState.channels[chIdx]) {
    latestState.channels[chIdx][4] = dt8Mask;
    latestState.channels[chIdx][11] = devType;
    latestState.channels[chIdx][18] = swOnly ? 1 : 0;
  }
  await sendCmd({
    action: "configure_gear",
    index: chIdx,
    device_type: devType,
    dt8_mask: dt8Mask,
    switch_only: swOnly,
  });
  renderedInspectorCh = -1;
  renderInspector(chIdx, true);
}

function selectLamp(idx) {
  selectedCh = idx;
  renderedInspectorCh = -1;
  if (latestState) {
    renderLampsGrid(latestState.channels || []);
    renderInspector(idx, true);
  }
}

function setInspectorColorMode(chIdx, mode) {
  selectedColorModeByCh[chIdx] = mode;
  renderInspector(chIdx, true);
}

function previewCctSlider(chIdx, kelvin) {
  const k = Number(kelvin) || 4000;
  const col = kelvinToCss(k);
  selectedColorModeByCh[chIdx] = "cct";
  const slider = document.getElementById("insp-cct-slider");
  const label = document.getElementById("insp-cct-val");
  if (slider) {
    slider.style.setProperty("--cct-thumb", col);
    slider.style.accentColor = col;
  }
  if (label) label.textContent = `${k}K`;
  if (latestState && latestState.channels && latestState.channels[chIdx]) {
    latestState.channels[chIdx][5] = k;
    latestState.channels[chIdx][6] = 0;
    latestState.channels[chIdx][7] = 0;
    latestState.channels[chIdx][8] = 0;
    renderLampsGrid(latestState.channels);
  }
}

function lampErrorCode(faulty, statusByte) {
  if (statusByte & 0x02) return "E106";
  if (statusByte & 0x01) return "E107";
  if (statusByte & 0x08) return "E108";
  if (faulty) return "E104";
  return "";
}

function renderLampsGrid(channels) {
  const grid = document.getElementById("lamps-grid");
  grid.innerHTML = "";
  const nomWatts = Number(document.getElementById("nominal-watts")?.value ?? 15.0);
  const idleWatts = Number(document.getElementById("idle-watts")?.value ?? 0.4);

  let cntPresent = 0;
  let cntOn = 0;
  let cntFault = 0;
  channels.forEach((ch) => {
    const [present, faulty, level, , , , , , , statusByte] = ch;
    if (present) cntPresent++;
    if (level > 0) cntOn++;
    if (faulty || (statusByte & 0x03) !== 0) cntFault++;
  });
  document.getElementById("cnt-all").textContent = String(channels.length);
  document.getElementById("cnt-present").textContent = String(cntPresent);
  document.getElementById("cnt-on").textContent = String(cntOn);
  document.getElementById("cnt-fault").textContent = String(cntFault);

  channels.forEach((ch, idx) => {
    const [
      present,
      faulty,
      level,
      ,
      dt8,
      kelvin,
      r,
      g,
      b,
      statusByte,
      ,
      devType,
      ,
      ,
      ,
      ,
      ,
      ,
      switchOnly,
    ] = ch;
    const hasErr = Boolean(faulty || (statusByte & 0x03) !== 0);
    if (lampFilter === "present" && !present) return;
    if (lampFilter === "on" && level === 0) return;
    if (lampFilter === "fault" && !hasErr) return;

    const isSwitch = Boolean(switchOnly || devType === 7);
    const pct = Math.round((level * 100) / 254);
    const pwr = channelPowerBreakdown(ch, nomWatts, idleWatts);
    const addrStr = "A" + String(idx).padStart(2, "0");

    let barColor = "#3b82f6";
    if (hasErr) barColor = "#ef4444";
    else if ((dt8 & 6) && (r || g || b)) barColor = rgbToHex(r, g, b);
    else if (dt8 & 1) barColor = kelvinToCss(kelvin || 4000);

    const isFinding = Boolean(
      latestState &&
        latestState.dali &&
        latestState.dali.finder_active &&
        latestState.dali.finder_ch === idx
    );
    const tile = document.createElement("div");
    tile.className =
      "lamp-tile" +
      (idx === selectedCh ? " selected" : "") +
      (isFinding ? " finding" : "") +
      (level > 0 ? " on" : "") +
      (hasErr ? " fault" : "") +
      (!present ? " absent" : "");
    tile.onclick = () => selectLamp(idx);

    const errCode = lampErrorCode(faulty, statusByte);
    const statusTxt = isFinding
      ? '<span style="color:var(--warn);font-weight:600">BLINK</span>'
      : hasErr
        ? `<span class="tile-err-tag">ERR: ${errCode}</span>`
        : !present
          ? "Free"
          : isSwitch
            ? level > 0
              ? "ON"
              : "Off"
            : level > 0
              ? `${pct}%`
              : "Off";

    tile.innerHTML = `
      <div class="tile-head">
        <span class="tile-addr">${addrStr}</span>
        <button class="tile-pwr-btn" title="${level > 0 ? "Turn Off" : "Turn On"}"
          onclick="event.stopPropagation();sendCmd({action:'${level > 0 ? "switch_off" : "switch_on"}',target:'ch',index:${idx}})">⏻</button>
      </div>
      <div class="tile-bar-track">
        <div class="tile-bar-fill" style="width:${present ? (isSwitch ? (level > 0 ? 100 : 0) : Math.max(level > 0 ? 8 : 0, pct)) : 0}%;background:${barColor}"></div>
      </div>
      <div class="tile-foot">
        <span class="tile-status">${statusTxt}</span>
        <span class="tile-type">${present ? `${pwr.totalW.toFixed(1)}W · ${decodeTypeShort(dt8, devType, isSwitch)}` : "—"}</span>
      </div>
    `;
    grid.appendChild(tile);
  });
}

function renderInspector(chIdx, forceRebuild = false) {
  const pane = document.getElementById("inspector-content");
  if (!latestState || !latestState.channels || !latestState.channels[chIdx]) return;
  const ch = latestState.channels[chIdx];
  const [
    present,
    faulty,
    level,
    ,
    dt8,
    kelvin,
    r,
    g,
    b,
    statusByte,
    groupsMask,
    devType,
    d4iSupported,
    manualOverride,
    d4iPowerDeciwatts,
    fullPowerDeciwatts,
    idlePowerCentiwatts,
    powerConfigured,
    switchOnly,
  ] = ch;

  const addrStr = "A" + String(chIdx).padStart(2, "0");
  const isSwitch = Boolean(switchOnly || devType === 7);
  const pct = Math.round((level * 100) / 254);
  const nomWatts = Number(document.getElementById("nominal-watts")?.value ?? 15.0);
  const idleWatts = Number(document.getElementById("idle-watts")?.value ?? 0.4);
  const pwr = channelPowerBreakdown(ch, nomWatts, idleWatts);
  const hasErr = Boolean(faulty || (statusByte & 0x03) !== 0);

  // Structural signature: only rebuild the Inspector DOM when the selected channel, tab, or capabilities change
  const sig = `${chIdx}:${inspectorTab}:${present}:${dt8}:${devType}:${d4iSupported}:${isSwitch}`;
  if (!forceRebuild && renderedInspectorCh === chIdx && renderedInspectorSig === sig) {
    // Update live readouts in-place without clobbering form inputs or sliders being dragged
    const activeEl = document.activeElement;
    const briText = document.getElementById("insp-bri-text");
    if (briText) {
      briText.textContent = `${isSwitch ? (level > 0 ? "ON" : "OFF") : `${pct}% (${level}/254)`} • ${pwr.totalW.toFixed(1)} W`;
    }
    const toggleBtn = document.getElementById("insp-toggle-btn");
    if (toggleBtn) {
      toggleBtn.className = "btn btn-sm " + (level > 0 ? "btn-primary" : "");
      toggleBtn.textContent = level > 0 ? "Turn Off" : "Turn On";
      toggleBtn.onclick = () =>
        sendCmd({ action: level > 0 ? "switch_off" : "switch_on", target: "ch", index: chIdx });
    }
    const briSlider = document.getElementById("insp-bri-slider");
    if (briSlider && activeEl !== briSlider) {
      briSlider.value = String(level);
    }
    const cctSlider = document.getElementById("insp-cct-slider");
    if (cctSlider && activeEl !== cctSlider) {
      const kVal = kelvin || 4000;
      const col = kelvinToCss(kVal);
      cctSlider.value = String(kVal);
      cctSlider.style.setProperty("--cct-thumb", col);
      cctSlider.style.accentColor = col;
      const cctVal = document.getElementById("insp-cct-val");
      if (cctVal) cctVal.textContent = `${kVal}K`;
    }
    const pwrBadge = document.getElementById("insp-pwr-badge");
    if (pwrBadge) {
      pwrBadge.textContent =
        d4iSupported && !manualOverride
          ? `D4i Ballast: ${((d4iPowerDeciwatts || 0) / 10).toFixed(1)} W`
          : `${pwr.totalW.toFixed(1)} W (Lamp ${pwr.lampW.toFixed(1)}W + Idle ${pwr.idleW.toFixed(2)}W)`;
    }
    return;
  }
  renderedInspectorCh = chIdx;
  renderedInspectorSig = sig;

  const d = channelDetailsCache[chIdx] || {
    device_type: devType ?? 6,
    version: 2,
    physical_min: 1,
    min_level: 1,
    max_level: 254,
    power_on_level: 254,
    system_failure_level: 254,
    fade_time: 4,
    fade_rate: 7,
    groups: groupsMask || 0,
    scenes: Array(16).fill(255),
    xy_x: 20480,
    xy_y: 21500,
    d4i_power_supported: Boolean(d4iSupported),
    use_manual_power_override: Boolean(manualOverride),
    d4i_active_power_deciwatts: d4iPowerDeciwatts || 0,
    full_power_deciwatts: powerConfigured ? fullPowerDeciwatts : Math.round(nomWatts * 10),
    idle_power_centiwatts: powerConfigured ? idlePowerCentiwatts : Math.round(idleWatts * 100),
    power_configured: Boolean(powerConfigured),
    switch_only: isSwitch,
  };

  const badges = [];
  badges.push(
    `<span class="badge ${present ? "ok" : ""}">${present ? "Installed" : "Empty Address"}</span>`
  );
  badges.push(`<span class="badge">${DEVICE_TYPE_NAMES[devType] || `Device Type ${devType}`}</span>`);
  if (!isSwitch) {
    if (dt8 & 1) badges.push('<span class="badge">CCT</span>');
    if (dt8 & 4) badges.push('<span class="badge">RGBW</span>');
    else if (dt8 & 2) badges.push('<span class="badge">RGB</span>');
    if (dt8 & 8) badges.push('<span class="badge">CIE xy</span>');
  }
  if (hasErr) {
    const errCode = lampErrorCode(faulty, statusByte);
    const reason = (statusByte & 0x02) ? "Lamp Failure" : (statusByte & 0x01) ? "Ballast Fault" : "No Reply";
    badges.push(`<span class="badge err">${errCode}: ${reason}</span>`);
  }

  let colorHtml = "";
  const effectiveDt8 = !isSwitch && (Number(devType) === 8 ? (dt8 || 15) : dt8);
  if (effectiveDt8) {
    const availModes = [];
    if (effectiveDt8 & 1) availModes.push({ id: "cct", label: "White (CCT)" });
    if (effectiveDt8 & 6) availModes.push({ id: "rgb", label: effectiveDt8 & 4 ? "Color (RGBW)" : "Color (RGB)" });
    if (effectiveDt8 & 8) availModes.push({ id: "xy", label: "CIE xy" });

    let activeMode = selectedColorModeByCh[chIdx];
    if (!activeMode || !availModes.some((m) => m.id === activeMode)) {
      if ((effectiveDt8 & 6) && (r || g || b)) activeMode = "rgb";
      else if (effectiveDt8 & 1) activeMode = "cct";
      else activeMode = availModes[0].id;
      selectedColorModeByCh[chIdx] = activeMode;
    }

    const modeTabsHtml =
      availModes.length > 1
        ? `<div class="segmented-control" style="margin-bottom:6px">
             ${availModes
               .map(
                 (m) =>
                   `<button type="button" class="pill-btn ${activeMode === m.id ? "active" : ""}"
                      onclick="setInspectorColorMode(${chIdx},'${m.id}')">${m.label}</button>`
               )
               .join("")}
           </div>`
        : "";

    let bodyHtml = "";
    if (activeMode === "cct") {
      const kVal = kelvin || 4000;
      const kCol = kelvinToCss(kVal);
      bodyHtml = `
        <div class="insp-row">
          <input type="range" id="insp-cct-slider" class="cct-range" min="2000" max="6500" step="100" value="${kVal}"
            style="--cct-thumb:${kCol};accent-color:${kCol}"
            oninput="previewCctSlider(${chIdx},this.value)"
            onchange="sendCmd({action:'set_cct',target:'ch',index:${chIdx},kelvin:Number(this.value)})">
          <span class="mono" id="insp-cct-val" style="min-width:52px;text-align:right">${kVal}K</span>
        </div>
        <div class="btn-row">
          ${[2700, 3000, 4000, 5000, 6500]
            .map(
              (k) =>
                `<button class="btn btn-xs" onclick="previewCctSlider(${chIdx},${k});sendCmd({action:'set_cct',target:'ch',index:${chIdx},kelvin:${k}})">${k}K</button>`
            )
            .join("")}
        </div>`;
    } else if (activeMode === "rgb") {
      const hexColor = (r || g || b) ? rgbToHex(r, g, b) : "#38bdf8";
      const wVal = d.w || 0;
      const wPct = Math.round((wVal * 100) / 254);
      bodyHtml = `
        <div class="swatch-row">
          ${COLOR_PRESETS.map(
            (p) =>
              `<button class="color-preset" style="background:${p.hex}" title="${p.name}"
                 onclick="sendCmd({action:'set_rgb',target:'ch',index:${chIdx},r:${p.r},g:${p.g},b:${p.b}})"></button>`
          ).join("")}
          <input type="color" class="color-input" value="${hexColor}" onchange="sendHexRgb('ch',${chIdx},this.value)">
          <span class="mono" style="font-size:11px;color:var(--text-2)">RGB: ${r || 0}, ${g || 0}, ${b || 0}</span>
        </div>
        ${
          effectiveDt8 & 4
            ? `<div class="insp-row" style="margin-top:4px">
                 <span style="font-size:11.5px;color:var(--text-2)">Dedicated White LED (W)</span>
                 <input type="range" min="0" max="254" value="${wVal}"
                   oninput="document.getElementById('insp-w-val').textContent=Math.round(this.value*100/254)+'%'"
                   onchange="sendCmd({action:'set_rgbw',target:'ch',index:${chIdx},r:${r || 0},g:${g || 0},b:${b || 0},w:Number(this.value)})">
                 <span class="mono" id="insp-w-val" style="min-width:36px;text-align:right">${wPct}%</span>
               </div>`
            : ""
        }`;
    } else if (activeMode === "xy") {
      const xyX = ((d.xy_x || 20480) / 65535).toFixed(3);
      const xyY = ((d.xy_y || 21500) / 65535).toFixed(3);
      bodyHtml = `
        <div class="form-grid-3">
          <label class="field"><span>x (0..0.8)</span><input type="number" id="insp-xy-x" min="0" max="0.8" step="0.005" value="${xyX}"></label>
          <label class="field"><span>y (0..0.8)</span><input type="number" id="insp-xy-y" min="0" max="0.8" step="0.005" value="${xyY}"></label>
          <div class="field" style="justify-content:flex-end">
            <button class="btn btn-sm" onclick="sendCmd({action:'set_xy',target:'ch',index:${chIdx},x:Math.round(Number(document.getElementById('insp-xy-x').value)*65535),y:Math.round(Number(document.getElementById('insp-xy-y').value)*65535),brightness:${level || 200}})">Apply</button>
          </div>
        </div>`;
    }

    colorHtml = `
      <div class="insp-section">
        <div class="insp-row">
          <span class="insp-sec-title">Color Mode</span>
        </div>
        ${modeTabsHtml}
        ${bodyHtml}
      </div>`;
  }

  const dtTypeOptions = Object.entries(DEVICE_TYPE_NAMES)
    .map(
      ([k, label]) =>
        `<option value="${k}" ${Number(devType) === Number(k) ? "selected" : ""}>${label}</option>`
    )
    .join("");
  const minPct = arcToPct(d.min_level ?? 1, 1);
  const maxPct = arcToPct(d.max_level ?? 254, 1);
  const ponPct = arcToPct(d.power_on_level ?? 254, 0);
  const failPct = arcToPct(d.system_failure_level ?? 254, 0);
  const dimCurve = Number(d.dimming_curve ?? 0);

  const ftOptions = FADE_TIME_LABELS.map(
    (lbl, i) => `<option value="${i}" ${d.fade_time === i ? "selected" : ""}>${lbl}</option>`
  ).join("");
  const frOptions = FADE_RATE_LABELS.slice(1)
    .map(
      (lbl, i) =>
        `<option value="${i + 1}" ${d.fade_rate === i + 1 ? "selected" : ""}>${lbl}</option>`
    )
    .join("");

  const fullWVal = (
    (powerConfigured ? fullPowerDeciwatts : Math.round(nomWatts * 10)) / 10
  ).toFixed(1);
  const idleWVal = (
    (powerConfigured ? idlePowerCentiwatts : Math.round(idleWatts * 100)) / 100
  ).toFixed(2);

  pane.innerHTML = `
    <div class="insp-header">
      <div>
        <div class="insp-title">Lamp ${addrStr}</div>
        <div class="insp-badges">${badges.join("")}</div>
      </div>
      <div class="btn-row">
        <button class="btn btn-xs" title="Blink lamp on/off to locate it in the room" onclick="sendCmd({action:'find_lamp',target:'ch',index:${chIdx},mode:'blink'})">Blink</button>
        <button class="btn btn-xs" title="Smoothly pulse lamp" onclick="sendCmd({action:'find_lamp',target:'ch',index:${chIdx},mode:'pulse'})">Pulse</button>
        <button class="btn btn-xs" title="Stop blinking and restore previous level" onclick="sendCmd({action:'find_lamp',target:'ch',index:${chIdx},mode:'stop'})">Stop</button>
      </div>
    </div>

    <div class="segmented-control" style="margin-top:10px">
      <button type="button" class="pill-btn ${inspectorTab === "control" ? "active" : ""}" onclick="setInspectorTab('control')">Control</button>
      <button type="button" class="pill-btn ${inspectorTab === "config" ? "active" : ""}" onclick="setInspectorTab('config')">Settings</button>
    </div>

    ${
      inspectorTab === "control"
        ? `
          <div class="insp-section">
            <div class="insp-row">
              <span class="insp-sec-title">${isSwitch ? "Switch Control" : "Brightness"}</span>
              <span class="mono" id="insp-bri-text">${isSwitch ? (level > 0 ? "ON" : "OFF") : `${pct}% (${level}/254)`} • ${pwr.totalW.toFixed(1)} W</span>
            </div>
            <div class="insp-row">
              <button id="insp-toggle-btn" class="btn btn-sm ${level > 0 ? "btn-primary" : ""}"
                onclick="sendCmd({action:'${level > 0 ? "switch_off" : "switch_on"}',target:'ch',index:${chIdx}})">${level > 0 ? "Turn Off" : "Turn On"}</button>
              <input type="range" id="insp-bri-slider" min="0" max="254" value="${level}" ${isSwitch ? "disabled title='Dimming disabled for Switch-Only device'" : ""}
                onchange="sendCmd({action:'set_level',target:'ch',index:${chIdx},level:Number(this.value)})">
            </div>
          </div>
          ${colorHtml}
        `
        : `
          <div class="insp-section">
            <span class="insp-sec-title">Device Type</span>
            <label class="field">
              <span>DALI Device Type</span>
              <select id="gm-devtype" onchange="onProfileOrSwitchChange(${chIdx})">${dtTypeOptions}</select>
            </label>
          </div>

          <div class="insp-section">
            <div class="insp-row">
              <span class="insp-sec-title">Power</span>
              <span id="insp-pwr-badge" class="badge ${d4iSupported && !manualOverride ? "ok" : ""}">${
                d4iSupported && !manualOverride
                  ? `D4i: ${((d4iPowerDeciwatts || 0) / 10).toFixed(1)} W`
                  : `${pwr.totalW.toFixed(1)} W`
              }</span>
            </div>
            <div class="form-grid-2">
              <label class="field">
                <span>Lamp at 100% (W)</span>
                <input type="number" id="gm-full-w" min="0" max="1000" step="0.1" value="${fullWVal}">
              </label>
              <label class="field">
                <span>Ballast Idle (W)</span>
                <input type="number" id="gm-idle-w" min="0" max="50" step="0.05" value="${idleWVal}">
              </label>
            </div>
            ${
              d4iSupported
                ? `<label class="check-row" style="font-size:11.5px;color:var(--text-2)">
                     <input type="checkbox" id="gm-pwr-override" ${manualOverride ? "checked" : ""}>
                     <span>Use calculated power instead of D4i</span>
                   </label>`
                : ""
            }
          </div>

          ${
            !isSwitch
              ? `<div class="insp-section">
                   <span class="insp-sec-title">Dimming Limits, Curve &amp; Fade</span>
                   <label class="field">
                     <span>Dimming Curve</span>
                     <select id="gm-curve">
                       <option value="0" ${dimCurve === 0 ? "selected" : ""}>Logarithmic (Standard eye curve)</option>
                       <option value="1" ${dimCurve === 1 ? "selected" : ""}>Linear</option>
                     </select>
                   </label>
                   <div class="form-grid-2">
                     <label class="field"><span>Min Level (%)</span><input type="number" id="gm-min" min="1" max="100" value="${minPct}"></label>
                     <label class="field"><span>Max Level (%)</span><input type="number" id="gm-max" min="1" max="100" value="${maxPct}"></label>
                   </div>
                   <div class="form-grid-2">
                     <label class="field"><span>Power-On Level (%)</span><input type="number" id="gm-pon" min="0" max="100" value="${ponPct}"></label>
                     <label class="field"><span>Bus-Fail Level (%)</span><input type="number" id="gm-fail" min="0" max="100" value="${failPct}"></label>
                   </div>
                   <div class="form-grid-2">
                     <label class="field"><span>Fade Time (Scenes &amp; %)</span><select id="gm-ft">${ftOptions}</select></label>
                     <label class="field"><span>Button Dim Speed</span><select id="gm-fr">${frOptions}</select></label>
                   </div>
                 </div>`
              : ""
          }

          <div class="insp-section">
            <div class="btn-row">
              <button class="btn btn-primary btn-sm" onclick="saveGearParams(${chIdx})">Save Settings</button>
              <button class="btn btn-sm" onclick="queryChannelDetails(${chIdx})">Read Ballast</button>
              ${
                !present || hasErr
                  ? `<button class="btn btn-sm" onclick="sendCmd({action:'replace_device',index:${chIdx}})">Assign New to ${addrStr}</button>`
                  : ""
              }
            </div>
          </div>
        `
    }
  `;
}

async function queryChannelDetails(chIdx) {
  try {
    await sendCmd({ action: "query_gear", index: chIdx });
    const res = await fetch(`/api/channel?ch=${chIdx}`);
    if (res.ok) {
      channelDetailsCache[chIdx] = await res.json();
      renderInspector(chIdx, true);
    }
  } catch (_) {}
}

async function saveGearParams(ch) {
  const fullW = Math.round(Number(document.getElementById("gm-full-w").value) * 10);
  const idleW = Math.round(Number(document.getElementById("gm-idle-w").value) * 100);
  const overEl = document.getElementById("gm-pwr-override");
  const devType = Number(document.getElementById("gm-devtype").value ?? 6);
  const swOnly = devType === 7;
  const curDt8 = latestState?.channels?.[ch]?.[4] || 0;
  const dt8Mask = devType === 8 ? (curDt8 || 15) : 0;

  const payload = {
    action: "configure_gear",
    index: ch,
    device_type: devType,
    dt8_mask: dt8Mask,
    switch_only: swOnly,
    full_power_deciwatts: fullW,
    idle_power_centiwatts: idleW,
    use_manual_power_override: overEl ? overEl.checked : false,
  };
  const minEl = document.getElementById("gm-min");
  if (minEl) {
    payload.min_level = pctToArc(minEl.value, 1);
    payload.max_level = pctToArc(document.getElementById("gm-max").value, 1);
    payload.power_on_level = pctToArc(document.getElementById("gm-pon").value, 0);
    payload.system_failure_level = pctToArc(document.getElementById("gm-fail").value, 0);
    payload.fade_time = Number(document.getElementById("gm-ft").value);
    payload.fade_rate = Number(document.getElementById("gm-fr").value);
    payload.dimming_curve = Number(document.getElementById("gm-curve").value);
  }
  await sendCmd(payload);
  renderedInspectorCh = -1;
  renderInspector(ch, true);
}

function sendHexRgb(target, index, hex) {
  const clean = (hex || "#ffffff").replace("#", "");
  const r = parseInt(clean.slice(0, 2), 16) || 0;
  const g = parseInt(clean.slice(2, 4), 16) || 0;
  const b = parseInt(clean.slice(4, 6), 16) || 0;
  sendCmd({ action: "set_rgb", target, index, r, g, b });
}

function selectGroup(grpIdx) {
  selectedGrp = grpIdx;
  renderedGrpSig = "";
  if (latestState) renderGroups(latestState.groups || [], latestState.channels || [], true);
}

function setGroupColorMode(grpIdx, mode) {
  selectedColorModeByGrp[grpIdx] = mode;
  renderedGrpSig = "";
  if (latestState) renderGroupDetail(grpIdx, latestState.groups[grpIdx] || 0, latestState.channels || [], true);
}

function previewGroupCctSlider(grpIdx, kelvin) {
  const k = Number(kelvin) || 4000;
  const col = kelvinToCss(k);
  const slider = document.getElementById("grp-cct-slider");
  const label = document.getElementById("grp-cct-val");
  if (slider) {
    slider.style.setProperty("--cct-thumb", col);
    slider.style.accentColor = col;
  }
  if (label) label.textContent = `${k}K`;
}

async function toggleLampInGroup(chIdx, grpIdx) {
  if (!latestState || !latestState.channels || !latestState.channels[chIdx]) return;
  const curMask = latestState.channels[chIdx][10] || 0;
  const nextMask = curMask ^ (1 << grpIdx);
  latestState.channels[chIdx][10] = nextMask;
  renderedGrpSig = "";
  await sendCmd({
    action: "configure_gear",
    index: chIdx,
    groups_mask: nextMask,
  });
}

function renderGroups(groups, channels, forceRebuild = false) {
  const list = document.getElementById("groups-grid");
  list.innerHTML = "";

  groups.forEach((lvl, idx) => {
    const members = [];
    (channels || []).forEach((ch, cIdx) => {
      if (ch[0] && (ch[10] & (1 << idx))) {
        members.push("A" + String(cIdx).padStart(2, "0"));
      }
    });
    const isSel = idx === selectedGrp;
    const gCode = "G" + String(idx).padStart(2, "0");
    const pct = Math.round((lvl * 100) / 254);
    const tile = document.createElement("div");
    tile.className =
      "lamp-tile" +
      (isSel ? " selected" : "") +
      (members.length > 0 && lvl > 0 ? " on" : "") +
      (members.length === 0 ? " absent" : "");
    tile.onclick = () => selectGroup(idx);

    tile.innerHTML = `
      <div class="tile-head">
        <span class="tile-addr">${gCode}</span>
        ${
          members.length > 0
            ? `<button class="tile-pwr-btn" title="${lvl > 0 ? "Turn Group Off" : "Turn Group On"}"
                 onclick="event.stopPropagation();sendCmd({action:'${lvl > 0 ? "switch_off" : "switch_on"}',target:'grp',index:${idx}})">⏻</button>`
            : ""
        }
      </div>
      <div class="tile-bar-track">
        <div class="tile-bar-fill" style="width:${members.length > 0 ? Math.max(lvl > 0 ? 8 : 0, pct) : 0}%;background:#3b82f6"></div>
      </div>
      <div class="tile-foot">
        <span class="tile-status">${members.length === 0 ? "Empty" : lvl > 0 ? `${pct}%` : "Off"}</span>
        <span class="tile-type">${members.length} lamp${members.length === 1 ? "" : "s"}</span>
      </div>
    `;
    list.appendChild(tile);
  });

  renderGroupDetail(selectedGrp, groups[selectedGrp] || 0, channels || [], forceRebuild);
}

function renderGroupDetail(grpIdx, lvl, channels, forceRebuild = false) {
  const pane = document.getElementById("group-detail-content");
  if (!pane) return;
  const gName = "G" + String(grpIdx).padStart(2, "0");
  const members = [];
  const lampTiles = [];
  let grpDt8 = 0;
  let sampleKelvin = 4000;
  let sampleR = 56;
  let sampleG = 189;
  let sampleB = 248;

  channels.forEach((ch, cIdx) => {
    const [present, , , , dt8, kelvin, r, g, b, , groupsMask, devType, , , , , , , switchOnly] = ch;
    if (!present) return;
    const isMember = (groupsMask & (1 << grpIdx)) !== 0;
    const isSwitch = Boolean(switchOnly || devType === 7);
    const addrStr = "A" + String(cIdx).padStart(2, "0");
    if (isMember) {
      members.push(addrStr);
      grpDt8 |= Number(devType) === 8 ? (dt8 || 15) : dt8;
      if (kelvin) sampleKelvin = kelvin;
      if (r || g || b) {
        sampleR = r;
        sampleG = g;
        sampleB = b;
      }
    }

    lampTiles.push(`
      <div class="group-lamp-item ${isMember ? "member" : ""}" onclick="toggleLampInGroup(${cIdx},${grpIdx})">
        <div class="tile-head">
          <strong class="mono">${addrStr}</strong>
          <span class="mono" style="font-size:11px;color:${isMember ? "var(--accent)" : "var(--text-3)"}">${isMember ? "✓ In group" : "+ Add"}</span>
        </div>
        <div class="tile-head">
          <span class="tile-type">${decodeTypeShort(dt8, devType, isSwitch)}</span>
          <button type="button" class="btn btn-xs" style="padding:1px 5px;font-size:10px"
            title="Blink ${addrStr}"
            onclick="event.stopPropagation();sendCmd({action:'find_lamp',target:'ch',index:${cIdx},mode:'blink'})">Blink</button>
        </div>
      </div>
    `);
  });

  const pct = Math.round((lvl * 100) / 254);
  const sig = `${grpIdx}:${members.join(",")}:${grpDt8}`;
  if (!forceRebuild && renderedGrpSig === sig) {
    const activeEl = document.activeElement;
    const briTxt = document.getElementById("grp-bri-text");
    if (briTxt) briTxt.textContent = `${pct}% (${lvl}/254)`;
    const briSlider = document.getElementById("grp-bri-slider");
    if (briSlider && activeEl !== briSlider) {
      briSlider.value = String(lvl);
    }
    return;
  }
  renderedGrpSig = sig;

  let colorHtml = "";
  if (members.length > 0 && grpDt8) {
    const availModes = [];
    if (grpDt8 & 1) availModes.push({ id: "cct", label: "White (CCT)" });
    if (grpDt8 & 6) availModes.push({ id: "rgb", label: grpDt8 & 4 ? "Color (RGBW)" : "Color (RGB)" });
    if (grpDt8 & 8) availModes.push({ id: "xy", label: "CIE xy" });

    let activeMode = selectedColorModeByGrp[grpIdx];
    if (!activeMode || !availModes.some((m) => m.id === activeMode)) {
      activeMode = availModes[0].id;
      selectedColorModeByGrp[grpIdx] = activeMode;
    }

    const modeTabsHtml =
      availModes.length > 1
        ? `<div class="segmented-control" style="margin-bottom:6px">
             ${availModes
               .map(
                 (m) =>
                   `<button type="button" class="pill-btn ${activeMode === m.id ? "active" : ""}"
                      onclick="setGroupColorMode(${grpIdx},'${m.id}')">${m.label}</button>`
               )
               .join("")}
           </div>`
        : "";

    let bodyHtml = "";
    if (activeMode === "cct") {
      const kCol = kelvinToCss(sampleKelvin);
      bodyHtml = `
        <div class="insp-row">
          <input type="range" id="grp-cct-slider" class="cct-range" min="2000" max="6500" step="100" value="${sampleKelvin}"
            style="--cct-thumb:${kCol};accent-color:${kCol}"
            oninput="previewGroupCctSlider(${grpIdx},this.value)"
            onchange="sendCmd({action:'set_cct',target:'grp',index:${grpIdx},kelvin:Number(this.value)})">
          <span class="mono" id="grp-cct-val" style="min-width:52px;text-align:right">${sampleKelvin}K</span>
        </div>
        <div class="btn-row">
          ${[2700, 3000, 4000, 5000, 6500]
            .map(
              (k) =>
                `<button class="btn btn-xs" onclick="previewGroupCctSlider(${grpIdx},${k});sendCmd({action:'set_cct',target:'grp',index:${grpIdx},kelvin:${k}})">${k}K</button>`
            )
            .join("")}
        </div>`;
    } else if (activeMode === "rgb") {
      const hexColor = rgbToHex(sampleR, sampleG, sampleB);
      bodyHtml = `
        <div class="swatch-row">
          ${COLOR_PRESETS.map(
            (p) =>
              `<button class="color-preset" style="background:${p.hex}" title="${p.name}"
                 onclick="sendCmd({action:'set_rgb',target:'grp',index:${grpIdx},r:${p.r},g:${p.g},b:${p.b}})"></button>`
          ).join("")}
          <input type="color" class="color-input" value="${hexColor}" onchange="sendHexRgb('grp',${grpIdx},this.value)">
        </div>
        ${
          grpDt8 & 4
            ? `<div class="insp-row" style="margin-top:4px">
                 <span style="font-size:11.5px;color:var(--text-2)">Dedicated White LED (W)</span>
                 <input type="range" min="0" max="254" value="0"
                   oninput="document.getElementById('grp-w-val').textContent=Math.round(this.value*100/254)+'%'"
                   onchange="sendCmd({action:'set_rgbw',target:'grp',index:${grpIdx},r:${sampleR},g:${sampleG},b:${sampleB},w:Number(this.value)})">
                 <span class="mono" id="grp-w-val" style="min-width:36px;text-align:right">0%</span>
               </div>`
            : ""
        }`;
    } else if (activeMode === "xy") {
      bodyHtml = `
        <div class="form-grid-3">
          <label class="field"><span>x (0..0.8)</span><input type="number" id="grp-xy-x" min="0" max="0.8" step="0.005" value="0.312"></label>
          <label class="field"><span>y (0..0.8)</span><input type="number" id="grp-xy-y" min="0" max="0.8" step="0.005" value="0.328"></label>
          <div class="field" style="justify-content:flex-end">
            <button class="btn btn-sm" onclick="sendCmd({action:'set_xy',target:'grp',index:${grpIdx},x:Math.round(Number(document.getElementById('grp-xy-x').value)*65535),y:Math.round(Number(document.getElementById('grp-xy-y').value)*65535),brightness:${lvl || 200}})">Apply</button>
          </div>
        </div>`;
    }

    colorHtml = `
      <div class="insp-section">
        <span class="insp-sec-title">Group Color Control (DT8)</span>
        ${modeTabsHtml}
        ${bodyHtml}
      </div>`;
  }

  pane.innerHTML = `
    <div class="insp-header">
      <div>
        <div class="insp-title">Group ${gName}</div>
        <div class="insp-badges">
          <span class="badge ${members.length > 0 ? "ok" : ""}">${members.length} lamp${members.length === 1 ? "" : "s"} assigned</span>
          ${grpDt8 & 1 ? '<span class="badge">CCT</span>' : ""}
          ${grpDt8 & 4 ? '<span class="badge">RGBW</span>' : grpDt8 & 2 ? '<span class="badge">RGB</span>' : ""}
          ${grpDt8 & 8 ? '<span class="badge">CIE xy</span>' : ""}
        </div>
      </div>
      ${
        members.length > 0
          ? `<div class="btn-row">
               <button class="btn btn-xs" onclick="sendCmd({action:'find_lamp',target:'grp',index:${grpIdx},mode:'blink'})">Blink Group</button>
             </div>`
          : ""
      }
    </div>

    ${
      members.length > 0
        ? `<div class="insp-section">
             <div class="insp-row">
               <span class="insp-sec-title">Group Brightness</span>
               <span class="mono" id="grp-bri-text">${pct}% (${lvl}/254)</span>
             </div>
             <div class="insp-row">
               <button class="btn btn-sm" onclick="sendCmd({action:'switch_on',target:'grp',index:${grpIdx}})">All On</button>
               <button class="btn btn-sm" onclick="sendCmd({action:'switch_off',target:'grp',index:${grpIdx}})">All Off</button>
               <input type="range" id="grp-bri-slider" min="0" max="254" value="${lvl}"
                 oninput="document.getElementById('grp-bri-text').textContent=Math.round(this.value*100/254)+'% ('+this.value+'/254)'"
                 onchange="sendCmd({action:'set_level',target:'grp',index:${grpIdx},level:Number(this.value)})">
             </div>
           </div>
           ${colorHtml}`
        : ""
    }

    <div class="insp-section">
      <span class="insp-sec-title">Assign Installed Lamps to ${gName}</span>
      <p class="subtext">Click any lamp below to add or remove it from ${gName}. Use Blink to check which physical light it is.</p>
      <div class="group-lamps-grid">
        ${lampTiles.join("") || '<span class="subtext">No installed lamps found.</span>'}
      </div>
    </div>
  `;
}

function selectScenePill(s) {
  selectedScene = s;
  if (latestState) renderScenes(latestState);
}

function recallSelectedScene() {
  sendCmd({ action: "recall_scene", scene: selectedScene });
}

function storeSelectedScene() {
  sendCmd({ action: "store_scene", scene: selectedScene });
}

function renderScenes(data) {
  const scenesBar = document.getElementById("scenes-bar");
  scenesBar.innerHTML = "";
  const activeScenes = Array.isArray(data.active_scenes) ? data.active_scenes : null;
  for (let s = 0; s < 16; s++) {
    if (activeScenes && activeScenes.length > 0 && !activeScenes.includes(s)) {
      continue;
    }
    const b = document.createElement("button");
    b.type = "button";
    b.className = "scene-pill" + (s === selectedScene ? " active" : "");
    b.textContent = "Scene " + (s + 1);
    b.onclick = () => {
      selectScenePill(s);
      sendCmd({ action: "recall_scene", scene: s });
    };
    scenesBar.appendChild(b);
  }
}

function decodeInputEvent(type, info) {
  if (type === 1) {
    // Part 301 Push button
    if (info & 0x04) return "Short Press";
    if (info & 0x08) return "Double Press";
    if (info & 0x10) return "Long Press Start";
    if (info & 0x20) return "Long Press Repeat";
    if (info & 0x40) return "Long Press Stop";
    return `Button (0x${info.toString(16)})`;
  }
  if (type === 3) {
    // Part 303 Occupancy sensor
    if (info & 0x08) return "Movement Detected";
    if (info & 0x02) return "Occupied";
    if (info & 0x01) return "Vacant";
    return `Occupancy (0x${info.toString(16)})`;
  }
  if (type === 4) {
    // Part 304 Light sensor
    return `${info} lx`;
  }
  return `Event 0x${info.toString(16)}`;
}

function inputTypeName(type) {
  if (type === 1) return "Push Button";
  if (type === 2) return "Switch";
  if (type === 3) return "Occupancy";
  if (type === 4) return "Light Sensor";
  return `Type ${type}`;
}

// Show ONLY discovered DALI-2 sensors & buttons + live instance events
function renderInputs(inputs, inputEvents) {
  const grid = document.getElementById("inputs-grid");
  grid.innerHTML = "";
  let any = 0;
  const evs = inputEvents || [];

  inputs.forEach((item, idx) => {
    const [present, summary, instTypes] = item;
    if (!present) return;
    any++;
    const instCount = summary & 0x1f;
    const dStr = "D" + String(idx).padStart(2, "0");

    // Summarize instance types cleanly (e.g. "4× Push Button" or "Occupancy · Light Sensor")
    const typeCounts = {};
    if (Array.isArray(instTypes) && instTypes.length > 0) {
      instTypes.forEach((t) => {
        const name = inputTypeName(t);
        typeCounts[name] = (typeCounts[name] || 0) + 1;
      });
    } else {
      if (summary & 0x20) typeCounts["Push Button"] = instCount || 1;
      if (summary & 0x40) typeCounts["Occupancy"] = 1;
      if (summary & 0x80) typeCounts["Light Sensor"] = 1;
    }
    const typeSummary = Object.entries(typeCounts)
      .map(([name, cnt]) => (cnt > 1 ? `${cnt}× ${name}` : name))
      .join(" · ");

    // Find most recent live event for this sensor
    const lastEv = evs.find((e) => e[0] === idx);
    const statusLine = lastEv
      ? `I${String(lastEv[1]).padStart(2, "0")}: ${decodeInputEvent(lastEv[2], lastEv[3])}`
      : `${instCount} instance${instCount === 1 ? "" : "s"}`;
    const isRecent = Boolean(lastEv && lastEv[4] <= 15);

    const tile = document.createElement("div");
    tile.className =
      "lamp-tile" +
      (lampFilter === "sensors" && idx === selectedSensor ? " selected" : "") +
      (isRecent ? " on" : "");
    tile.onclick = () => selectSensor(idx);
    tile.innerHTML = `
      <div class="tile-head">
        <span class="tile-addr">${dStr}</span>
        <button class="tile-pwr-btn" title="Blink physical LED on ${dStr} (Identify Device)"
          onclick="event.stopPropagation();sendCmd({action:'identify_input',index:${idx}})">◎</button>
      </div>
      <div class="tile-bar-track">
        <div class="tile-bar-fill" style="width:${lastEv ? 100 : 35}%;background:${isRecent ? "#10b981" : "#3b82f6"}"></div>
      </div>
      <div class="tile-foot">
        <span class="tile-status">${statusLine}</span>
        <span class="tile-type">${typeSummary || "Input device"}</span>
      </div>
    `;
    grid.appendChild(tile);
  });
  const cntSensors = document.getElementById("cnt-sensors");
  if (cntSensors) cntSensors.textContent = String(any);
  if (!any) {
    grid.innerHTML = '<div class="subtext">No DALI-2 sensors or buttons found on the bus.</div>';
  }

  const tbody = document.getElementById("input-events-tbody");
  if (!tbody) return;
  tbody.innerHTML = "";
  if (evs.length === 0) {
    tbody.innerHTML =
      '<tr><td colspan="5" class="subtext">No recent sensor/button events yet. Press a DALI-2 button to see its address and instance here.</td></tr>';
    return;
  }
  evs.forEach((ev) => {
    const [dev, inst, type, info, ageS] = ev;
    const tr = document.createElement("tr");
    tr.innerHTML = `
      <td class="mono"><strong>${dev}</strong> <span class="subtext">(D${String(dev).padStart(2, "0")})</span></td>
      <td class="mono"><strong>${inst}</strong> <span class="subtext">(I${String(inst).padStart(2, "0")})</span></td>
      <td><span class="badge">${inputTypeName(type)}</span></td>
      <td><strong>${decodeInputEvent(type, info)}</strong></td>
      <td class="mono">${ageS === 0 ? "Just now" : `${ageS}s ago`}</td>
    `;
    tbody.appendChild(tr);
  });
}

function renderSensorInspector(devIdx) {
  const pane = document.getElementById("inspector-content");
  if (!pane || !latestState) return;
  const inputs = latestState.inputs || [];
  const item = inputs[devIdx] || [0, 0, []];
  const [present, summary, instTypes] = item;
  const dStr = "D" + String(devIdx).padStart(2, "0");
  const evs = latestState.input_events || [];

  const instances = Array.isArray(instTypes) && instTypes.length > 0 ? instTypes : [];
  const instRows = instances.map((t, iIdx) => {
    const iStr = "I" + String(iIdx).padStart(2, "0");
    const lastEv = evs.find((e) => e[0] === devIdx && e[1] === iIdx);
    const valStr = lastEv ? decodeInputEvent(lastEv[2], lastEv[3]) : "Idle";
    let filterOpts = "";
    if (t === 1) {
      filterOpts = `
        <select class="sensor-filter-sel">
          <option value="244" selected>Short + Long Press (Dimming)</option>
          <option value="4">Short Press Only</option>
          <option value="12">Short + Double Press</option>
          <option value="0">Disabled</option>
        </select>`;
    } else if (t === 3) {
      filterOpts = `
        <select class="sensor-filter-sel">
          <option value="27" selected>Occupied + Vacant + Movement</option>
          <option value="3">Occupied + Vacant Only</option>
          <option value="0">Disabled</option>
        </select>`;
    } else if (t === 4) {
      filterOpts = `
        <select class="sensor-filter-sel">
          <option value="1" selected>Report Lux Changes</option>
          <option value="0">Disabled</option>
        </select>`;
    } else {
      filterOpts = `
        <select class="sensor-filter-sel">
          <option value="3" selected>On / Off</option>
          <option value="0">Disabled</option>
        </select>`;
    }

    return `
      <div class="action-row" style="flex-direction:column;align-items:stretch;gap:8px">
        <div class="tile-head">
          <div>
            <strong class="mono">${iStr}</strong>
            <span class="badge" style="margin-left:6px">${inputTypeName(t)}</span>
          </div>
          <span class="mono" style="font-size:11.5px;color:${lastEv ? "var(--ok)" : "var(--text-2)"}">${valStr}</span>
        </div>
        <div class="insp-row">
          <span style="font-size:11.5px;color:var(--text-2)">Event Filter</span>
          ${filterOpts}
        </div>
      </div>
    `;
  });

  pane.innerHTML = `
    <div class="insp-header">
      <div>
        <div class="insp-title">Sensor ${dStr} <span style="font-weight:400;color:var(--text-2);font-size:12px">(Device ${devIdx})</span></div>
        <div class="insp-badges">
          <span class="badge ${present ? "ok" : ""}">${present ? "Installed" : "Not Installed"}</span>
          <span class="badge">${instances.length || (summary & 0x1f)} instance${(instances.length || (summary & 0x1f)) === 1 ? "" : "s"}</span>
        </div>
      </div>
      <div class="btn-row">
        <button class="btn btn-xs" title="Blink LED on physical sensor for 10s (IEC 62386-103 IDENTIFY DEVICE)"
          onclick="sendCmd({action:'identify_input',index:${devIdx}})">Identify LED</button>
      </div>
    </div>

    <div class="insp-section">
      <span class="insp-sec-title">Instances &amp; Event Filters (IEC 62386-301..304)</span>
      <div class="action-stack">
        ${instRows.join("") || '<span class="subtext">No instances discovered on this address.</span>'}
      </div>
    </div>

    <div class="insp-section">
      <div class="btn-row">
        <button class="btn btn-primary btn-sm" onclick="sendCmd({action:'configure_input',index:${devIdx}})">Save to Sensor</button>
        <button class="btn btn-sm" onclick="sendCmd({action:'scan_inputs'})">Re-Scan Sensors</button>
      </div>
      <div class="subtext">In ETS (Input 1..32), set Device Address = ${devIdx} and Instance = 0..${Math.max(0, instances.length - 1)}.</div>
    </div>
  `;
}

function handleLiveState(data) {
  latestState = data;
  const chipId = data.chip_id || "f5aa11f";
  const fwVer = data.fw_version || "26.10.15.1915";
  const iaStr = formatIa(data.knx.ia);

  // Header shows only firmware version
  document.getElementById("hdr-meta").textContent = `v${fwVer}`;

  const chipPreview = document.getElementById("cfg-chip-id-preview");
  if (chipPreview) chipPreview.textContent = chipId;

  // 1. Lamps card
  const d = data.dali || {};
  document.getElementById("sum-lamps").textContent = `${d.present || 0} / 64`;
  const errTag = document.getElementById("sum-lamps-err");
  if ((d.faulty || 0) > 0) {
    errTag.style.display = "inline-block";
    errTag.textContent = `${d.faulty} error${d.faulty === 1 ? "" : "s"}`;
  } else {
    errTag.style.display = "none";
  }
  const activeCnt = d.active || 0;
  document.getElementById("sum-comm-state").textContent =
    d.comm_phase && d.comm_phase !== "idle"
      ? `Scanning (${d.comm_progress}%)`
      : `${activeCnt} ${activeCnt === 1 ? "is" : "are"} on`;

  // 2. Power card
  const nomW = Number(document.getElementById("nominal-watts")?.value ?? 15.0);
  const idleWDefault = Number(document.getElementById("idle-watts")?.value ?? 0.4);
  let activeW = 0;
  let standbyW = 0;
  (data.channels || []).forEach((ch) => {
    const p = channelPowerBreakdown(ch, nomW, idleWDefault);
    if (p.source === "off") return;
    activeW += p.lampW;
    standbyW += p.idleW;
  });
  const totalW = activeW + standbyW;
  const dailyKwh = (totalW * 24) / 1000;
  document.getElementById("pwr-active-w").textContent = `${totalW.toFixed(1)} W`;
  document.getElementById("pwr-daily-kwh").textContent = `${dailyKwh.toFixed(2)} kWh/d`;
  document.getElementById("pwr-standby-w").textContent =
    `Lamps ${activeW.toFixed(1)}W • Idle ${standbyW.toFixed(1)}W`;

  // 3. DALI Bus Supply card (compact)
  const psuEl = document.getElementById("pwr-psu-state");
  const psuSub = document.getElementById("pwr-psu-sub");
  const rearmBtn = document.getElementById("btn-rearm-psu");
  if (d.psu_short) {
    psuEl.textContent = "SHORT";
    psuEl.className = "summary-val err";
    psuSub.textContent = "Short circuit";
    rearmBtn.style.display = "inline-block";
  } else if (d.psu_on) {
    psuEl.textContent = "ON";
    psuEl.className = "summary-val ok";
    psuSub.textContent = "Internal";
    rearmBtn.style.display = "none";
  } else {
    psuEl.textContent = "EXT";
    psuEl.className = "summary-val";
    psuSub.textContent = "External";
    rearmBtn.style.display = "none";
  }

  // 4. KNX card (compact)
  const knxSt = document.getElementById("sum-knx-state");
  knxSt.textContent = data.knx.bus_ok ? (data.knx.prog_mode ? "PROG" : "ON") : "OFF";
  knxSt.className = "summary-val " + (data.knx.bus_ok ? "ok" : "err");
  document.getElementById("sum-knx-ia").textContent = iaStr;

  // 5. MQTT card (compact)
  const mqttSt = document.getElementById("sum-mqtt-state");
  mqttSt.textContent = data.net.mqtt ? "ON" : "OFF";
  mqttSt.className = "summary-val " + (data.net.mqtt ? "ok" : "");
  document.getElementById("sum-mqtt-sub").textContent = data.net.mqtt ? "Online" : "Offline";

  // 6. Stats card (2-row Uptime + Free RAM)
  document.getElementById("sum-uptime").textContent = formatUptime(data.uptime_s || 0);
  document.getElementById("sum-ram").textContent =
    `${Math.round((data.free_heap || 0) / 1024)} KB`;

  renderLampsGrid(data.channels || []);
  renderInputs(data.inputs || [], data.input_events || []);
  if (lampFilter === "sensors") {
    if (renderedInspectorCh !== -100 - selectedSensor) {
      renderedInspectorCh = -100 - selectedSensor;
      renderSensorInspector(selectedSensor);
    }
  } else {
    renderInspector(selectedCh);
  }
  renderGroups(data.groups || [], data.channels || []);
  renderScenes(data);

  document.getElementById("info-fw-ver").textContent = fwVer;
  document.getElementById("info-part").textContent = data.partition || "ota_0";
}

async function applyScenario(name) {
  try {
    const res = await fetch("/api/scenario", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ scenario: name }),
    });
    if (res.ok) {
      const st = await res.json();
      handleLiveState(st);
      loadErrorLogs();
      loadDebugTrace();
    }
  } catch (_) {}
}

function connectLive() {
  const wsPill = document.getElementById("pill-ws");
  const wsText = document.getElementById("ws-text");

  fetch("/api/state")
    .then((r) => (r.ok ? r.json() : Promise.reject()))
    .then((st) => {
      mockMode = true;
      wsPill.className = "conn-badge ok";
      wsText.textContent = "Simulated Lab";
      handleLiveState(st);
      setInterval(async () => {
        try {
          const r = await fetch("/api/state");
          if (r.ok) handleLiveState(await r.json());
        } catch (_) {}
      }, 1000);
    })
    .catch(() => {
      document.getElementById("scenario-bar").style.display = "none";
      const proto = location.protocol === "https:" ? "wss:" : "ws:";
      const url = `${proto}//${location.host}/ws`;
      try {
        ws = new WebSocket(url);
      } catch (_) {
        setTimeout(connectLive, 2000);
        return;
      }
      ws.onopen = () => {
        wsPill.className = "conn-badge ok";
        wsText.textContent = "Live";
        syncGatewayTime();
      };
      ws.onmessage = (ev) => {
        try {
          const msg = JSON.parse(ev.data);
          if (msg.type === "live") handleLiveState(msg);
        } catch (_) {}
      };
      ws.onclose = () => {
        wsPill.className = "conn-badge err";
        wsText.textContent = "Reconnecting";
        setTimeout(connectLive, 2000);
      };
    });
}

async function sendCmd(payload) {
  if (ws && ws.readyState === WebSocket.OPEN && payload.action !== "query_gear") {
    ws.send(JSON.stringify(payload));
    return;
  }
  const res = await fetch("/api/command", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  });
  if (mockMode && res.ok) {
    const r = await fetch("/api/state");
    if (r.ok) handleLiveState(await r.json());
  }
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

async function loadConfig() {
  try {
    const res = await fetch("/api/config");
    const c = await res.json();
    document.getElementById("cfg-wifi-enabled").checked = !!c.wifi_enabled;
    document.getElementById("cfg-wifi-ps").checked = !!c.wifi_power_save;
    document.getElementById("cfg-wifi-ssid").value = c.wifi_ssid || "";
    document.getElementById("cfg-wifi-pass").value = c.wifi_password || "";
    document.getElementById("cfg-hostname").value = c.hostname || "knx-dali-gw";
    document.getElementById("cfg-mqtt-enabled").checked = !!c.mqtt_enabled;
    document.getElementById("cfg-mqtt-host").value = c.mqtt_host || "";
    document.getElementById("cfg-mqtt-port").value = c.mqtt_port || 1883;
    document.getElementById("cfg-mqtt-user").value = c.mqtt_username || "";
    document.getElementById("cfg-mqtt-pass").value = c.mqtt_password || "";
    document.getElementById("cfg-mqtt-base").value = c.mqtt_base_topic || "knx_dali";
    document.getElementById("cfg-ha-prefix").value = c.ha_discovery_prefix || "homeassistant";
    document.getElementById("cfg-oled-geom").value = String(c.oled_geometry ?? 0);
    document.getElementById("cfg-oled-sleep").value = c.oled_sleep_seconds ?? 60;
    document.getElementById("cfg-dali-psu").checked = !!c.internal_dali_psu_enabled;
  } catch (_) {}
}

async function saveConfig(ev) {
  ev.preventDefault();
  const payload = {
    wifi_enabled: document.getElementById("cfg-wifi-enabled").checked,
    ap_fallback_enabled: true,
    wifi_power_save: document.getElementById("cfg-wifi-ps").checked,
    wifi_ssid: document.getElementById("cfg-wifi-ssid").value,
    wifi_password: document.getElementById("cfg-wifi-pass").value,
    hostname: document.getElementById("cfg-hostname").value,
    mqtt_enabled: document.getElementById("cfg-mqtt-enabled").checked,
    ha_discovery_enabled: true,
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
  msg.textContent = "Settings saved.";
  setTimeout(() => (msg.textContent = ""), 3000);
}

async function loadDebugTrace() {
  try {
    const res = await fetch("/api/debug");
    const d = await res.json();
    window.lastTraceData = d;
    document.getElementById("dbg-autotrigger").checked = !!d.auto_trigger;
    const cap = d.capacity || 512;
    const buf = d.buffered ?? (d.entries ? d.entries.length : 0);
    const over = d.total_frames > buf ? d.total_frames - buf : 0;
    document.getElementById("trace-stats").textContent =
      `${d.active ? "Recording on gateway (continues with browser closed)" : "Paused"} • Buffer: ${buf}/${cap} frames${over > 0 ? ` (${over} older overwritten)` : ""} • ${d.total_collisions} collisions`;
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
  } catch (_) {}
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
  try {
    const res = await fetch("/api/logs");
    const d = await res.json();
    window.lastLogData = d;
    const tbody = document.getElementById("logs-tbody");
    tbody.innerHTML = "";
    (d.entries || []).forEach((e) => {
      const tr = document.createElement("tr");
      const sevClass = e.severity === "INFO" ? "ok" : e.severity === "WARN" ? "warn" : "err";
      const addrStr = e.address === 65535 ? "Bus / System" : `Lamp A${String(e.address & 63).padStart(2, "0")}`;
      tr.innerHTML = `
        <td class="mono">${e.seq}</td>
        <td class="mono">${formatTimestamp(e.epoch_ms, e.uptime_ms)}</td>
        <td class="mono">${e.source}</td>
        <td><span class="badge ${sevClass}">${e.severity}</span></td>
        <td><strong>E${e.code}: ${e.title}</strong></td>
        <td class="mono">${addrStr}</td>
        <td class="mono">×${e.count}</td>
        <td><div>${e.description || e.detail}</div><div class="subtext">${e.remedy}</div></td>
      `;
      tbody.appendChild(tr);
    });
  } catch (_) {}
}

async function clearErrorLogs() {
  await fetch("/api/logs/clear", { method: "POST" });
  await loadErrorLogs();
}

function exportJson(filename, obj) {
  const blob = new Blob([JSON.stringify(obj, null, 2)], { type: "application/json" });
  const a = document.createElement("a");
  a.href = URL.createObjectURL(blob);
  a.download = filename;
  a.click();
}

function uploadOta(target) {
  const fileInput = document.getElementById("file-" + target);
  const prog = document.getElementById("prog-" + target);
  const status = document.getElementById("status-" + target);
  if (!fileInput.files || fileInput.files.length === 0) {
    status.textContent = "Please choose a .bin file first.";
    return;
  }
  const file = fileInput.files[0];
  status.textContent = `Uploading ${file.name} (${Math.round(file.size / 1024)} KB)...`;
  prog.style.width = "0%";

  const xhr = new XMLHttpRequest();
  xhr.open("POST", `/api/ota/${target}`, true);
  xhr.upload.onprogress = (ev) => {
    if (ev.lengthComputable) {
      prog.style.width = Math.round((ev.loaded * 100) / ev.total) + "%";
    }
  };
  xhr.onload = () => {
    if (xhr.status === 200) {
      prog.style.width = "100%";
      status.textContent =
        target === "firmware"
          ? "Firmware updated. Gateway is restarting..."
          : "Web interface updated. Reloading...";
      setTimeout(() => location.reload(), 3000);
    } else {
      status.textContent = `Update failed (${xhr.status}): ${xhr.responseText}`;
    }
  };
  xhr.onerror = () => {
    status.textContent = "Upload connection interrupted.";
  };
  xhr.send(file);
}

async function rebootDevice() {
  await fetch("/api/reboot", { method: "POST" });
}

document.addEventListener("DOMContentLoaded", () => {
  initUi();
  connectLive();
});
