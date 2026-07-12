// Register Service Worker
if ('serviceWorker' in navigator) {
    window.addEventListener('load', () => {
        navigator.serviceWorker.register('/service-worker.js')
            .then(() => console.log('SW registered'))
            .catch((err) => console.log('SW failed', err));
    });
}

//=============================================================================
// State
//=============================================================================
const state = {
    connected: false,
    websocket: null,
    reconnectTimer: null,
    lastMsgAt: 0,
    sampleTimes: {},       // ECU name -> last device millis (for update-period calc)
    sampleIntervals: {},   // ECU name -> EMA of ms between updates
    recLast: {},           // ECU name -> last recorded value (for forward-fill)
    recording: false,
    recordingStart: null,
    recordedData: [],
    plotData: {},
    plotMaxPoints: 100
};

// The app is served from the scanner, so talk to whatever host served it
// (falls back to the SoftAP address for laptop dev).
const HOST = (location.hostname && !['localhost', '127.0.0.1', ''].includes(location.hostname))
    ? location.hostname : '192.168.4.1';
const apiBase = `http://${HOST}`;
const wsUrl = `ws://${HOST}:81/`;

// Display settings (stored in localStorage)
const settings = {
    updateRate: parseInt(localStorage.getItem('obd1_rate')) || 100,
    units: localStorage.getItem('obd1_units') || 'imperial'
};

// Gauge configurations
const gaugeConfigs = {
    // Vehicle gauges
    'v-rpm': { min: 0, max: 7000 },
    'v-speed': { min: 0, max: 120 },
    'v-coolant': { min: 0, max: 250 },
    'v-battery': { min: 10, max: 15 },
    'v-iat': { min: 0, max: 200 },
    'v-fuel': { min: 0, max: 100 },
    // Diagnostic gauges
    'd-throttle': { min: 0, max: 100 },
    'd-timing': { min: 0, max: 50 },
    'd-o2': { min: 0, max: 1 },
    'd-map': { min: 0, max: 105 },
    'd-fueltrim': { min: -25, max: 25 },
    'd-injector': { min: 0, max: 20 }
};

// Firmware reports metric. These gauges can be shown in imperial (converted here).
const unitVariants = {
    'v-speed':   { metric:   { unit: 'km/h', min: 0, max: 200 },
                   imperial: { unit: 'mph',  min: 0, max: 120, conv: v => v * 0.621371 } },
    'v-coolant': { metric:   { unit: '°C',   min: -20, max: 130 },
                   imperial: { unit: '°F',   min: -4,  max: 266, conv: v => v * 9 / 5 + 32 } },
};

// Resolve a gauge's {min,max,unit,conv} for the current unit setting.
function resolveConfig(id) {
    const variant = unitVariants[id];
    if (variant) return variant[settings.units] || variant.metric;
    return gaugeConfigs[id] || null;
}

// Per data-page: which ECU params the page shows and the gauge each drives.
// (Only params that exist on the active ECU are listed.)
const pageParams = {
    vehicle: [
        { name: 'EngineSpeed',    gauge: 'v-rpm' },
        { name: 'VehicleSpeed',   gauge: 'v-speed' },
        { name: 'CoolantTemp',    gauge: 'v-coolant' },
        { name: 'BatteryVoltage', gauge: 'v-battery' },
    ],
    diagnostic: [
        { name: 'ThrottlePosition',   gauge: 'd-throttle' },
        { name: 'IgnitionAdvance',    gauge: 'd-timing' },
        { name: 'O2Average',          gauge: 'd-o2' },
        { name: 'AFCorrection',       gauge: 'd-fueltrim' },
        { name: 'InjectorPulseWidth', gauge: 'd-injector' },
    ],
};

// Derived: ECU name <-> gauge id.
const ecuParamToGauge = {};
const gaugeToParam = {};
Object.values(pageParams).flat().forEach(p => {
    ecuParamToGauge[p.name] = p.gauge;
    gaugeToParam[p.gauge] = p.name;
});

// Params the user toggled OFF (greyed tiles), for this session.
const disabledParams = new Set();
let currentPage = 'home';

// Param catalog metadata (name -> {unit}), filled from /params.
let paramMeta = {};
let readMsEstimate = 85;   // per-param read time (ms), from /params

// Custom View: chosen param names (persisted) and their live gauge elements.
const customSelected = new Set(JSON.parse(localStorage.getItem('obd1_custom') || '[]'));
let customGaugeEls = {};

// Data Recorder: which params to record + their integer poll factors.
let recorderRecordSet = new Set();
let recorderFactorsInt = {};
let recorderColumns = [];   // ordered column names for the CSV
let recordMeta = {};        // header metadata captured at record start
let ecuInfo = { ecu: '', name: '' };  // from /params, for the CSV header
const APP_VERSION = 'v2.0';

function fmtVal(v) {
    if (typeof v !== 'number') return v ?? '—';
    return Number.isInteger(v) ? String(v) : v.toFixed(1);
}

// CSV-safe unit suffix: "%" -> "percent", "V" -> "v", "km/h" -> "kmh", etc.
function unitSlug(u) {
    if (!u) return '';
    return u.replace(/%/g, 'percent').replace(/[^a-zA-Z0-9]/g, '').toLowerCase();
}

//=============================================================================
// Navigation
//=============================================================================
function navigateTo(pageId) {
    document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
    document.getElementById(`page-${pageId}`).classList.add('active');
    currentPage = pageId;
    resetPeriodStats();
    if (pageId === 'recorder') loadRecorder();
    if (pageId === 'custom') loadCustom();
    if (pageId === 'calibration') loadCalibration();
    applyPolling();
    updateFooter();
}

// Menu card clicks
document.querySelectorAll('.menu-card').forEach(card => {
    card.addEventListener('click', () => navigateTo(card.dataset.page));
});

// Back buttons
document.querySelectorAll('.back-btn').forEach(btn => {
    btn.addEventListener('click', () => navigateTo(btn.dataset.page));
});

// View toggle (gauges/plots)
document.querySelectorAll('.view-toggle').forEach(toggle => {
    toggle.querySelectorAll('.toggle-btn').forEach(btn => {
        btn.addEventListener('click', () => {
            const parent = btn.closest('.data-main');
            parent.querySelectorAll('.toggle-btn').forEach(b => b.classList.remove('active'));
            btn.classList.add('active');
            
            const view = btn.dataset.view;
            parent.querySelector('.gauges-view').classList.toggle('active', view === 'gauges');
            parent.querySelector('.plots-view').classList.toggle('active', view === 'plots');
        });
    });
});

//=============================================================================
// Active polling set (only poll what the current page needs)
//=============================================================================
// Returns [{name, factor}] the scanner should poll right now.
function activePollList() {
    if (currentPage === 'vehicle' || currentPage === 'diagnostic') {
        return (pageParams[currentPage] || [])
            .filter(p => !disabledParams.has(p.name))
            .map(p => ({ name: p.name, factor: 1 }));
    }
    if (currentPage === 'custom') {
        return [...customSelected].map(name => ({ name, factor: 1 }));
    }
    if (currentPage === 'recorder' && state.recording) {
        return [...recorderRecordSet].map(name => ({ name, factor: recorderFactorsInt[name] || 1 }));
    }
    return [];
}

// Names the current page is actively showing (for the footer estimate).
function pageActiveParams() {
    if (currentPage === 'vehicle' || currentPage === 'diagnostic')
        return (pageParams[currentPage] || []).filter(p => !disabledParams.has(p.name)).map(p => p.name);
    if (currentPage === 'custom') return [...customSelected];
    return [];
}

async function applyPolling() {
    try {
        await fetch(`${apiBase}/poll`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ params: activePollList() })
        });
    } catch (e) { /* offline; reasserted on reconnect/nav */ }
}

// Tap a gauge tile to enable/disable that parameter (grey + stop polling it).
document.querySelectorAll('.gauge-card').forEach(card => {
    const valueEl = card.querySelector('.gauge-value');
    const param = valueEl ? gaugeToParam[valueEl.id] : null;
    if (!param) { card.classList.add('gauge-na'); return; }  // no backing ECU param
    card.classList.add('gauge-toggleable');
    card.addEventListener('click', () => {
        if (disabledParams.has(param)) {
            disabledParams.delete(param);
            card.classList.remove('gauge-off');
        } else {
            disabledParams.add(param);
            card.classList.add('gauge-off');
        }
        resetPeriodStats();
        applyPolling();
        updateFooter();
    });
});

//=============================================================================
// Estimated update period (measured from real samples)
//=============================================================================
function resetPeriodStats() {
    state.sampleTimes = {};
    state.sampleIntervals = {};
}

function updateFooter() {
    const footer = document.getElementById(`footer-${currentPage}`);
    if (!footer) return;
    const active = pageActiveParams();
    if (active.length === 0) { footer.textContent = 'No parameters selected'; return; }
    const vals = active.map(n => state.sampleIntervals[n]).filter(v => typeof v === 'number');
    if (vals.length === 0) { footer.textContent = `Estimated sample period: measuring… (${active.length} params)`; return; }
    // Full-set loop = time to refresh every selected param once = the slowest one.
    const periodMs = Math.round(Math.max(...vals));
    const hz = periodMs > 0 ? (1000 / periodMs).toFixed(1) : '—';
    footer.textContent = `Estimated sample period: ~${periodMs} ms (${hz} Hz) · ${active.length} params`;
}

//=============================================================================
// Comms heartbeat
//=============================================================================
// Just note that traffic arrived; the interval below decides healthy vs stale.
// (A steady gentle pulse shows health — it does NOT flash per message.)
function pulseHeartbeat() {
    state.lastMsgAt = performance.now();
}

// Beacon = "module reachable", not "data flowing". Green whenever the socket is
// open (even on idle pages that poll nothing); red when the link is down.
function wsOpen() {
    return state.websocket && state.websocket.readyState === WebSocket.OPEN;
}

setInterval(() => {
    const open = wsOpen();
    document.querySelectorAll('.heartbeat').forEach(h => {
        h.classList.toggle('alive', open);
        h.classList.toggle('stale', !open);
    });
}, 500);

// Periodic reachability ping so an open-but-dead (half-open) socket is caught and
// recycled — keeps the beacon honest even when no data is being polled.
setInterval(async () => {
    if (!wsOpen()) return;
    try {
        await fetch(`${apiBase}/data`, { cache: 'no-store', signal: AbortSignal.timeout(2500) });
    } catch (e) {
        try { state.websocket.close(); } catch (_) {}  // force reconnect; beacon goes red
    }
}, 3000);

//=============================================================================
// Gauge Updates
//=============================================================================
// `value` is the engineering value from firmware (metric); convert per unit choice.
function updateGauge(id, value) {
    const config = resolveConfig(id);
    if (!config) return;
    if (config.conv) value = config.conv(value);

    const percentage = Math.min(100, Math.max(0, ((value - config.min) / (config.max - config.min)) * 100));

    const valueEl = document.getElementById(id);
    const barEl = document.getElementById(`${id}-bar`);

    if (valueEl) valueEl.textContent = fmtVal(value);
    if (barEl) barEl.style.width = `${percentage}%`;

    // Keep the gauge's unit label in sync with the current unit setting.
    if (config.unit && valueEl) {
        const unitEl = valueEl.parentElement && valueEl.parentElement.querySelector('.gauge-unit');
        if (unitEl) unitEl.textContent = config.unit;
    }

    // Store (converted) value for plotting.
    if (!state.plotData[id]) state.plotData[id] = [];
    state.plotData[id].push(value);
    if (state.plotData[id].length > state.plotMaxPoints) state.plotData[id].shift();
}

//=============================================================================
// Plot Drawing
//=============================================================================
function drawPlot(canvasId, dataId) {
    const canvas = document.getElementById(canvasId);
    if (!canvas || !canvas.getContext) return;
    
    const ctx = canvas.getContext('2d');
    const data = state.plotData[dataId] || [];
    const config = resolveConfig(dataId);
    if (!config) return;
    
    // Set canvas size
    const rect = canvas.getBoundingClientRect();
    canvas.width = rect.width * 2;
    canvas.height = rect.height * 2;
    ctx.scale(2, 2);
    
    const w = rect.width;
    const h = rect.height;
    
    // Clear
    ctx.fillStyle = '#0d0d1a';
    ctx.fillRect(0, 0, w, h);
    
    if (data.length < 2) return;
    
    // Draw grid
    ctx.strokeStyle = '#1a1a35';
    ctx.lineWidth = 1;
    for (let i = 0; i < 4; i++) {
        const y = (h / 4) * i;
        ctx.beginPath();
        ctx.moveTo(0, y);
        ctx.lineTo(w, y);
        ctx.stroke();
    }
    
    // Draw line
    ctx.strokeStyle = '#00d9ff';
    ctx.lineWidth = 2;
    ctx.beginPath();
    
    const xStep = w / (state.plotMaxPoints - 1);
    const range = config.max - config.min;
    
    for (let i = 0; i < data.length; i++) {
        const x = i * xStep;
        const y = h - ((data[i] - config.min) / range) * h;
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
    }
    ctx.stroke();
}

function updatePlots() {
    // Vehicle plots
    drawPlot('plot-v-rpm', 'v-rpm');
    drawPlot('plot-v-speed', 'v-speed');
    drawPlot('plot-v-coolant', 'v-coolant');
    drawPlot('plot-v-battery', 'v-battery');
    // Diagnostic plots
    drawPlot('plot-d-throttle', 'd-throttle');
    drawPlot('plot-d-timing', 'd-timing');
    drawPlot('plot-d-o2', 'd-o2');
    drawPlot('plot-d-map', 'd-map');
}

// NOTE: The app no longer generates any simulated/random data. Live values come
// only from the scanner over the WebSocket. In firmware SIMULATE_ECU builds the
// device replays real captured frames from resources/sample_data.txt.

//=============================================================================
// Data Recorder
//=============================================================================
const recorderIndicator = document.getElementById('recorderIndicator');
const recorderTime = document.getElementById('recorderTime');
const recorderSamples = document.getElementById('recorderSamples');
const btnRecord = document.getElementById('btnRecord');
const btnExport = document.getElementById('btnExport');

let recorderTimer = null;

function formatTime(ms) {
    const secs = Math.floor(ms / 1000);
    const mins = Math.floor(secs / 60);
    const hrs = Math.floor(mins / 60);
    return `${String(hrs).padStart(2, '0')}:${String(mins % 60).padStart(2, '0')}:${String(secs % 60).padStart(2, '0')}`;
}

function updateRecorderDisplay() {
    if (state.recording) {
        recorderTime.textContent = formatTime(Date.now() - state.recordingStart);
        recorderSamples.textContent = `${state.recordedData.length} samples`;
    }
}

// List all params with a record checkbox + sample factor.
// Defaults: factor 1, except temperatures & pressures which default to 0.5.
async function loadRecorder() {
    const statusEl = document.getElementById('recStatus');
    const body = document.getElementById('recTableBody');
    statusEl.textContent = 'Loading…';
    body.innerHTML = '';
    try {
        const data = await fetchParams();
        const params = data.params || [];
        statusEl.textContent = `${params.length} parameters`;
        for (const p of params) {
            const slow = /Temp|Pressure/i.test(p.name);
            const tr = document.createElement('tr');
            tr.dataset.name = p.name;
            tr.className = 'rec-row';   // selected by default; tap to grey out

            const tdName = document.createElement('td'); tdName.textContent = p.name;
            const tdUnit = document.createElement('td'); tdUnit.textContent = p.unit || '';

            const tdFac = document.createElement('td');
            const fac = document.createElement('input');
            fac.type = 'number'; fac.step = '0.5'; fac.min = '0.5'; fac.className = 'rec-factor';
            fac.value = slow ? 0.5 : 1;
            fac.addEventListener('click', e => e.stopPropagation());   // don't toggle row
            fac.addEventListener('change', updateRecorderEstimate);
            tdFac.appendChild(fac);

            tr.append(tdName, tdUnit, tdFac);
            tr.addEventListener('click', () => {
                tr.classList.toggle('rec-off');
                updateRecorderEstimate();
            });
            body.appendChild(tr);
        }
        updateRecorderEstimate();
    } catch (e) {
        statusEl.textContent = `Failed to load /params: ${e.message}`;
    }
}

// Estimate the full-set sample period from the selected rows and per-read time.
function updateRecorderEstimate() {
    const el = document.getElementById('footer-recorder');
    if (!el) return;
    const rows = [...document.querySelectorAll('#recTableBody tr')].filter(tr => !tr.classList.contains('rec-off'));
    if (rows.length === 0) { el.textContent = 'No parameters selected'; return; }
    const factors = rows.map(tr => Math.max(0.5, numOr(tr.querySelector('.rec-factor').value, 1)));
    const minF = Math.min(...factors);
    // Reads per full loop = sum of integer-normalized factors; loop time = reads × per-read.
    const reads = factors.reduce((sum, f) => sum + Math.max(1, Math.round(f / minF)), 0);
    const loopMs = reads * readMsEstimate;
    const hz = loopMs > 0 ? (1000 / loopMs).toFixed(1) : '—';
    el.textContent = `Estimated sample period: ~${loopMs} ms (${hz} Hz) · ${rows.length} params`;
}

// Turn the selected (non-greyed) rows + fractional factors into an integer-weighted
// poll set (e.g. factors {1, 0.5} -> {2, 1}, so factor-1 params poll twice as often).
function buildRecorderSelection() {
    const rows = [...document.querySelectorAll('#recTableBody tr')]
        .filter(tr => !tr.classList.contains('rec-off'))
        .map(tr => ({ name: tr.dataset.name, factor: Math.max(0.5, numOr(tr.querySelector('.rec-factor').value, 1)) }));
    recorderRecordSet = new Set(rows.map(r => r.name));
    recorderColumns = rows.map(r => r.name);   // CSV column order
    recorderFactorsInt = {};
    if (rows.length) {
        const minF = Math.min(...rows.map(r => r.factor));
        rows.forEach(r => { recorderFactorsInt[r.name] = Math.max(1, Math.round(r.factor / minF)); });
    }
}

btnRecord.addEventListener('click', () => {
    if (state.recording) {
        state.recording = false;
        clearInterval(recorderTimer);
        recorderIndicator.classList.remove('recording');
        btnRecord.textContent = '● Record';
        btnRecord.classList.remove('recording');
        btnExport.disabled = state.recordedData.length === 0;
        document.getElementById('recorderState').textContent = 'Stopped';
        applyPolling();  // stop polling the record set
    } else {
        buildRecorderSelection();
        if (recorderRecordSet.size === 0) {
            document.getElementById('recorderState').textContent = 'Select params first';
            return;
        }
        state.recording = true;
        state.recordingStart = Date.now();
        state.recordedData = [];
        state.recLast = {};
        recorderColumns.forEach(n => { state.recLast[n] = ''; });
        recordMeta = {
            ecu: ecuInfo.ecu,
            name: ecuInfo.name,
            started: new Date(state.recordingStart),
            factors: { ...recorderFactorsInt },
            units: {}
        };
        recorderColumns.forEach(n => { recordMeta.units[n] = (paramMeta[n] || {}).unit || ''; });
        recorderIndicator.classList.add('recording');
        btnRecord.textContent = '■ Stop';
        btnRecord.classList.add('recording');
        btnExport.disabled = true;
        document.getElementById('recorderState').textContent = 'Recording';
        recorderTimer = setInterval(updateRecorderDisplay, 100);
        applyPolling();  // poll exactly the selected set at their factors
    }
});

btnExport.addEventListener('click', () => {
    if (state.recordedData.length === 0) return;
    const cols = recorderColumns;
    const factorStr = cols.map(n => `${n}(${recordMeta.factors[n] || 1})`).join(' ');
    const header = cols.map(n => {
        const s = unitSlug(recordMeta.units[n]);
        return s ? `${n}_${s}` : n;
    });

    const lines = [
        `# OBD1 Scanner recording`,
        `# ECU: ${recordMeta.name || '?'} (ROM ${recordMeta.ecu || '?'})`,
        `# Recorded: ${recordMeta.started.toISOString()}`,
        `# App version: ${APP_VERSION}`,
        `# Sample factors: ${factorStr}`,
        `# Values: user-calibrated = engineering * cal_slope + cal_offset (metric)`,
        `# Rows: one per data update; unchanged params carry forward (forward-filled)`,
        `epoch_ms,datetime,${header.join(',')}`,
    ];
    state.recordedData.forEach(row => {
        const iso = new Date(row.epoch).toISOString();
        lines.push(`${row.epoch},${iso},${cols.map(n => row[n] ?? '').join(',')}`);
    });

    const blob = new Blob([lines.join('\n')], { type: 'text/csv' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    const stamp = recordMeta.started.toISOString().slice(0, 19).replace(/[:T]/g, '-');
    a.href = url;
    a.download = `obd1_${recordMeta.ecu || 'rec'}_${stamp}.csv`;
    a.click();
    URL.revokeObjectURL(url);
});

//=============================================================================
// Settings
//=============================================================================
document.getElementById('settingRate').value = settings.updateRate;
document.getElementById('settingUnits').value = settings.units;

document.getElementById('settingRate').addEventListener('change', (e) => {
    settings.updateRate = parseInt(e.target.value);
    localStorage.setItem('obd1_rate', settings.updateRate);
});

document.getElementById('settingUnits').addEventListener('change', (e) => {
    settings.units = e.target.value;
    localStorage.setItem('obd1_units', settings.units);
});

document.getElementById('btnClearData').addEventListener('click', () => {
    state.plotData = {};
    state.recordedData = [];
    Object.keys(gaugeConfigs).forEach(id => {
        const el = document.getElementById(id);
        const bar = document.getElementById(`${id}-bar`);
        if (el) el.textContent = '0';
        if (bar) bar.style.width = '0%';
    });
});

//=============================================================================
// ECU Calibration (GET /params, POST /scaling)
//=============================================================================
// The firmware applies the hardcoded formula and sends engineering values.
// The user's calibration_slope/offset are applied here, on top:
//     displayed = engineering * cal_slope + cal_offset
// paramName -> { slope, offset }
let calibration = {};

function numOr(v, dflt) {
    const n = parseFloat(v);
    return Number.isFinite(n) ? n : dflt;
}

// Human-readable hardcoded formula, e.g. "×25" or "×1.8 − 147.6".
function fmtHw(p) {
    const s = numOr(p.hw_scale, 1), o = numOr(p.hw_offset, 0);
    let str = '×' + (Number.isInteger(s) ? s : s.toFixed(4));
    if (o) str += (o < 0 ? ` − ${Math.abs(o)}` : ` + ${o}`);
    return str;
}

// Fetch the full param catalog and refresh the calibration map used by the gauges.
async function fetchParams() {
    const res = await fetch(`${apiBase}/params`);
    if (!res.ok) throw new Error('HTTP ' + res.status);
    const data = await res.json();
    const map = {};
    paramMeta = {};
    for (const p of (data.params || [])) {
        map[p.name] = { slope: numOr(p.cal_slope, 1), offset: numOr(p.cal_offset, 0) };
        paramMeta[p.name] = { unit: p.unit || '' };
    }
    calibration = map;
    readMsEstimate = numOr(data.readMs, readMsEstimate);
    ecuInfo = { ecu: data.ecu || '', name: data.name || '' };
    return data;
}

// Render the editable calibration table.
async function loadCalibration() {
    const statusEl = document.getElementById('calStatus');
    const body = document.getElementById('calTableBody');
    statusEl.textContent = 'Loading…';
    body.innerHTML = '';

    try {
        const data = await fetchParams();
        document.getElementById('calEcuName').textContent =
            `${data.name || 'ECU'} (${data.ecu || '?'})`;

        const params = data.params || [];
        statusEl.textContent = `${params.length} parameters`;

        for (const p of params) {
            const tr = document.createElement('tr');
            tr.dataset.name = p.name;

            const tdName = document.createElement('td');
            tdName.textContent = p.name;
            const tdUnit = document.createElement('td');
            tdUnit.textContent = p.unit || '';
            const tdHw = document.createElement('td');
            tdHw.textContent = fmtHw(p);
            tdHw.className = 'cal-hw';

            const tdSlope = document.createElement('td');
            const inSlope = document.createElement('input');
            inSlope.type = 'number'; inSlope.step = 'any'; inSlope.className = 'cal-slope';
            inSlope.value = numOr(p.cal_slope, 1);
            tdSlope.appendChild(inSlope);

            const tdOff = document.createElement('td');
            const inOff = document.createElement('input');
            inOff.type = 'number'; inOff.step = 'any'; inOff.className = 'cal-offset';
            inOff.value = numOr(p.cal_offset, 0);
            tdOff.appendChild(inOff);

            tr.append(tdName, tdUnit, tdHw, tdSlope, tdOff);
            body.appendChild(tr);
        }
    } catch (e) {
        statusEl.textContent = `Failed to load /params: ${e.message} (is the scanner at ${HOST}?)`;
    }
}

// Persist calibration slope/offset and poll factor to the scanner.
// (Firmware keys: multiplier=slope, offset, factor.)
async function saveCalibration() {
    const statusEl = document.getElementById('calStatus');
    const rows = [...document.querySelectorAll('#calTableBody tr')];
    if (rows.length === 0) { statusEl.textContent = 'Nothing to save — load first.'; return; }

    const scaling = rows.map(tr => ({
        name: tr.dataset.name,
        multiplier: numOr(tr.querySelector('.cal-slope').value, 1),
        offset: numOr(tr.querySelector('.cal-offset').value, 0)
    }));

    statusEl.textContent = 'Saving…';
    try {
        const res = await fetch(`${apiBase}/scaling`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ scaling })
        });
        const data = await res.json();
        if (!res.ok || data.status === 'error') {
            throw new Error(data.message || ('HTTP ' + res.status));
        }
        // Apply saved calibration to the live map immediately (gauges update now).
        for (const s of scaling) calibration[s.name] = { slope: s.multiplier, offset: s.offset };
        statusEl.textContent = `Saved ${scaling.length} parameters ✓`;
    } catch (e) {
        statusEl.textContent = 'Save failed: ' + e.message;
    }
}

document.getElementById('btnRefreshCal').addEventListener('click', loadCalibration);
document.getElementById('btnSaveCal').addEventListener('click', saveCalibration);
document.getElementById('btnOpenCal').addEventListener('click', () => navigateTo('calibration'));

//=============================================================================
// Custom View (pick any params to watch live)
//=============================================================================
async function loadCustom() {
    const statusEl = document.getElementById('customStatus');
    const body = document.getElementById('customTableBody');
    statusEl.textContent = 'Loading…';
    body.innerHTML = '';
    try {
        const data = await fetchParams();
        const params = data.params || [];
        statusEl.textContent = `${params.length} parameters`;
        for (const p of params) {
            const tr = document.createElement('tr');
            const tdShow = document.createElement('td');
            const cb = document.createElement('input');
            cb.type = 'checkbox'; cb.checked = customSelected.has(p.name);
            cb.addEventListener('change', () => {
                if (cb.checked) customSelected.add(p.name); else customSelected.delete(p.name);
                localStorage.setItem('obd1_custom', JSON.stringify([...customSelected]));
                renderCustomGauges();
                resetPeriodStats();
                applyPolling();
                updateFooter();
            });
            tdShow.appendChild(cb);
            const tdName = document.createElement('td'); tdName.textContent = p.name;
            const tdUnit = document.createElement('td'); tdUnit.textContent = p.unit || '';
            tr.append(tdShow, tdName, tdUnit);
            body.appendChild(tr);
        }
        renderCustomGauges();
    } catch (e) {
        statusEl.textContent = `Failed to load /params: ${e.message}`;
    }
}

function renderCustomGauges() {
    const grid = document.getElementById('customGauges');
    grid.innerHTML = '';
    customGaugeEls = {};
    for (const name of customSelected) {
        const card = document.createElement('div');
        card.className = 'gauge-card';
        const label = document.createElement('div'); label.className = 'gauge-label'; label.textContent = name;
        const val = document.createElement('div'); val.className = 'gauge-value'; val.textContent = '0';
        const unit = document.createElement('div'); unit.className = 'gauge-unit'; unit.textContent = (paramMeta[name] || {}).unit || '';
        card.append(label, val, unit);
        grid.appendChild(card);
        customGaugeEls[name] = { valueEl: val };
    }
}

//=============================================================================
// Connection (auto-connect + auto-reconnect)
//=============================================================================
function connect() {
    try {
        state.websocket = new WebSocket(wsUrl);
    } catch (e) {
        scheduleReconnect();
        return;
    }

    state.websocket.onopen = () => {
        state.connected = true;
        fetchParams().catch(() => {});  // calibration for scaling
        applyPolling();                  // assert current page's poll set
    };

    state.websocket.onmessage = (event) => {
        pulseHeartbeat();
        try {
            const data = JSON.parse(event.data);
            if (Array.isArray(data.params)) {
                data.params.forEach(p => {
                    // Measure update period from device-time deltas (real samples).
                    if (typeof p.time === 'number') {
                        const prev = state.sampleTimes[p.name];
                        if (prev !== undefined && p.time > prev) {
                            const dt = p.time - prev;
                            const ema = state.sampleIntervals[p.name];
                            state.sampleIntervals[p.name] = (ema === undefined) ? dt : ema * 0.7 + dt * 0.3;
                        }
                        state.sampleTimes[p.name] = p.time;
                    }
                    // FW sends engineering value (p.value); apply user calibration on top.
                    const eng = (p.value !== undefined) ? p.value : p.raw;
                    const cal = calibration[p.name] || { slope: 1, offset: 0 };
                    const disp = (eng !== undefined) ? eng * cal.slope + cal.offset : undefined;

                    const gaugeId = ecuParamToGauge[p.name];
                    if (gaugeId && disp !== undefined) updateGauge(gaugeId, disp);

                    const cg = customGaugeEls[p.name];
                    if (cg && disp !== undefined) cg.valueEl.textContent = fmtVal(disp);

                    if (state.recording && recorderRecordSet.has(p.name) && disp !== undefined) {
                        state.recLast[p.name] = disp;   // user-calibrated value; forward-filled
                    }
                });
                // One wide row per snapshot: timestamp + every column (unchanged
                // params keep their previous value = forward fill).
                if (state.recording) {
                    const row = { epoch: Date.now() };
                    recorderColumns.forEach(n => { row[n] = state.recLast[n]; });
                    state.recordedData.push(row);
                }
            }
            updatePlots();
            updateFooter();
        } catch (e) {
            console.error('Parse error', e);
        }
    };

    state.websocket.onerror = () => { try { state.websocket.close(); } catch (e) {} };
    state.websocket.onclose = () => {
        state.connected = false;
        scheduleReconnect();
    };
}

function scheduleReconnect() {
    clearTimeout(state.reconnectTimer);
    state.reconnectTimer = setTimeout(connect, 2000);
}

// Auto-connect on load and keep retrying if the link drops.
connect();

//=============================================================================
// Install Prompt
//=============================================================================
let deferredPrompt;
window.addEventListener('beforeinstallprompt', (e) => {
    e.preventDefault();
    deferredPrompt = e;
});

console.log('BT Diagnostic Tool v2.0 ready');
