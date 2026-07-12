//=============================================================================
// OBD1 Scanner — barebones device console
//
// Talks to the firmware served from the device:
//   GET  /status              -> device status JSON
//   WS   :81                  -> same JSON, pushed ~1 Hz (heartbeat)
//   POST /update/firmware     -> OTA the app image
//   POST /update/filesystem   -> OTA this PWA
//=============================================================================

// The app is served from the device, so talk to whoever served it. Fall back
// to the SoftAP address when opened from a laptop for dev.
const HOST = (location.hostname && !['localhost', '127.0.0.1', ''].includes(location.hostname))
  ? location.hostname : '192.168.4.1';
const API = `http://${HOST}`;
const WS_URL = `ws://${HOST}:81/`;

// --- Service worker (installable PWA) ---------------------------------------
if ('serviceWorker' in navigator) {
  window.addEventListener('load', () => {
    navigator.serviceWorker.register('/service-worker.js').catch(() => {});
  });
}

// --- Status rendering -------------------------------------------------------
const $ = (id) => document.getElementById(id);

function fmtUptime(ms) {
  const s = Math.floor(ms / 1000);
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  return `${h}h ${m}m ${s % 60}s`;
}

function setLink(online) {
  $('dot').classList.toggle('online', online);
  $('s-link').textContent = online ? 'connected' : 'offline';
}

function render(d) {
  setLink(true);
  $('s-version').textContent = d.version || '—';
  $('s-build').textContent   = d.build || '—';
  $('s-commit').textContent  = d.commit || '—';
  $('s-uptime').textContent  = d.uptime_ms != null ? fmtUptime(d.uptime_ms) : '—';
  $('s-heap').textContent    = d.free_heap != null ? `${(d.free_heap / 1024).toFixed(1)} KB` : '—';
  $('s-clients').textContent = d.clients != null ? d.clients : '—';
  $('s-sta').textContent     = d.sta_connected
    ? `${d.sta_ssid || 'connected'} (${d.sta_ip || ''})`
    : (d.sta_ssid ? `connecting to ${d.sta_ssid}…` : 'not set up');
  $('foot').textContent      = `${d.name || 'OBD1 Scanner'} · ${d.version || ''}`.trim();
}

// --- Live link: WebSocket heartbeat + HTTP poll fallback --------------------
let ws;
function connectWs() {
  try {
    ws = new WebSocket(WS_URL);
  } catch (e) { return; }
  ws.onmessage = (ev) => { try { render(JSON.parse(ev.data)); } catch {} };
  ws.onclose = () => { setLink(false); setTimeout(connectWs, 2000); };
  ws.onerror = () => { try { ws.close(); } catch {} };
}

async function pollStatus() {
  try {
    const r = await fetch(`${API}/status`, { cache: 'no-store' });
    if (r.ok) render(await r.json());
  } catch {
    setLink(false);
  }
}

connectWs();
pollStatus();
setInterval(pollStatus, 3000);  // fallback / initial fill if WS is down

// --- OTA upload -------------------------------------------------------------
function upload(endpoint, fileEl, btn, bar, msg) {
  const file = fileEl.files[0];
  if (!file) { setMsg(msg, 'Choose a .bin file first.', 'bad'); return; }

  btn.disabled = true;
  setMsg(msg, 'Uploading…', '');
  bar.style.width = '0%';

  const form = new FormData();
  form.append('file', file, file.name);

  const xhr = new XMLHttpRequest();
  xhr.open('POST', `${API}${endpoint}`);
  xhr.upload.onprogress = (e) => {
    if (e.lengthComputable) bar.style.width = `${(e.loaded / e.total * 100).toFixed(0)}%`;
  };
  xhr.onload = () => {
    btn.disabled = false;
    if (xhr.status === 200) {
      bar.style.width = '100%';
      setMsg(msg, 'Update applied — device rebooting. Reconnect in ~10 s.', 'ok');
    } else {
      setMsg(msg, `Failed (HTTP ${xhr.status}). ${xhr.responseText || ''}`, 'bad');
    }
  };
  xhr.onerror = () => {
    btn.disabled = false;
    // A firmware update can drop the socket as the device reboots — that's
    // usually success, not failure.
    setMsg(msg, 'Connection closed. If flashing firmware, this is normal — device is rebooting.', 'ok');
  };
  xhr.send(form);
}

function setMsg(el, text, cls) {
  el.textContent = text;
  el.className = `msg ${cls}`;
}

$('fw-btn').addEventListener('click', () =>
  upload('/update/firmware', $('fw-file'), $('fw-btn'), $('fw-bar'), $('fw-msg')));
$('fs-btn').addEventListener('click', () =>
  upload('/update/filesystem', $('fs-file'), $('fs-btn'), $('fs-bar'), $('fs-msg')));

// --- Wi-Fi provisioning + internet pull-OTA ---------------------------------
$('wifi-form').addEventListener('submit', async (e) => {
  e.preventDefault();
  const ssid = $('wifi-ssid').value.trim();
  if (!ssid) { setMsg($('wifi-msg'), 'Enter a Wi-Fi network name.', 'bad'); return; }
  $('wifi-btn').disabled = true;
  setMsg($('wifi-msg'), 'Saving…', '');
  try {
    const body = new URLSearchParams({ ssid, pass: $('wifi-pass').value });
    const r = await fetch(`${API}/wifi`, { method: 'POST', body });
    const j = await r.json();
    setMsg($('wifi-msg'), r.ok ? 'Saved — connecting. Watch “Internet” above.' : (j.msg || 'Failed'),
           r.ok ? 'ok' : 'bad');
  } catch {
    setMsg($('wifi-msg'), 'Could not reach the scanner.', 'bad');
  }
  $('wifi-btn').disabled = false;
});

$('chk-btn').addEventListener('click', async () => {
  $('chk-btn').disabled = true;
  $('pull-btn').style.display = 'none';
  setMsg($('chk-msg'), 'Checking…', '');
  try {
    const j = await (await fetch(`${API}/update/check`, { cache: 'no-store' })).json();
    if (!j.ok) {
      setMsg($('chk-msg'), j.msg || 'Check failed.', 'bad');
    } else if (j.update_available) {
      setMsg($('chk-msg'), `Update available: ${j.current} → ${j.latest}`, 'ok');
      $('pull-btn').style.display = '';
    } else {
      setMsg($('chk-msg'), `Up to date (${j.current}).`, 'ok');
    }
  } catch {
    setMsg($('chk-msg'), 'Could not reach the scanner.', 'bad');
  }
  $('chk-btn').disabled = false;
});

$('pull-btn').addEventListener('click', async () => {
  $('pull-btn').disabled = true;
  setMsg($('pull-msg'), 'Downloading + flashing on the device… (~30 s, do not power off)', '');
  try {
    const r = await fetch(`${API}/update/pull`, { method: 'POST' });
    const j = await r.json();
    setMsg($('pull-msg'), r.ok ? 'Updated — device rebooting. Reconnect shortly.' : (j.msg || 'Failed'),
           r.ok ? 'ok' : 'bad');
  } catch {
    // The socket drops as the device reboots on success.
    setMsg($('pull-msg'), 'Connection closed — device is likely rebooting after update.', 'ok');
  }
  $('pull-btn').disabled = false;
});
