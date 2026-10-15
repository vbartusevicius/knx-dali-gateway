#pragma once
// clang-format off

#include <cstddef>

namespace gateway::firmware::web_assets {

inline constexpr const char index_html_data[] = R"WEBUI(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>KNX-DALI Gateway — Console &amp; Diagnostics</title>
  <link rel="stylesheet" href="app.css">
</head>
<body>
  <header class="topbar">
    <div class="brand">
      <span class="brand-badge">KNX ⇄ DALI-2</span>
      <div>
        <h1>KNX-DALI Gateway</h1>
        <p class="subtitle" id="hdr-sub">ESP32-C6 • Wired KNX TP + Multi-Master DALI-2 / DT8</p>
      </div>
    </div>
    <div class="status-pills">
      <span class="pill" id="pill-ws"><span class="dot"></span>WS: Connecting</span>
      <span class="pill" id="pill-knx">KNX: --</span>
      <span class="pill" id="pill-dali">DALI: --/64</span>
      <span class="pill" id="pill-mqtt">MQTT: --</span>
      <span class="pill" id="pill-uptime">UP: --</span>
    </div>
  </header>

  <nav class="tabbar">
    <button class="tab active" data-tab="monitoring">Live Monitoring</button>
    <button class="tab" data-tab="configuration">Configuration</button>
    <button class="tab" data-tab="logs">Logs &amp; Debug</button>
    <button class="tab" data-tab="firmware">Firmware Update</button>
  </nav>

  <main class="container">
    <!-- PANEL 1: LIVE MONITORING -->
    <section id="panel-monitoring" class="panel active">
      <div class="grid-top">
        <div class="card front-panel-card">
          <div class="card-header">
            <h2>Front Panel Live Mirror (OLED + SK6805 LEDs)</h2>
            <div class="btn-group">
              <button class="btn btn-sm" id="btn-oled-page">SW2 FUNC (Next Page)</button>
              <button class="btn btn-sm" id="btn-oled-geom">128×64 ⇄ 64×128</button>
            </div>
          </div>
          <div class="front-panel-wrap">
            <div class="oled-bezel">
              <canvas id="oled-canvas" width="128" height="64"></canvas>
              <div class="oled-caption" id="oled-meta">SSD1306 128×64 • Page 1/3</div>
            </div>
            <div class="leds-column">
              <div class="led-item"><span class="led-dot" id="led-0"></span><span>D1 STATUS</span></div>
              <div class="led-item"><span class="led-dot" id="led-1"></span><span>D2 KNX</span></div>
              <div class="led-item"><span class="led-dot" id="led-2"></span><span>D3 DALI</span></div>
            </div>
          </div>
        </div>

        <div class="card broadcast-card">
          <div class="card-header">
            <h2>Broadcast &amp; Scene Control</h2>
            <span class="badge" id="comm-status-badge">Commissioning: Idle</span>
          </div>
          <div class="broadcast-controls">
            <div class="control-row">
              <span class="label">Broadcast All Lamps:</span>
              <button class="btn btn-primary" onclick="sendCmd({action:'switch_on',target:'broadcast'})">ALL ON</button>
              <button class="btn" onclick="sendCmd({action:'switch_off',target:'broadcast'})">ALL OFF</button>
              <input type="range" min="0" max="254" value="254" id="bcast-level"
                onchange="sendCmd({action:'set_level',target:'broadcast',level:Number(this.value)})">
              <span class="mono" id="bcast-level-val">254</span>
            </div>
            <div class="control-row">
              <span class="label">Broadcast CCT / RGB:</span>
              <input type="range" min="2000" max="6500" step="50" value="4000" id="bcast-cct" class="cct-range"
                onchange="sendCmd({action:'set_cct',target:'broadcast',kelvin:Number(this.value)})">
              <span class="mono" id="bcast-cct-val">4000K</span>
              <input type="color" value="#ffaa44" id="bcast-rgb" title="Broadcast DT8 RGB Colour"
                onchange="sendHexRgb('broadcast',0,this.value)">
            </div>
            <div class="scenes-bar" id="scenes-bar"></div>
          </div>
        </div>
      </div>

      <div class="card">
        <div class="card-header">
          <div>
            <h2>DALI Power, Bus Current &amp; Energy Monitor (REM3-2415S/A/CTRL + IEC 62386-102/252)</h2>
            <p class="muted">Monitors the isolated 16 V / 250 mA DALI bus supply, short-circuit protection latch, and logarithmic lighting power across all 64 ballasts.</p>
          </div>
          <div class="inline-controls">
            <span class="muted" style="font-size:12px">Nominal Fixture Wattage (W):</span>
            <input type="number" id="nominal-watts" min="1" max="200" value="15" style="width:68px" onchange="if(latestState)handleLiveState(latestState)">
            <button class="btn btn-sm" onclick="sendCmd({action:'reset_psu'})">Re-Arm DALI PSU</button>
          </div>
        </div>
        <div class="power-metrics-grid">
          <div class="metric-box">
            <span class="metric-label">DALI Bus Supply (REM3)</span>
            <span class="metric-value" id="pwr-psu-state">16.0 V ACTIVE</span>
            <span class="metric-sub" id="pwr-psu-sub">Short-circuit protection armed (&gt;80 ms cutoff)</span>
          </div>
          <div class="metric-box">
            <span class="metric-label">DALI Bus Current Budget (Max 250 mA)</span>
            <span class="metric-value" id="pwr-bus-ma">0 mA / 250 mA</span>
            <div class="progress-wrap" style="margin-top:6px"><div class="progress-bar" id="pwr-bus-bar" style="width:0%"></div></div>
            <span class="metric-sub" id="pwr-bus-sub">0 gear (2 mA ea) + 0 sensors (5 mA ea)</span>
          </div>
          <div class="metric-box">
            <span class="metric-label">Active Lighting Power (IEC 62386 Curve)</span>
            <span class="metric-value" id="pwr-active-w">0.0 W</span>
            <span class="metric-sub" id="pwr-standby-w">Standby draw: 0.0 W (0.4 W/driver)</span>
          </div>
          <div class="metric-box">
            <span class="metric-label">Est. Daily Consumption &amp; Bus Health</span>
            <span class="metric-value" id="pwr-daily-kwh">0.00 kWh/d</span>
            <span class="metric-sub" id="pwr-bus-health">TX: 0 frames • Collisions: 0</span>
          </div>
        </div>
      </div>

      <div class="card">
        <div class="card-header">
          <h2>DALI Ballasts / Drivers &amp; Lamps (64 Short Addresses A00..A63)</h2>
          <div class="filter-bar">
            <button class="btn btn-sm active" data-filter="all" onclick="setLampFilter('all',this)">All (64)</button>
            <button class="btn btn-sm" data-filter="present" onclick="setLampFilter('present',this)">Present</button>
            <button class="btn btn-sm" data-filter="on" onclick="setLampFilter('on',this)">Active ON</button>
            <button class="btn btn-sm" data-filter="fault" onclick="setLampFilter('fault',this)">Faults</button>
            <button class="btn btn-sm" onclick="sendCmd({action:'find_lamp',target:'broadcast',mode:'stop'})">Stop Finder</button>
            <button class="btn btn-sm" onclick="sendCmd({action:'scan_bus'})">Scan DALI Bus</button>
          </div>
        </div>
        <div class="lamps-grid" id="lamps-grid"></div>
      </div>

      <div class="grid-two">
        <div class="card">
          <div class="card-header">
            <h2>DALI Groups (G00..G15)</h2>
          </div>
          <div class="groups-grid" id="groups-grid"></div>
        </div>

        <div class="card">
          <div class="card-header">
            <h2>DALI-2 Input Devices (Part 103 Multi-Master)</h2>
            <button class="btn btn-sm" onclick="sendCmd({action:'scan_inputs'})">Scan Inputs</button>
          </div>
          <div class="inputs-grid" id="inputs-grid"></div>
        </div>
      </div>
    </section>

    <!-- PANEL 2: CONFIGURATION -->
    <section id="panel-configuration" class="panel">
      <div class="grid-two">
        <div class="card">
          <div class="card-header">
            <h2>WiFi &amp; Setup Portal Configuration</h2>
          </div>
          <form id="form-config" onsubmit="saveConfig(event)">
            <label class="field-check">
              <input type="checkbox" id="cfg-wifi-enabled" checked>
              <span>Enable WiFi Radio (KNX/DALI control always runs independently)</span>
            </label>
            <label class="field-check">
              <input type="checkbox" id="cfg-ap-fallback" checked>
              <span>Enable SoftAP Setup Portal Fallback (SSID: <code id="cfg-ap-ssid-preview">KNX-DALI-SETUP-f5aa11f</code>)</span>
            </label>
            <label class="field-check">
              <input type="checkbox" id="cfg-wifi-ps" checked>
              <span>Enable DTIM Modem Sleep (recommended for KNX bus power budget)</span>
            </label>
            <div class="field">
              <span>WiFi Station SSID</span>
              <input type="text" id="cfg-wifi-ssid" maxlength="32" placeholder="Home-IoT-24G">
            </div>
            <div class="field">
              <span>WiFi Password</span>
              <input type="password" id="cfg-wifi-pass" maxlength="64" placeholder="WPA2/WPA3 Password">
            </div>
            <div class="field">
              <span>Device Hostname / Unique Node Prefix (Chip ID <code id="cfg-chip-id-preview">f5aa11f</code> is appended automatically in MQTT/HA)</span>
              <input type="text" id="cfg-hostname" maxlength="32" value="knx-dali-gw">
            </div>

            <hr class="sep">
            <h3>MQTT &amp; Home Assistant Auto-Discovery</h3>
            <label class="field-check">
              <input type="checkbox" id="cfg-mqtt-enabled">
              <span>Enable MQTT Client (auto-reconnects with exponential backoff)</span>
            </label>
            <label class="field-check">
              <input type="checkbox" id="cfg-ha-enabled" checked>
              <span>Publish Home Assistant MQTT Auto-Discovery topics</span>
            </label>
            <div class="field-row">
              <div class="field">
                <span>MQTT Broker Host / IP</span>
                <input type="text" id="cfg-mqtt-host" maxlength="64" placeholder="192.168.1.10">
              </div>
              <div class="field" style="max-width:120px">
                <span>Port</span>
                <input type="number" id="cfg-mqtt-port" min="1" max="65535" value="1883">
              </div>
            </div>
            <div class="field-row">
              <div class="field">
                <span>MQTT Username</span>
                <input type="text" id="cfg-mqtt-user" maxlength="32">
              </div>
              <div class="field">
                <span>MQTT Password</span>
                <input type="password" id="cfg-mqtt-pass" maxlength="64">
              </div>
            </div>
            <div class="field-row">
              <div class="field">
                <span>MQTT Base Topic</span>
                <input type="text" id="cfg-mqtt-base" maxlength="32" value="knx_dali">
              </div>
              <div class="field">
                <span>HA Discovery Prefix</span>
                <input type="text" id="cfg-ha-prefix" maxlength="32" value="homeassistant">
              </div>
            </div>

            <hr class="sep">
            <h3>Hardware, Front-Panel OLED &amp; DALI Bus Supply</h3>
            <div class="field-row">
              <div class="field">
                <span>Front-Panel OLED Display Type</span>
                <select id="cfg-oled-geom">
                  <option value="0">Auto (V3 — 0.96" 64×128 Portrait)</option>
                  <option value="2">V3 — 0.96" Winstar/SSD1312 64×128 Portrait (14×28 mm)</option>
                  <option value="1">V1 / V2 — 0.96" Winstar 128×64 Landscape SSD1306</option>
                  <option value="3">Disabled (OLED Off)</option>
                </select>
              </div>
              <div class="field" style="max-width:160px">
                <span>OLED Sleep (s, 0=Always On)</span>
                <input type="number" id="cfg-oled-sleep" min="0" max="3600" value="60">
              </div>
            </div>
            <label class="field-check">
              <input type="checkbox" id="cfg-dali-psu" checked>
              <span>Enable Internal DALI Bus PSU (REM3-2415S/A/CTRL via D1/GPIO1; latches OFF on bus short)</span>
            </label>
            <div style="margin-bottom:10px">
              <button type="button" class="btn btn-sm" onclick="sendCmd({action:'reset_psu'})">Re-Arm DALI PSU After Short Trip</button>
            </div>

            <div class="form-actions">
              <button type="submit" class="btn btn-primary">Save &amp; Apply Configuration</button>
              <span id="cfg-save-msg" class="save-msg"></span>
            </div>
          </form>
        </div>

        <div class="card">
          <div class="card-header">
            <h2>DALI Commissioning &amp; Maintenance</h2>
          </div>
          <p class="muted">
            ETS owns the authoritative KNX association &amp; group configuration. Commissioning commands below run through the same serialized DALI worker queue as ETS function properties.
          </p>
          <div class="action-list">
            <div class="action-item">
              <div>
                <strong>Scan DALI Control Gear (0..63)</strong>
                <p>Queries presence, status byte, and DT8 colour capabilities on all 64 short addresses.</p>
              </div>
              <button class="btn btn-primary" onclick="sendCmd({action:'scan_bus'})">Scan Gear</button>
            </div>
            <div class="action-item">
              <div>
                <strong>Address Unaddressed DALI Gear</strong>
                <p>Runs IEC 62386-102 random-address binary search for new ballasts without changing existing addresses.</p>
              </div>
              <button class="btn" onclick="sendCmd({action:'assign_addresses',mode:'new'})">Assign New</button>
            </div>
            <div class="action-item">
              <div>
                <strong>Replace Single Ballast</strong>
                <p>Assigns one unaddressed replacement ballast to an absent short address and restores its groups &amp; level.</p>
              </div>
              <div class="inline-controls">
                <input type="number" id="replace-addr" min="0" max="63" value="0" style="width:70px">
                <button class="btn" onclick="sendCmd({action:'replace_device',index:Number(document.getElementById('replace-addr').value)})">Replace</button>
              </div>
            </div>
            <div class="action-item">
              <div>
                <strong>Scan &amp; Address DALI-2 Input Devices (Part 103)</strong>
                <p>Inventories 24-bit multi-master sensors/buttons (301/303/304) and configures instance event filters.</p>
              </div>
              <div class="inline-controls">
                <button class="btn" onclick="sendCmd({action:'scan_inputs'})">Scan Inputs</button>
                <button class="btn" onclick="sendCmd({action:'assign_inputs',mode:'new'})">Assign New</button>
              </div>
            </div>
            <div class="action-item">
              <div>
                <strong>Synchronize Gateway Wall-Clock Time</strong>
                <p>Syncs browser UTC epoch to the gateway so persistent error logs and DALI debug traces record exact wall-clock timestamps.</p>
              </div>
              <button class="btn" onclick="syncGatewayTime()">Sync Browser Time</button>
            </div>
          </div>
        </div>
      </div>
    </section>

    <!-- PANEL 3: LOGS & DEBUG -->
    <section id="panel-logs" class="panel">
      <div class="card">
        <div class="card-header">
          <div>
            <h2>DALI Live Debug Session (256-Frame Circular Buffer)</h2>
            <p class="muted">Captures 16-bit gear commands/queries, 8-bit backward replies, 24-bit Part 103 events, and collisions with full protocol decoding.</p>
          </div>
          <div class="btn-group">
            <label class="field-check inline-check">
              <input type="checkbox" id="dbg-autotrigger" checked onchange="controlDebug('auto')">
              <span>Auto-trigger from DALI events/faults</span>
            </label>
            <button class="btn btn-primary btn-sm" onclick="controlDebug('start')">Start Capture</button>
            <button class="btn btn-sm" onclick="controlDebug('stop')">Pause</button>
            <button class="btn btn-sm" onclick="controlDebug('clear')">Clear</button>
            <button class="btn btn-sm" onclick="loadDebugTrace()">Refresh</button>
            <button class="btn btn-sm" onclick="exportJson('dali-trace.json', window.lastTraceData)">Export JSON</button>
          </div>
        </div>
        <div class="trace-stats" id="trace-stats">Session: ACTIVE • Total Frames: 0 • Collisions: 0 • Bus Errors: 0</div>
        <div class="table-wrap">
          <table class="data-table">
            <thead>
              <tr>
                <th>#</th>
                <th>Timestamp</th>
                <th>Uptime</th>
                <th>Bits</th>
                <th>Raw Frame</th>
                <th>Target</th>
                <th>Mnemonic</th>
                <th>Decoded Details &amp; Reply</th>
                <th>Status</th>
              </tr>
            </thead>
            <tbody id="trace-tbody"></tbody>
          </table>
        </div>
      </div>

      <div class="card">
        <div class="card-header">
          <div>
            <h2>Persistent KNX &amp; DALI Error Log (NVS Non-Volatile Ring Buffer)</h2>
            <p class="muted">Survives power failures and reboots. Automatically flushed to flash on Critical/Error events and NCN5130 SAVEB power warnings.</p>
          </div>
          <div class="btn-group">
            <button class="btn btn-sm" onclick="loadErrorLogs()">Refresh</button>
            <button class="btn btn-sm" onclick="clearErrorLogs()">Clear Log</button>
            <button class="btn btn-sm" onclick="exportJson('gateway-error-log.json', window.lastLogData)">Export JSON</button>
          </div>
        </div>
        <div class="table-wrap">
          <table class="data-table">
            <thead>
              <tr>
                <th>#</th>
                <th>Time (UTC / Local)</th>
                <th>Source</th>
                <th>Severity</th>
                <th>Error Code &amp; Title</th>
                <th>Target</th>
                <th>Count</th>
                <th>Detail &amp; Recommended Remedy</th>
              </tr>
            </thead>
            <tbody id="logs-tbody"></tbody>
          </table>
        </div>
      </div>

      <div class="card">
        <div class="card-header">
          <h2>Descriptive Error Code Reference Catalog</h2>
        </div>
        <div class="table-wrap">
          <table class="data-table">
            <thead>
              <tr>
                <th>Code</th>
                <th>Symbol</th>
                <th>Title</th>
                <th>Description</th>
                <th>Recommended Remedy</th>
              </tr>
            </thead>
            <tbody id="catalog-tbody"></tbody>
          </table>
        </div>
      </div>
    </section>

    <!-- PANEL 4: FIRMWARE UPDATE -->
    <section id="panel-firmware" class="panel">
      <div class="grid-two">
        <div class="card">
          <div class="card-header">
            <h2>Application Firmware Update (<code>firmware.bin</code>)</h2>
          </div>
          <p class="muted">
            Streams a compiled ESP32-C6 application binary (<code>.pio/build/release_c6/firmware.bin</code>) directly to the inactive OTA slot (<code>ota_0</code> / <code>ota_1</code>), verifies the ESP-IDF image header, and switches the boot partition.
          </p>
          <div class="sys-info-box" id="fw-sys-info">
            <div><strong>Running Version:</strong> <span id="info-fw-ver">--</span></div>
            <div><strong>Active Partition:</strong> <span id="info-part">--</span></div>
            <div><strong>Free / Min Heap:</strong> <span id="info-heap">--</span></div>
          </div>
          <div class="upload-box">
            <input type="file" id="file-firmware" accept=".bin">
            <button class="btn btn-primary" onclick="uploadOta('firmware')">Upload &amp; Flash Firmware (.bin)</button>
          </div>
          <div class="progress-wrap">
            <div class="progress-bar" id="prog-firmware" style="width:0%"></div>
          </div>
          <div class="upload-status" id="status-firmware">Select a valid ESP32-C6 firmware.bin file.</div>
        </div>

        <div class="card">
          <div class="card-header">
            <h2>Web UI / LittleFS Partition Update (<code>littlefs.bin</code>)</h2>
          </div>
          <p class="muted">
            Uploads a packed Web UI filesystem image (<code>gateway/build/littlefs.bin</code>) directly to the 2 MB <code>storage</code> flash partition (<code>0x420000</code>) and hot-reloads the HTML/CSS/JS assets without rebooting.
          </p>
          <div class="upload-box">
            <input type="file" id="file-littlefs" accept=".bin">
            <button class="btn btn-primary" onclick="uploadOta('littlefs')">Upload &amp; Flash LittleFS (.bin)</button>
          </div>
          <div class="progress-wrap">
            <div class="progress-bar" id="prog-littlefs" style="width:0%"></div>
          </div>
          <div class="upload-status" id="status-littlefs">Select a littlefs.bin filesystem image.</div>

          <hr class="sep">
          <div class="action-item">
            <div>
              <strong>Configuration-Preserving Device Reboot</strong>
              <p>Restarts the ESP32-C6 and TP-UART link while retaining all KNX associations, NVS target levels, and error logs.</p>
            </div>
            <button class="btn" onclick="rebootDevice()">Reboot Gateway</button>
          </div>
        </div>
      </div>
    </section>
  </main>

  <!-- BALLAST INSPECTOR MODAL -->
  <div class="modal-backdrop" id="gear-modal" style="display:none">
    <div class="modal">
      <div class="card-header">
        <h2 id="modal-title">Ballast Inspector — A00</h2>
        <button class="btn btn-sm" onclick="closeGearModal()">Close ✕</button>
      </div>
      <div class="modal-body" id="modal-body"></div>
    </div>
  </div>

  <script src="app.js"></script>
</body>
</html>
)WEBUI";
inline constexpr std::size_t index_html_size = sizeof(index_html_data) - 1;

inline constexpr const char app_css_data[] = R"WEBUI(:root {
  --bg: #0b0f17;
  --bg-card: #131a26;
  --bg-elevated: #1b2434;
  --border: #263248;
  --text: #e6edf3;
  --muted: #8b9bb4;
  --primary: #38bdf8;
  --primary-hover: #0ea5e9;
  --ok: #22c55e;
  --warn: #f59e0b;
  --err: #ef4444;
  --font: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
  --mono: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
}

* {
  box-sizing: border-box;
  margin: 0;
  padding: 0;
}

body {
  background: var(--bg);
  color: var(--text);
  font-family: var(--font);
  font-size: 14px;
  line-height: 1.45;
}

.topbar {
  display: flex;
  justify-content: space-between;
  align-items: center;
  flex-wrap: wrap;
  gap: 12px;
  padding: 14px 20px;
  background: var(--bg-card);
  border-bottom: 1px solid var(--border);
}

.brand {
  display: flex;
  align-items: center;
  gap: 12px;
}

.brand-badge {
  background: linear-gradient(135deg, #0284c7, #2563eb);
  color: #fff;
  font-weight: 700;
  font-size: 12px;
  padding: 6px 10px;
  border-radius: 6px;
  letter-spacing: 0.4px;
}

.brand h1 {
  font-size: 18px;
  font-weight: 700;
}

.subtitle {
  color: var(--muted);
  font-size: 12px;
}

.status-pills {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}

.pill {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  background: var(--bg-elevated);
  border: 1px solid var(--border);
  padding: 4px 10px;
  border-radius: 999px;
  font-size: 12px;
  font-family: var(--mono);
}

.pill .dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: var(--warn);
}

.pill.ok .dot {
  background: var(--ok);
}

.pill.err .dot {
  background: var(--err);
}

.tabbar {
  display: flex;
  gap: 4px;
  padding: 0 20px;
  background: var(--bg-card);
  border-bottom: 1px solid var(--border);
  overflow-x: auto;
}

.tab {
  background: transparent;
  color: var(--muted);
  border: none;
  border-bottom: 2px solid transparent;
  padding: 12px 18px;
  font-size: 14px;
  font-weight: 600;
  cursor: pointer;
}

.tab:hover {
  color: var(--text);
}

.tab.active {
  color: var(--primary);
  border-bottom-color: var(--primary);
}

.container {
  max-width: 1480px;
  margin: 0 auto;
  padding: 18px 20px 40px;
}

.panel {
  display: none;
  flex-direction: column;
  gap: 16px;
}

.panel.active {
  display: flex;
}

.grid-top,
.grid-two {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(420px, 1fr));
  gap: 16px;
}

.power-metrics-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
  gap: 12px;
}

.metric-box {
  background: var(--bg-elevated);
  border: 1px solid var(--border);
  border-radius: 8px;
  padding: 12px 14px;
  display: flex;
  flex-direction: column;
  gap: 3px;
}

.metric-label {
  font-size: 11px;
  text-transform: uppercase;
  letter-spacing: 0.4px;
  color: var(--muted);
}

.metric-value {
  font-size: 20px;
  font-weight: 700;
  font-family: var(--mono);
  color: var(--primary);
}

.metric-sub {
  font-size: 11px;
  color: var(--muted);
}

.cct-range {
  accent-color: #fbbf24;
}

.color-swatch {
  display: inline-block;
  width: 14px;
  height: 14px;
  border-radius: 4px;
  border: 1px solid rgba(255, 255, 255, 0.35);
  vertical-align: middle;
}

.card {
  background: var(--bg-card);
  border: 1px solid var(--border);
  border-radius: 10px;
  padding: 16px;
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  flex-wrap: wrap;
  gap: 10px;
  margin-bottom: 14px;
}

.card-header h2 {
  font-size: 15px;
  font-weight: 600;
}

.muted {
  color: var(--muted);
  font-size: 12.5px;
  margin-bottom: 10px;
}

.btn-group,
.filter-bar,
.inline-controls {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 6px;
}

.btn {
  background: var(--bg-elevated);
  color: var(--text);
  border: 1px solid var(--border);
  border-radius: 6px;
  padding: 7px 12px;
  font-size: 13px;
  font-weight: 500;
  cursor: pointer;
}

.btn:hover {
  border-color: var(--primary);
}

.btn.active,
.btn-primary {
  background: #0284c7;
  border-color: #38bdf8;
  color: #fff;
}

.btn-sm {
  padding: 5px 9px;
  font-size: 12px;
}

.front-panel-wrap {
  display: flex;
  align-items: center;
  gap: 22px;
  flex-wrap: wrap;
}

.oled-bezel {
  background: #05070b;
  border: 2px solid #2d3b55;
  border-radius: 8px;
  padding: 10px;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 6px;
}

#oled-canvas {
  width: 256px;
  height: 128px;
  image-rendering: pixelated;
  background: #020408;
}

.oled-caption {
  font-family: var(--mono);
  font-size: 11px;
  color: var(--muted);
}

.leds-column {
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.led-item {
  display: flex;
  align-items: center;
  gap: 10px;
  font-family: var(--mono);
  font-size: 12px;
}

.led-dot {
  width: 14px;
  height: 14px;
  border-radius: 50%;
  background: #002800;
  border: 1px solid #3b4b66;
  box-shadow: 0 0 8px rgba(0, 255, 0, 0.3);
}

.broadcast-controls {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.control-row {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
}

.control-row .label {
  min-width: 140px;
  color: var(--muted);
}

.control-row input[type="range"] {
  flex: 1;
  min-width: 120px;
}

.scenes-bar {
  display: grid;
  grid-template-columns: repeat(8, 1fr);
  gap: 6px;
}

.lamps-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(230px, 1fr));
  gap: 10px;
}

.lamp-card {
  background: var(--bg-elevated);
  border: 1px solid var(--border);
  border-radius: 8px;
  padding: 10px;
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.lamp-card.on {
  border-color: #0284c7;
  box-shadow: inset 0 0 0 1px rgba(56, 189, 248, 0.25);
}

.lamp-card.fault {
  border-color: var(--err);
}

.lamp-card.absent {
  opacity: 0.55;
}

.lamp-top {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.lamp-title {
  font-family: var(--mono);
  font-weight: 700;
  font-size: 13px;
}

.badges {
  display: flex;
  gap: 4px;
  flex-wrap: wrap;
}

.badge {
  font-size: 10.5px;
  font-family: var(--mono);
  padding: 2px 6px;
  border-radius: 4px;
  background: #243046;
  color: var(--text);
}

.badge.ok {
  background: rgba(34, 197, 94, 0.18);
  color: #4ade80;
}

.badge.warn {
  background: rgba(245, 158, 11, 0.2);
  color: #fbbf24;
}

.badge.err {
  background: rgba(239, 68, 68, 0.22);
  color: #f87171;
}

.lamp-slider-row {
  display: flex;
  align-items: center;
  gap: 8px;
}

.lamp-slider-row input[type="range"] {
  flex: 1;
}

.groups-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(190px, 1fr));
  gap: 8px;
}

.inputs-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(150px, 1fr));
  gap: 8px;
}

.field {
  display: flex;
  flex-direction: column;
  gap: 4px;
  margin-bottom: 10px;
  flex: 1;
}

.field span {
  font-size: 12px;
  color: var(--muted);
}

.field input,
.field select,
.inline-controls input {
  background: var(--bg-elevated);
  color: var(--text);
  border: 1px solid var(--border);
  border-radius: 6px;
  padding: 7px 10px;
  font-size: 13px;
}

.field-row {
  display: flex;
  gap: 10px;
  flex-wrap: wrap;
}

.field-check {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 8px;
  cursor: pointer;
}

.inline-check {
  margin-bottom: 0;
  margin-right: 8px;
}

.sep {
  border: none;
  border-top: 1px solid var(--border);
  margin: 14px 0;
}

h3 {
  font-size: 13.5px;
  margin-bottom: 10px;
  color: var(--primary);
}

.action-list {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.action-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 12px;
  padding: 10px;
  background: var(--bg-elevated);
  border: 1px solid var(--border);
  border-radius: 8px;
}

.action-item p {
  font-size: 12px;
  color: var(--muted);
}

.table-wrap {
  overflow-x: auto;
  max-height: 420px;
  overflow-y: auto;
  border: 1px solid var(--border);
  border-radius: 8px;
}

.data-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 12.5px;
}

.data-table th,
.data-table td {
  padding: 8px 10px;
  text-align: left;
  border-bottom: 1px solid var(--border);
}

.data-table th {
  background: var(--bg-elevated);
  color: var(--muted);
  position: sticky;
  top: 0;
}

.mono {
  font-family: var(--mono);
}

.trace-stats {
  font-family: var(--mono);
  font-size: 12px;
  color: var(--primary);
  margin-bottom: 8px;
}

.sys-info-box {
  background: var(--bg-elevated);
  border: 1px solid var(--border);
  border-radius: 8px;
  padding: 10px 12px;
  margin-bottom: 12px;
  display: flex;
  flex-direction: column;
  gap: 4px;
  font-family: var(--mono);
  font-size: 12.5px;
}

.upload-box {
  display: flex;
  gap: 10px;
  align-items: center;
  flex-wrap: wrap;
  margin-bottom: 10px;
}

.progress-wrap {
  width: 100%;
  height: 10px;
  background: var(--bg-elevated);
  border-radius: 999px;
  overflow: hidden;
  margin-bottom: 8px;
}

.progress-bar {
  height: 100%;
  background: linear-gradient(90deg, #0284c7, #22c55e);
  transition: width 0.15s ease;
}

.upload-status {
  font-family: var(--mono);
  font-size: 12px;
  color: var(--muted);
}

.modal-backdrop {
  position: fixed;
  inset: 0;
  background: rgba(4, 7, 14, 0.78);
  display: flex;
  justify-content: center;
  align-items: center;
  z-index: 100;
  padding: 16px;
}

.modal {
  background: var(--bg-card);
  border: 1px solid var(--border);
  border-radius: 10px;
  padding: 18px;
  max-width: 680px;
  width: 100%;
  max-height: 90vh;
  overflow-y: auto;
}
)WEBUI";
inline constexpr std::size_t app_css_size = sizeof(app_css_data) - 1;

inline constexpr const char app_js_data[] = R"WEBUI("use strict";

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
    `OLED ${w}×${h} • Page ${(oled.page || 0) + 1}/3`;
}

// IEC 62386-102 Table 15 & 16 human-readable Fade Time / Fade Rate tables
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

// IEC 62386-102 §9.1 logarithmic dimming curve: X(n) = 10^((n - 254) / (253/3))
function daliArcToPowerRatio(arc) {
  if (!arc || arc <= 0) return 0.0;
  if (arc >= 254) return 1.0;
  return Math.pow(10, (arc - 254) / (253 / 3));
}

function rgbToHex(r, g, b) {
  const to2 = (v) => Math.max(0, Math.min(255, Number(v) || 0)).toString(16).padStart(2, "0");
  return "#" + to2(r) + to2(g) + to2(b);
}

function sendHexRgb(target, index, hex) {
  const clean = (hex || "#ffffff").replace("#", "");
  const r = parseInt(clean.slice(0, 2), 16) || 0;
  const g = parseInt(clean.slice(2, 4), 16) || 0;
  const b = parseInt(clean.slice(4, 6), 16) || 0;
  sendCmd({ action: "set_rgb", target, index, r, g, b });
}

function formatGroupList(mask) {
  if (!mask) return "No groups";
  const list = [];
  for (let g = 0; g < 16; g++) {
    if (mask & (1 << g)) list.push("G" + String(g).padStart(2, "0"));
  }
  return list.join(", ");
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
  const nomWatts = Number(document.getElementById("nominal-watts")?.value || 15);
  channels.forEach((ch, idx) => {
    const [present, faulty, level, source, dt8, kelvin, r, g, b, statusByte, groupsMask, devType] = ch;
    if (lampFilter === "present" && !present) return;
    if (lampFilter === "on" && level === 0) return;
    if (lampFilter === "fault" && !faulty && (statusByte & 0x03) === 0) return;

    const pct = Math.round((level * 100) / 254);
    const liveWatts = present
      ? (level > 0 ? nomWatts * daliArcToPowerRatio(level) + 0.4 : 0.4).toFixed(1)
      : "0.0";
    const card = document.createElement("div");
    card.className =
      "lamp-card" + (level > 0 ? " on" : "") + (faulty ? " fault" : "") + (!present ? " absent" : "");

    const statusFlags = decodeStatusBits(statusByte);
    const addrStr = "A" + String(idx).padStart(2, "0");

    // Detailed DT6 & DT8 capability badges
    const dtBadges = [];
    if (devType === 6 || !dt8) dtBadges.push('<span class="badge">DT6 Dim</span>');
    if (dt8 & 1) dtBadges.push('<span class="badge">DT8 Tc</span>');
    if (dt8 & 4) dtBadges.push('<span class="badge">DT8 RGBW</span>');
    else if (dt8 & 2) dtBadges.push('<span class="badge">DT8 RGB</span>');
    if (dt8 & 8) dtBadges.push('<span class="badge">DT8 XY</span>');

    const hexColor = (r || g || b) ? rgbToHex(r, g, b) : "#ffd28a";

    let colourControls = "";
    if (dt8 & 1) {
      const kVal = kelvin || 4000;
      const mirek = Math.round(1000000 / kVal);
      colourControls += `
        <div class="lamp-slider-row">
          <span class="mono" style="font-size:11px;min-width:28px">CCT</span>
          <input type="range" class="cct-range" min="2000" max="6500" step="100" value="${kVal}"
            onchange="sendCmd({action:'set_cct',target:'ch',index:${idx},kelvin:Number(this.value)})">
          <span class="mono" style="font-size:11px;min-width:74px;text-align:right">${kVal}K (${mirek}M)</span>
        </div>`;
    }
    if (dt8 & 6) {
      colourControls += `
        <div class="lamp-slider-row">
          <span class="mono" style="font-size:11px;min-width:28px">RGB</span>
          <input type="color" value="${hexColor}" onchange="sendHexRgb('ch',${idx},this.value)">
          <span class="mono" style="font-size:11px;color:#8b9bb4">${r || 0},${g || 0},${b || 0}</span>
          ${
            dt8 & 4
              ? `<span class="mono" style="font-size:11px;margin-left:auto">W:</span>
                 <input type="range" min="0" max="254" value="0" style="max-width:70px"
                   onchange="sendCmd({action:'set_rgbw',target:'ch',index:${idx},r:${r || 0},g:${g || 0},b:${b || 0},w:Number(this.value)})">`
              : ""
          }
        </div>`;
    }
    if (dt8 & 8) {
      colourControls += `
        <div class="lamp-slider-row">
          <span class="mono" style="font-size:11px">CIE xy:</span>
          <button class="btn btn-sm" onclick="sendCmd({action:'set_xy',target:'ch',index:${idx},x:20480,y:21500,brightness:${level || 200}})">D65 White</button>
          <button class="btn btn-sm" onclick="sendCmd({action:'set_xy',target:'ch',index:${idx},x:29000,y:26500,brightness:${level || 200}})">Warm 2700K</button>
          <button class="btn btn-sm" onclick="openGearModal(${idx})">Custom xy…</button>
        </div>`;
    }

    card.innerHTML = `
      <div class="lamp-top">
        <span class="lamp-title">${addrStr} <span class="color-swatch" style="background:${level > 0 ? hexColor : "#1f2937"}"></span></span>
        <div class="badges">
          <span class="badge ${present ? "ok" : ""}">${present ? "PRESENT" : "ABSENT"}</span>
          ${dtBadges.join("")}
          <span class="badge ${source === "readback" ? "ok" : "warn"}">${source}</span>
          ${faulty ? '<span class="badge err">FAULT</span>' : ""}
        </div>
      </div>
      <div class="lamp-slider-row">
        <button class="btn btn-sm" onclick="sendCmd({action:'${level > 0 ? "switch_off" : "switch_on"}',target:'ch',index:${idx}})">${level > 0 ? "OFF" : "ON"}</button>
        <input type="range" min="0" max="254" value="${level}"
          onchange="sendCmd({action:'set_level',target:'ch',index:${idx},level:Number(this.value)})">
        <span class="mono" style="min-width:74px;text-align:right">${pct}% • ${liveWatts}W</span>
      </div>
      ${colourControls}
      <div class="lamp-top" style="margin-top:2px">
        <span class="mono" style="font-size:11px;color:#8b9bb4">
          ARC:${level}/254 • ${formatGroupList(groupsMask)}
          ${statusFlags.length ? " • " + statusFlags.join(",") : ""}
        </span>
        <div class="btn-group">
          <button class="btn btn-sm" title="Lamp Finder: Blink ON/OFF (30s, restores previous level)" onclick="sendCmd({action:'find_lamp',target:'ch',index:${idx},mode:'blink'})">Blink</button>
          <button class="btn btn-sm" title="Lamp Finder: Smooth Breathing Pulse (30s, restores previous level)" onclick="sendCmd({action:'find_lamp',target:'ch',index:${idx},mode:'pulse'})">Pulse</button>
          <button class="btn btn-sm" title="Ballast Inspector, DT6/DT8 Registers &amp; Parameter Tuning" onclick="openGearModal(${idx})">Inspect / Tune</button>
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
        <div class="btn-group">
          <button class="btn btn-sm" title="Blink Group" onclick="sendCmd({action:'find_lamp',target:'grp',index:${idx},mode:'blink'})">Blink</button>
          <span class="badge">${pct}% (${lvl})</span>
        </div>
      </div>
      <div class="lamp-slider-row">
        <button class="btn btn-sm" onclick="sendCmd({action:'switch_on',target:'grp',index:${idx}})">ON</button>
        <button class="btn btn-sm" onclick="sendCmd({action:'switch_off',target:'grp',index:${idx}})">OFF</button>
        <input type="range" min="0" max="254" value="${lvl}"
          onchange="sendCmd({action:'set_level',target:'grp',index:${idx},level:Number(this.value)})">
        <input type="color" value="#ffd28a" title="Group DT8 RGB Colour" onchange="sendHexRgb('grp',${idx},this.value)">
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

function updatePowerMonitor(data) {
  const d = data.dali || {};
  const channels = data.channels || [];
  const nomW = Number(document.getElementById("nominal-watts")?.value || 15);

  const psuEl = document.getElementById("pwr-psu-state");
  const psuSub = document.getElementById("pwr-psu-sub");
  if (d.psu_short) {
    psuEl.textContent = "SHORT TRIPPED!";
    psuEl.style.color = "var(--err)";
    psuSub.textContent = "REM3 latched OFF (>80 ms low) — fix wiring & Re-Arm";
  } else if (d.psu_on) {
    psuEl.textContent = "16.0 V ACTIVE";
    psuEl.style.color = "var(--ok)";
    psuSub.textContent = "Internal REM3-2415S/A/CTRL supply armed";
  } else {
    psuEl.textContent = "EXTERNAL PSU";
    psuEl.style.color = "var(--primary)";
    psuSub.textContent = "Internal REM3 disabled via GPIO1";
  }

  // IEC 62386-101 bus current: 2 mA per control gear + 5 mA per multi-master input device
  const gearCnt = d.present || 0;
  const inpCnt = d.inputs || 0;
  const busMa = gearCnt * 2 + inpCnt * 5;
  const busPct = Math.min(100, Math.round((busMa * 100) / 250));
  document.getElementById("pwr-bus-ma").textContent = `${busMa} mA / 250 mA (${busPct}%)`;
  document.getElementById("pwr-bus-bar").style.width = `${busPct}%`;
  document.getElementById("pwr-bus-sub").textContent =
    `${gearCnt} gear (2 mA ea) + ${inpCnt} input devs (5 mA ea)`;

  let activeW = 0;
  let standbyW = 0;
  channels.forEach((ch) => {
    const [present, , level] = ch;
    if (!present) return;
    standbyW += 0.4;
    if (level > 0) {
      activeW += nomW * daliArcToPowerRatio(level);
    }
  });
  const totalW = activeW + standbyW;
  const dailyKwh = (totalW * 24) / 1000;
  document.getElementById("pwr-active-w").textContent = `${totalW.toFixed(1)} W`;
  document.getElementById("pwr-standby-w").textContent =
    `Lamp output: ${activeW.toFixed(1)} W • Driver standby: ${standbyW.toFixed(1)} W`;
  document.getElementById("pwr-daily-kwh").textContent = `${dailyKwh.toFixed(2)} kWh/d`;
  document.getElementById("pwr-bus-health").textContent =
    `TX: ${d.tx_frames || 0} frames • Collisions: ${d.collisions || 0}`;
}

function handleLiveState(data) {
  latestState = data;
  const chipId = data.chip_id || "f5aa11f";
  const apPreview = document.getElementById("cfg-ap-ssid-preview");
  if (apPreview) apPreview.textContent = `KNX-DALI-SETUP-${chipId}`;
  const chipPreview = document.getElementById("cfg-chip-id-preview");
  if (chipPreview) chipPreview.textContent = chipId;

  const knxPill = document.getElementById("pill-knx");
  knxPill.textContent = `KNX: ${formatIa(data.knx.ia)} ${data.knx.prog_mode ? "[PROG]" : data.knx.bus_ok ? "OK" : "SAVEB!"}`;
  knxPill.className = "pill " + (data.knx.bus_ok ? "ok" : "err");

  const daliPill = document.getElementById("pill-dali");
  daliPill.textContent = `DALI: ${data.dali.present}/64 ON:${data.dali.active} FLT:${data.dali.faulty}`;
  daliPill.className = "pill " + (data.dali.faulty > 0 || data.dali.psu_short ? "err" : "ok");

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

  updatePowerMonitor(data);
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

  const ftOptions = FADE_TIME_LABELS.map(
    (lbl, i) => `<option value="${i}" ${d.fade_time === i ? "selected" : ""}>${lbl}</option>`
  ).join("");
  const frOptions = FADE_RATE_LABELS.slice(1)
    .map(
      (lbl, idx) =>
        `<option value="${idx + 1}" ${d.fade_rate === idx + 1 ? "selected" : ""}>${lbl}</option>`
    )
    .join("");
  const xyX = ((d.xy_x || 20480) / 65535).toFixed(4);
  const xyY = ((d.xy_y || 21500) / 65535).toFixed(4);

  body.innerHTML = `
    <div class="sys-info-box">
      <div style="display:flex;flex-wrap:wrap;gap:6px;margin-bottom:8px">
        <span class="badge ${d.gear_failure ? "err" : "ok"}">Driver: ${d.gear_failure ? "FAULT" : "OK"}</span>
        <span class="badge ${d.lamp_failure ? "err" : "ok"}">Lamp: ${d.lamp_failure ? "FAILURE" : "OK"}</span>
        <span class="badge ${d.arc_on ? "ok" : ""}">Arc: ${d.arc_on ? "ON" : "OFF"}</span>
        <span class="badge ${d.limit_error ? "warn" : ""}">Limit: ${d.limit_error ? "OUT OF RANGE" : "OK"}</span>
        <span class="badge">DT${d.device_type} (v${d.version})</span>
        <span class="badge">Phys Min: ${d.physical_min} (${(daliArcToPowerRatio(d.physical_min) * 100).toFixed(2)}%)</span>
      </div>
      <div><strong>Group Memberships (G00..G15):</strong> ${formatGroupList(d.groups)}</div>
      <div><strong>Scene Levels (S01..S16):</strong> ${(d.scenes || []).map((v, i) => `S${String(i + 1).padStart(2, "0")}:${v === 255 ? "MASK" : Math.round((v * 100) / 254) + "%"}`).join(" • ")}</div>
    </div>
    <h3 style="margin:10px 0 6px;font-size:13px">DT6 Operating Limits &amp; Fade Curve (IEC 62386-102)</h3>
    <div class="field-row">
      <div class="field"><span>Min Level (1..254, PhysMin=${d.physical_min})</span><input type="number" id="gm-min" min="1" max="254" value="${d.min_level}"></div>
      <div class="field"><span>Max Level (1..254)</span><input type="number" id="gm-max" min="1" max="254" value="${d.max_level}"></div>
      <div class="field"><span>Power-On Level (0..254)</span><input type="number" id="gm-pon" min="0" max="254" value="${d.power_on_level}"></div>
    </div>
    <div class="field-row">
      <div class="field"><span>System-Failure Level (0..254)</span><input type="number" id="gm-fail" min="0" max="254" value="${d.system_failure_level}"></div>
      <div class="field"><span>Fade Time (IEC 62386 Table 15)</span><select id="gm-ft">${ftOptions}</select></div>
      <div class="field"><span>Fade Rate (IEC 62386 Table 16)</span><select id="gm-fr">${frOptions}</select></div>
    </div>
    <h3 style="margin:10px 0 6px;font-size:13px">DT8 Colour Control (IEC 62386-209: Tc / RGBW / CIE 1931 xy)</h3>
    <div class="field-row">
      <div class="field">
        <span>Tunable White CCT (K)</span>
        <input type="number" min="1800" max="8000" step="100" value="${d.kelvin || 4000}"
          onchange="sendCmd({action:'set_cct',target:'ch',index:${ch},kelvin:Number(this.value)})">
      </div>
      <div class="field">
        <span>CIE 1931 x (0.00..0.80)</span>
        <input type="number" id="gm-xy-x" min="0" max="0.8" step="0.005" value="${xyX}">
      </div>
      <div class="field">
        <span>CIE 1931 y (0.00..0.80)</span>
        <input type="number" id="gm-xy-y" min="0" max="0.8" step="0.005" value="${xyY}">
      </div>
      <div class="field" style="justify-content:flex-end">
        <button class="btn" type="button" onclick="sendCmd({action:'set_xy',target:'ch',index:${ch},x:Math.round(Number(document.getElementById('gm-xy-x').value)*65535),y:Math.round(Number(document.getElementById('gm-xy-y').value)*65535),brightness:${d.level || 200}})">Apply CIE xy</button>
      </div>
    </div>
    <div class="btn-group" style="margin-top:10px">
      <button class="btn btn-primary" onclick="saveGearParams(${ch})">Write DT6 Parameters to Ballast (DTR0 + 2x Store)</button>
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
)WEBUI";
inline constexpr std::size_t app_js_size = sizeof(app_js_data) - 1;

}
// clang-format on
