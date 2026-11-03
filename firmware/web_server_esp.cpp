#include "web_server_esp.h"
#include "camera_module.h"
#include "config.h"
#include "sd_storage.h"

#include "esp_camera.h"
#include "esp_http_server.h"
#include <ArduinoJson.h>
#include <SD.h>
#include <WiFi.h>
#include <algorithm>
#include <atomic>
#include <string.h>
#include <time.h>
#include <vector>

#define PART_BOUNDARY "123456789000000000000987654321"

static const char* STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUNDARY =
    "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART =
    "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static const char DASHBOARD_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="pl">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>SPY-ESP Control</title>
  <style>
    :root {
      color-scheme: dark;
      --bg: #07111f;
      --panel: rgba(10, 18, 33, 0.86);
      --line: rgba(151, 164, 185, 0.18);
      --text: #eef4ff;
      --muted: #9eb0c9;
      --accent: #57b8ff;
      --accent2: #7affc4;
      --danger: #ff708b;
      --shadow: 0 24px 64px rgba(0, 0, 0, 0.38);
      --radius: 22px;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      font-family: Inter, ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      background:
        radial-gradient(circle at top left, rgba(87, 184, 255, 0.16), transparent 28%),
        radial-gradient(circle at top right, rgba(122, 255, 196, 0.11), transparent 24%),
        linear-gradient(180deg, #060b13 0%, #07111f 100%);
      color: var(--text);
    }
    .shell {
      width: min(1180px, calc(100vw - 24px));
      margin: 14px auto 24px;
      display: grid;
      gap: 18px;
    }
    .hero, .panel {
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: var(--radius);
      box-shadow: var(--shadow);
      backdrop-filter: blur(14px);
    }
    .hero {
      display: grid;
      grid-template-columns: 1.1fr 0.9fr;
      gap: 18px;
      padding: 22px;
    }
    .brand {
      display: flex;
      align-items: center;
      gap: 16px;
      margin-bottom: 10px;
    }
    .brand-mark {
      width: 76px;
      height: 76px;
      border-radius: 20px;
      display: grid;
      place-items: center;
      background: linear-gradient(145deg, rgba(87, 184, 255, 0.2), rgba(122, 255, 196, 0.08));
      border: 1px solid rgba(122, 255, 196, 0.2);
      flex: none;
    }
    .brand-mark svg { width: 48px; height: 48px; }
    h1, h2, p { margin: 0; }
    h1 {
      font-size: clamp(28px, 4vw, 44px);
      line-height: 0.94;
      letter-spacing: -0.04em;
    }
    .hero-copy p, .panel p, .muted { color: var(--muted); }
    .hero-copy { display: grid; gap: 12px; }
    .hero-copy p { line-height: 1.55; max-width: 58ch; }
    .status-pill {
      display: inline-flex;
      align-items: center;
      gap: 10px;
      border-radius: 999px;
      padding: 10px 14px;
      border: 1px solid var(--line);
      background: rgba(255, 255, 255, 0.04);
      font-weight: 700;
      width: fit-content;
    }
    .status-dot {
      width: 10px;
      height: 10px;
      border-radius: 999px;
      background: #ffbf6f;
      box-shadow: 0 0 18px currentColor;
    }
    .status-dot.online { background: var(--accent2); }
    .status-dot.recording { background: var(--danger); }
    .quick-stats, .status-grid, .storage-grid {
      display: grid;
      gap: 12px;
      grid-template-columns: repeat(3, minmax(0, 1fr));
    }
    .status-grid, .storage-grid { grid-template-columns: repeat(2, minmax(0, 1fr)); }
    .stat, .kv {
      padding: 14px;
      border-radius: 18px;
      border: 1px solid var(--line);
      background: rgba(255, 255, 255, 0.04);
    }
    .stat strong, .kv strong {
      display: block;
      margin-top: 8px;
      font-size: 1.08rem;
      overflow-wrap: anywhere;
    }
    .layout {
      display: grid;
      grid-template-columns: 1.05fr 0.95fr;
      gap: 18px;
    }
    .panel { padding: 18px; display: grid; gap: 16px; }
    .panel-head { display: grid; gap: 6px; }
    .controls {
      display: grid;
      gap: 12px;
    }
    .row {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 12px;
    }
    .field {
      display: grid;
      gap: 8px;
    }
    .field label {
      color: var(--muted);
      font-size: 0.92rem;
    }
    select, input {
      width: 100%;
      border-radius: 12px;
      border: 1px solid var(--line);
      background: rgba(255, 255, 255, 0.04);
      color: var(--text);
      padding: 12px 14px;
      font-weight: 600;
    }
    .actions {
      display: flex;
      flex-wrap: wrap;
      gap: 12px;
    }
    button, .link-btn {
      appearance: none;
      border: none;
      cursor: pointer;
      border-radius: 14px;
      padding: 13px 16px;
      color: var(--text);
      font-weight: 700;
      letter-spacing: 0.01em;
      background: linear-gradient(135deg, rgba(87, 184, 255, 0.18), rgba(87, 184, 255, 0.08));
      border: 1px solid rgba(87, 184, 255, 0.26);
      text-decoration: none;
      display: inline-flex;
      align-items: center;
      justify-content: center;
      transition: transform 120ms ease;
    }
    .secondary { background: rgba(255, 255, 255, 0.04); border-color: var(--line); }
    .danger { background: linear-gradient(135deg, rgba(255, 112, 139, 0.22), rgba(255, 112, 139, 0.12)); border-color: rgba(255, 112, 139, 0.28); }
    button:hover, .link-btn:hover { transform: translateY(-1px); }
    button:disabled, .link-btn.disabled {
      opacity: 0.45;
      cursor: default;
      transform: none;
      pointer-events: none;
    }
    .callout {
      padding: 14px;
      border-radius: 16px;
      border: 1px solid rgba(255,255,255,0.08);
      background: rgba(255,255,255,0.035);
      color: var(--muted);
      line-height: 1.5;
    }
    @media (max-width: 1000px) {
      .hero, .layout { grid-template-columns: 1fr; }
    }
    @media (max-width: 720px) {
      .shell { width: min(100vw - 16px, 1180px); }
      .quick-stats, .status-grid, .storage-grid, .row { grid-template-columns: 1fr; }
      .hero, .panel { padding: 16px; }
    }
  </style>
</head>
<body>
  <main class="shell">
    <section class="hero">
      <div class="hero-copy">
        <div class="brand">
          <div class="brand-mark" aria-hidden="true">
            <svg viewBox="0 0 64 64" fill="none">
              <path d="M10 24c6-8 13-12 22-12s16 4 22 12c-6 8-13 12-22 12S16 32 10 24Z" stroke="#7affc4" stroke-width="3"/>
              <circle cx="32" cy="24" r="8" fill="#57b8ff"/>
              <path d="M24 46h16" stroke="#eef4ff" stroke-width="3" stroke-linecap="round"/>
              <path d="M20 52h24" stroke="#57b8ff" stroke-width="3" stroke-linecap="round" opacity="0.85"/>
            </svg>
          </div>
          <div>
            <div class="muted">Mission Control</div>
            <h1>SPY-ESP</h1>
          </div>
        </div>
        <p>
          Lekki panel operatorski dla ESP32-S3 Sense. Jedna akcja naraz, szybki status i osobne widoki dla sesji oraz plikow.
        </p>
        <div class="actions">
          <button id="startBtn">Start nagrywania</button>
          <button id="stopBtn" class="danger">Stop nagrywania</button>
          <button id="captureBtn" class="secondary">Zapisz zdjecie</button>
          <button id="refreshBtn" class="secondary">Odswiez status</button>
          <a class="link-btn secondary" href="/sessions" target="_blank">Przegladaj sesje</a>
          <a id="streamLink" class="link-btn secondary" href="#" target="_blank" rel="noreferrer">Otworz stream</a>
        </div>
      </div>
      <div>
        <div class="status-pill">
          <span id="statusDot" class="status-dot"></span>
          <span id="statusText">Ladowanie statusu...</span>
        </div>
        <div class="quick-stats" style="margin-top:14px;">
          <div class="stat"><div class="muted">Siec</div><strong id="wifiName">-</strong></div>
          <div class="stat"><div class="muted">IP</div><strong id="ipValue">-</strong></div>
          <div class="stat"><div class="muted">Sesja</div><strong id="sessionDir">-</strong></div>
        </div>
      </div>
    </section>

    <section class="layout" style="grid-template-columns:1fr;">
      <section class="panel">
        <div class="panel-head">
          <h2>Podglad kamery</h2>
          <p>Live stream MJPEG z ESP32-S3.</p>
        </div>
        <div id="feedWrap" style="position:relative;background:#08101c;border-radius:12px;overflow:hidden;min-height:180px;display:flex;align-items:center;justify-content:center;">
          <img id="cameraFeed" alt="stream" style="width:100%;display:block;border-radius:12px;">
          <div id="feedError" style="display:none;color:var(--muted);padding:32px;text-align:center;">Brak obrazu &mdash; kamera niedostepna</div>
        </div>
      </section>
    </section>

    <section class="layout">
      <section class="panel">
        <div class="panel-head">
          <h2>Sterowanie</h2>
          <p>Najpierw ustaw parametry, potem uruchom nagrywanie.</p>
        </div>
        <div class="controls">
          <div class="row">
            <div class="field">
              <label for="resolutionSelect">Rozdzielczosc</label>
              <select id="resolutionSelect">
                <option value="QQVGA">160x120</option>
                <option value="QVGA">320x240</option>
                <option value="VGA">640x480</option>
                <option value="SVGA">800x600</option>
              </select>
            </div>
            <div class="field">
              <label for="fpsSelect">FPS AVI</label>
              <select id="fpsSelect">
                <option value="2">2 FPS</option>
                <option value="5">5 FPS</option>
                <option value="10">10 FPS</option>
              </select>
            </div>
          </div>
          <div class="actions">
            <button id="applyResolutionBtn" class="secondary">Zapisz rozdzielczosc</button>
            <button id="applyFpsBtn" class="secondary">Zapisz FPS</button>
          </div>
          <div class="callout" id="actionMessage">
            Jedna akcja naraz. Jesli trwa pobieranie pliku lub inna operacja, kolejne polecenie zostanie odrzucone.
          </div>
        </div>
      </section>

      <section class="panel">
        <div class="panel-head">
          <h2>Stan</h2>
          <p>Szybki podglad bez skanowania calej karty SD.</p>
        </div>
        <div class="status-grid">
          <div class="kv"><span>Kamera</span><strong id="cameraState">-</strong></div>
          <div class="kv"><span>Rozdzielczosc</span><strong id="cameraResolution">-</strong></div>
          <div class="kv"><span>AVI FPS</span><strong id="aviFps">-</strong></div>
          <div class="kv"><span>AVI klatki</span><strong id="videoFrameCount">0</strong></div>
          <div class="kv"><span>AVI plik</span><strong id="videoPath">-</strong></div>
          <div class="kv"><span>RSSI</span><strong id="rssiValue">-</strong></div>
        </div>
        <div class="actions">
          <a id="downloadAviBtn" class="link-btn secondary disabled" download>Pobierz AVI</a>
        </div>
      </section>
    </section>

    <section class="layout">
      <section class="panel">
        <div class="panel-head">
          <h2>Pamiec SD</h2>
          <p>Podstawowe dane o karcie i liczbie sesji.</p>
        </div>
        <div class="storage-grid">
          <div class="kv"><span>Status SD</span><strong id="sdState">-</strong></div>
          <div class="kv"><span>Pojemnosc</span><strong id="sdTotal">-</strong></div>
          <div class="kv"><span>Wykorzystane</span><strong id="sdUsed">-</strong></div>
          <div class="kv"><span>Wolne</span><strong id="sdFree">-</strong></div>
          <div class="kv"><span>Sesje</span><strong id="sessionCount">0</strong></div>
          <div class="kv"><span>JPEG w sesji</span><strong id="savedCount">0</strong></div>
        </div>
      </section>

      <section class="panel">
        <div class="panel-head">
          <h2>Urzadzenie</h2>
          <p>Diagnoza pracy firmware.</p>
        </div>
        <div class="status-grid">
          <div class="kv"><span>Uptime</span><strong id="uptimeValue">-</strong></div>
          <div class="kv"><span>Heap</span><strong id="heapValue">-</strong></div>
          <div class="kv"><span>Czas lokalny</span><strong id="timeValue">-</strong></div>
          <div class="kv"><span>Busy</span><strong id="busyValue">-</strong></div>
        </div>
      </section>
    </section>
  </main>

  <script>
    const state = { status: null, actionBusy: false, userEditing: false, editTimeout: null };
    const $ = (id) => document.getElementById(id);

    function markUserEditing() {
      state.userEditing = true;
      clearTimeout(state.editTimeout);
      state.editTimeout = setTimeout(() => { state.userEditing = false; }, 30000);
    }

    function clearUserEditing() {
      state.userEditing = false;
      clearTimeout(state.editTimeout);
    }

    function formatMb(value) {
      if (value === undefined || value === null) return '-';
      return `${value} MB`;
    }

    function formatUptime(ms) {
      if (ms === undefined || ms === null) return '-';
      const sec = Math.floor(ms / 1000);
      const h = Math.floor(sec / 3600);
      const m = Math.floor((sec % 3600) / 60);
      const s = sec % 60;
      return `${h}h ${m}m ${s}s`;
    }

    function setMessage(text) {
      $('actionMessage').textContent = text;
      $('statusText').textContent = text;
    }

    function setControlsEnabled(enabled) {
      for (const id of ['startBtn', 'stopBtn', 'captureBtn', 'refreshBtn', 'applyResolutionBtn', 'applyFpsBtn']) {
        $(id).disabled = !enabled;
      }
    }

    function renderStatus() {
      const s = state.status;
      if (!s) return;

      const dot = $('statusDot');
      dot.className = 'status-dot';
      if (s.recording) dot.classList.add('recording');
      else if (s.camera_ready && s.sd_ready) dot.classList.add('online');

      $('wifiName').textContent = s.wifi_ssid || '-';
      $('ipValue').textContent = s.ip || '-';
      $('sessionDir').textContent = s.session_dir || '-';
      $('cameraState').textContent = s.camera_ready ? 'Gotowa' : 'Brak';
      $('cameraResolution').textContent = s.camera_resolution || '-';
      $('aviFps').textContent = s.avi_fps ? `${s.avi_fps} FPS` : '-';
      $('videoFrameCount').textContent = s.video_frame_count ?? 0;
      $('videoPath').textContent = s.video_path || '-';
      $('rssiValue').textContent = s.wifi_rssi !== undefined ? `${s.wifi_rssi} dBm` : '-';
      $('sdState').textContent = s.sd_ready ? 'Zamontowana' : 'Brak';
      $('sdTotal').textContent = formatMb(s.sd_total_mb);
      $('sdUsed').textContent = formatMb(s.sd_used_mb);
      $('sdFree').textContent = formatMb(s.sd_free_mb);
      $('sessionCount').textContent = s.session_count ?? 0;
      $('savedCount').textContent = s.saved_count ?? 0;
      $('uptimeValue').textContent = formatUptime(s.uptime_ms);
      $('heapValue').textContent = `${s.free_heap ?? '-'} B`;
      $('timeValue').textContent = s.local_time || '-';
      $('busyValue').textContent = s.busy ? 'Tak' : 'Nie';

      if (!state.userEditing) {
        $('resolutionSelect').value = s.camera_frame_size || 'QVGA';
        $('fpsSelect').value = String(s.avi_fps || 5);
      }

      const controlsAllowed = !state.actionBusy && !s.busy;
      setControlsEnabled(controlsAllowed);
      $('startBtn').disabled = !controlsAllowed || !s.sd_ready || !!s.recording;
      $('stopBtn').disabled = !controlsAllowed || !s.recording;
      $('captureBtn').disabled = !controlsAllowed || !s.sd_ready || !!s.busy;

      const downloadBtn = $('downloadAviBtn');
      if (s.video_path) {
        downloadBtn.href = `http://${window.location.hostname}:82/media?path=${encodeURIComponent(s.video_path)}`;
        downloadBtn.classList.remove('disabled');
      } else {
        downloadBtn.removeAttribute('href');
        downloadBtn.classList.add('disabled');
      }

      if (!state.actionBusy) {
        if (s.busy) setMessage('Urzadzenie jest zajete transferem pliku. Poczekaj na zakonczenie.');
        else if (s.recording) setMessage('Nagrywanie aktywne. Uzyj Stop nagrywania aby domknac plik AVI.');
        else setMessage('Urzadzenie gotowe. Ustaw parametry i rozpocznij nagrywanie.');
      }
    }

    async function getJson(url, options) {
      const res = await fetch(url, options);
      if (!res.ok) {
        let msg = `${res.status} ${res.statusText}`;
        try {
          msg = await res.text();
        } catch (_) {}
        throw new Error(msg);
      }
      return res.json();
    }

    async function postJson(url, body) {
      return getJson(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
      });
    }

    async function refreshStatus() {
      try {
        state.status = await getJson('/status');
        renderStatus();
      } catch (error) {
        setMessage(`Blad statusu: ${error.message}`);
      }
    }

    async function runAction(action, fn) {
      if (state.actionBusy) {
        setMessage('Akcja juz trwa. Poczekaj na zakonczenie poprzedniej.');
        return;
      }
      if (state.status?.busy) {
        setMessage('Urzadzenie zajete transferem pliku. Sprobuj ponownie po zakonczeniu pobierania.');
        return;
      }

      state.actionBusy = true;
      renderStatus();
      try {
        setMessage(`${action}...`);
        await fn();
        await refreshStatus();
      } catch (error) {
        setMessage(`Blad: ${error.message}`);
      } finally {
        state.actionBusy = false;
        renderStatus();
      }
    }

    $('resolutionSelect').addEventListener('change', markUserEditing);
    $('fpsSelect').addEventListener('change', markUserEditing);

    function reconnectStream() {
      const feed = $('cameraFeed');
      const feedError = $('feedError');
      feed.src = '';
      feed.style.display = 'block';
      feedError.style.display = 'none';
      setTimeout(() => {
        feed.src = `http://${window.location.hostname}:81/stream`;
      }, 1500);
    }

    function stopStreamThen(fn) {
      const feed = $('cameraFeed');
      feed.src = '';
      return new Promise(resolve => setTimeout(resolve, 600)).then(fn);
    }

    $('startBtn').addEventListener('click', () => runAction('Start nagrywania', () => postJson('/record/start', {})));
    $('stopBtn').addEventListener('click', () => runAction('Stop nagrywania', () => postJson('/record/stop', {})));
    $('captureBtn').addEventListener('click', () => runAction('Zapis zdjecia', () => getJson('/capture')));
    $('refreshBtn').addEventListener('click', refreshStatus);
    $('applyResolutionBtn').addEventListener('click', () =>
      stopStreamThen(() => runAction('Zmiana rozdzielczosci', () => postJson('/camera/config', { frame_size: $('resolutionSelect').value })))
        .then(() => { clearUserEditing(); reconnectStream(); })
        .catch(() => { reconnectStream(); })
    );
    $('applyFpsBtn').addEventListener('click', () => runAction('Zmiana FPS', () => postJson('/recording/config', { avi_fps: Number($('fpsSelect').value || '5') })).then(clearUserEditing).catch(() => {}));

     $('streamLink').href = `http://${window.location.hostname}:81/stream`;

     const feed = $('cameraFeed');
     const feedError = $('feedError');
     feed.onerror = () => { feed.style.display = 'none'; feedError.style.display = 'block'; };
     feed.onload = () => { feed.style.display = 'block'; feedError.style.display = 'none'; };
     feed.src = `http://${window.location.hostname}:81/stream`;

     refreshStatus();
     setInterval(refreshStatus, 4000);
  </script>
</body>
</html>
)HTML";

static const char SESSIONS_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="pl">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>SPY-ESP Sessions</title>
  <style>
    :root { color-scheme: dark; --bg:#07111f; --panel:rgba(10,18,33,.86); --line:rgba(151,164,185,.18); --text:#eef4ff; --muted:#9eb0c9; --accent:#57b8ff; --shadow:0 24px 64px rgba(0,0,0,.38); --radius:22px; }
    *{box-sizing:border-box} body{margin:0;min-height:100vh;font-family:Inter,ui-sans-serif,system-ui,sans-serif;background:linear-gradient(180deg,#060b13 0%,#07111f 100%);color:var(--text)}
    .shell{width:min(1100px,calc(100vw - 24px));margin:14px auto 24px;display:grid;gap:18px}.panel{background:var(--panel);border:1px solid var(--line);border-radius:var(--radius);box-shadow:var(--shadow);padding:18px;display:grid;gap:14px}
    .top{display:flex;justify-content:space-between;gap:12px;align-items:center}.muted{color:var(--muted)}h1,h2,p{margin:0}.link-btn{appearance:none;border:none;cursor:pointer;border-radius:14px;padding:13px 16px;color:var(--text);font-weight:700;background:rgba(255,255,255,.04);border:1px solid var(--line);text-decoration:none;display:inline-flex;align-items:center;justify-content:center}
    input{width:100%;border-radius:12px;border:1px solid var(--line);background:rgba(255,255,255,.04);color:var(--text);padding:12px 14px;font-weight:600}
    .list{display:grid;gap:12px}.item{display:grid;grid-template-columns:1fr auto;gap:14px;align-items:center;padding:14px;border-radius:18px;border:1px solid var(--line);background:rgba(255,255,255,.03)}
    .meta{color:var(--muted);font-size:.92rem;margin-top:6px}.empty{padding:20px;border:1px dashed rgba(255,255,255,.14);border-radius:16px;text-align:center;color:var(--muted)}
    .spinner{text-align:center;padding:24px;color:var(--muted);font-size:.95rem}.spinner::before{content:'';display:block;width:32px;height:32px;border:3px solid rgba(87,184,255,.2);border-top-color:var(--accent);border-radius:50%;animation:spin .8s linear infinite;margin:0 auto 10px}
    @keyframes spin{to{transform:rotate(360deg)}}
    .load-more-btn{width:100%;padding:13px;border-radius:14px;border:1px dashed var(--line);background:rgba(255,255,255,.03);color:var(--accent);font-weight:700;cursor:pointer;font-size:.95rem}
    .load-more-btn:disabled{opacity:.4;cursor:default}
    @media (max-width:700px){.item,.top{grid-template-columns:1fr;display:grid}}
  </style>
</head>
<body>
  <main class="shell">
    <section class="panel">
      <div class="top">
        <div>
          <h1>Sesje</h1>
          <p class="muted">Osobna, lekka lista sesji. Wejdz do szczegolow wybranej sesji.</p>
        </div>
        <a class="link-btn" href="/">Powrot do dashboardu</a>
      </div>
      <input id="filter" type="text" placeholder="Filtruj po nazwie sesji lub dacie">
      <div id="list" class="list"><div class="spinner">Ladowanie sesji...</div></div>
      <button id="loadMore" class="load-more-btn" style="display:none" onclick="loadMore()">Zaladuj wiecej</button>
    </section>
  </main>
  <script>
    const list = document.getElementById('list');
    const filter = document.getElementById('filter');
    const loadMoreBtn = document.getElementById('loadMore');
    const LIMIT = 10;
    let sessions = [];
    let total = 0;
    let offset = 0;
    let loading = false;

    function renderItems(items) {
      for (const session of items) {
        const node = document.createElement('article');
        node.className = 'item';
        node.innerHTML = `
          <div>
            <h2>${session.name}</h2>
            <div class="meta">${session.video_path ? 'MP4 gotowy' : 'brak nagrania'} \u2022 ${session.path}</div>
          </div>
          <a class="link-btn" href="/session?path=${encodeURIComponent(session.path)}">Otworz sesje</a>
        `;
        list.appendChild(node);
      }
    }

    function applyFilter() {
      const q = filter.value.trim().toLowerCase();
      const items = list.querySelectorAll('.item');
      let visible = 0;
      items.forEach(el => {
        const name = el.querySelector('h2').textContent.toLowerCase();
        const show = !q || name.includes(q);
        el.style.display = show ? '' : 'none';
        if (show) visible++;
      });
      let empty = list.querySelector('.empty');
      if (!visible && sessions.length) {
        if (!empty) { empty = document.createElement('div'); empty.className='empty'; list.appendChild(empty); }
        empty.textContent = 'Brak pasujacych sesji.';
      } else if (empty) {
        empty.remove();
      }
    }

    async function loadMore() {
      if (loading) return;
      loading = true;
      loadMoreBtn.disabled = true;
      loadMoreBtn.textContent = 'Ladowanie...';

      try {
        const res = await fetch(`/sessions-data?offset=${offset}&limit=${LIMIT}`);
        const data = await res.json();
        total = data.total || 0;
        const batch = data.sessions || [];
        sessions = sessions.concat(batch);
        offset += batch.length;

        // Remove spinner if first load
        const spinner = list.querySelector('.spinner');
        if (spinner) spinner.remove();

        if (!sessions.length) {
          list.innerHTML = '<div class="empty">Brak sesji na karcie SD.</div>';
        } else {
          renderItems(batch);
          applyFilter();
        }

        if (offset < total) {
          loadMoreBtn.style.display = '';
          loadMoreBtn.textContent = `Zaladuj wiecej (${offset} z ${total})`;
          loadMoreBtn.disabled = false;
        } else {
          loadMoreBtn.style.display = 'none';
        }
      } catch(e) {
        const spinner = list.querySelector('.spinner');
        if (spinner) spinner.remove();
        list.innerHTML = '<div class="empty">Blad ladowania sesji.</div>';
        loadMoreBtn.style.display = 'none';
      }
      loading = false;
    }

    filter.addEventListener('input', applyFilter);
    loadMore();
  </script>
</body>
</html>
)HTML";

static const char SESSION_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="pl">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>SPY-ESP Session Detail</title>
  <style>
    :root { color-scheme: dark; --bg:#07111f; --panel:rgba(10,18,33,.86); --line:rgba(151,164,185,.18); --text:#eef4ff; --muted:#9eb0c9; --shadow:0 24px 64px rgba(0,0,0,.38); --radius:22px; }
    *{box-sizing:border-box} body{margin:0;min-height:100vh;font-family:Inter,ui-sans-serif,system-ui,sans-serif;background:linear-gradient(180deg,#060b13 0%,#07111f 100%);color:var(--text)}
    .shell{width:min(1180px,calc(100vw - 24px));margin:14px auto 24px;display:grid;gap:18px}.panel{background:var(--panel);border:1px solid var(--line);border-radius:var(--radius);box-shadow:var(--shadow);padding:18px;display:grid;gap:14px}
    .top{display:flex;justify-content:space-between;gap:12px;align-items:center}.muted{color:var(--muted)}h1,h2,p{margin:0}.link-btn{appearance:none;border:none;cursor:pointer;border-radius:14px;padding:13px 16px;color:var(--text);font-weight:700;background:rgba(255,255,255,.04);border:1px solid var(--line);text-decoration:none;display:inline-flex;align-items:center;justify-content:center}
    .actions{display:flex;gap:12px;flex-wrap:wrap}.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(190px,1fr));gap:12px}.thumb{display:block;border-radius:16px;overflow:hidden;border:1px solid var(--line);background:rgba(255,255,255,.03);text-decoration:none;color:var(--text)}
    .thumb img{width:100%;height:150px;object-fit:cover;display:block;background:#08101c}.thumb-body{padding:12px;display:grid;gap:8px}.name{font-size:.9rem;overflow-wrap:anywhere}.empty{padding:20px;border:1px dashed rgba(255,255,255,.14);border-radius:16px;text-align:center;color:var(--muted)}
    @media (max-width:700px){.top{display:grid;grid-template-columns:1fr}}
  </style>
</head>
<body>
  <main class="shell">
    <section class="panel">
      <div class="top">
        <div>
          <h1 id="title">Sesja</h1>
          <p class="muted" id="meta">Ladowanie...</p>
        </div>
        <div class="actions">
          <a class="link-btn" href="/sessions">Wroc do sesji</a>
          <a class="link-btn" href="/">Dashboard</a>
        </div>
      </div>
      <div class="actions">
        <a id="downloadAvi" class="link-btn" download>Pobierz AVI</a>
      </div>
      <div id="images" class="grid"></div>
    </section>
  </main>
  <script>
    const params = new URLSearchParams(window.location.search);
    const path = params.get('path') || '';
    async function load() {
      const res = await fetch(`/session-data?path=${encodeURIComponent(path)}`);
      const data = await res.json();
      document.getElementById('title').textContent = data.name || 'Sesja';
      document.getElementById('meta').textContent = `${data.image_count || 0} zdjec • ${data.path || ''}`;
      const avi = document.getElementById('downloadAvi');
      if (data.video_path) avi.href = `http://${window.location.hostname}:82/media?path=${encodeURIComponent(data.video_path)}`;
      else avi.style.display = 'none';
      const root = document.getElementById('images');
      root.innerHTML = '';
      if (!data.images?.length) {
        root.innerHTML = '<div class="empty">Brak zdjec w tej sesji.</div>';
        return;
      }
      for (const image of data.images) {
        const url = `http://${window.location.hostname}:82/media?path=${encodeURIComponent(image.path)}`;
        const card = document.createElement('a');
        card.className = 'thumb';
        card.href = url;
        card.target = '_blank';
        card.rel = 'noreferrer';
        card.innerHTML = `
          <img loading="lazy" src="${url}" alt="${image.name}">
          <div class="thumb-body">
            <div class="name">${image.name}</div>
            <div><a class="link-btn" href="${url}" download>Pobierz</a></div>
          </div>
        `;
        root.appendChild(card);
      }
    }
    load();
  </script>
</body>
</html>
)HTML";

static httpd_handle_t g_api_server = nullptr;
static httpd_handle_t g_stream_server = nullptr;
static httpd_handle_t g_media_server = nullptr;
static unsigned long g_last_avi_record_ms = 0;
static unsigned long g_last_jpeg_record_ms = 0;
static std::atomic<bool> g_media_transfer_busy{false};
static std::atomic<int>  g_stream_clients{0};

struct MediaEntry {
    String name;
    String path;
    size_t size;
};

struct SessionEntry {
    String name;
    String path;
    String video_path;
    std::vector<MediaEntry> images;
};

static String base_name(const char* path) {
    if (path == nullptr) {
        return String();
    }
    const char* slash = strrchr(path, '/');
    return slash ? String(slash + 1) : String(path);
}

static bool is_jpeg_name(const String& name) {
    return name.endsWith(".jpg") || name.endsWith(".jpeg") || name.endsWith(".JPG") || name.endsWith(".JPEG");
}

static size_t url_decode_inplace(char* text) {
    if (text == nullptr) {
        return 0;
    }
    char* src = text;
    char* dst = text;
    while (*src != '\0') {
        if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            auto hex = [](char c) -> uint8_t {
                if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
                if (c >= 'a' && c <= 'f') return (uint8_t)(10 + c - 'a');
                if (c >= 'A' && c <= 'F') return (uint8_t)(10 + c - 'A');
                return 0;
            };
            *dst++ = (char)((hex(src[1]) << 4) | hex(src[2]));
            src += 3;
            continue;
        }
        if (*src == '+') {
            *dst++ = ' ';
            src++;
            continue;
        }
        *dst++ = *src++;
    }
    *dst = '\0';
    return (size_t)(dst - text);
}

static void send_json(httpd_req_t* req, JsonDocument& doc) {
    String payload;
    serializeJson(doc, payload);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, payload.c_str(), payload.length());
}

static String local_time_string() {
    struct tm tm_now = {};
    if (!getLocalTime(&tm_now, 10)) {
        return String("-");
    }
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
             tm_now.tm_year + 1900,
             tm_now.tm_mon + 1,
             tm_now.tm_mday,
             tm_now.tm_hour,
             tm_now.tm_min,
             tm_now.tm_sec);
    return String(buffer);
}

static std::vector<SessionEntry> scan_sessions(bool include_images, const String& only_path = String()) {
    std::vector<SessionEntry> sessions;
    if (!sd_storage_is_ready()) {
        return sessions;
    }

    File root = SD.open(SD_ROOT_DIR);
    if (!root || !root.isDirectory()) {
        return sessions;
    }

    File entry = root.openNextFile();
    while (entry) {
        if (entry.isDirectory()) {
            SessionEntry session;
            session.path = String(entry.path());
            session.name = base_name(entry.path());

            // When not filtering to a specific path and not including images,
            // skip scanning directory contents — just list the folder names.
            bool scan_contents = include_images || (only_path.length() > 0 && session.path == only_path);

            if (only_path.length() == 0 || session.path == only_path) {
                if (scan_contents) {
                    File file = entry.openNextFile();
                    while (file) {
                        if (!file.isDirectory()) {
                            String name = base_name(file.path());
                            if (name.endsWith(".idx") || name.endsWith(".tmp")) {
                            } else if (name == "recording.idx") {
                            } else if (is_jpeg_name(name)) {
                                if (include_images) {
                                    session.images.push_back({name, String(file.path()), (size_t)file.size()});
                                }
                            } else if (name.endsWith(".avi") || name.endsWith(".AVI") || name.endsWith(".mov") || name.endsWith(".MOV") || name.endsWith(".mp4") || name.endsWith(".MP4")) {
                                session.video_path = String(file.path());
                            }
                        }
                        file.close();
                        file = entry.openNextFile();
                    }
                    std::sort(session.images.begin(), session.images.end(), [](const MediaEntry& a, const MediaEntry& b) {
                        return a.name > b.name;
                    });
                } else {
                    // Fast path: look only for .mp4 file without iterating all contents
                    File file = entry.openNextFile();
                    while (file) {
                        if (!file.isDirectory()) {
                            String name = base_name(file.path());
                            if (name.endsWith(".mp4") || name.endsWith(".MP4") ||
                                name.endsWith(".mov") || name.endsWith(".MOV") ||
                                name.endsWith(".avi") || name.endsWith(".AVI")) {
                                session.video_path = String(file.path());
                                file.close();
                                break;
                            }
                        }
                        file.close();
                        file = entry.openNextFile();
                    }
                }
                sessions.push_back(std::move(session));
            }
        }

        entry.close();
        entry = root.openNextFile();
    }

    std::sort(sessions.begin(), sessions.end(), [](const SessionEntry& a, const SessionEntry& b) {
        return a.name > b.name;
    });
    return sessions;
}

static void add_storage_stats(JsonDocument& doc) {
    if (!sd_storage_is_ready()) {
        doc["sd_total_mb"] = 0;
        doc["sd_used_mb"] = 0;
        doc["sd_free_mb"] = 0;
        return;
    }
    unsigned long long total_mb = SD.totalBytes() / (1024ULL * 1024ULL);
    unsigned long long used_mb = SD.usedBytes() / (1024ULL * 1024ULL);
    doc["sd_total_mb"] = total_mb;
    doc["sd_used_mb"] = used_mb;
    doc["sd_free_mb"] = total_mb >= used_mb ? total_mb - used_mb : 0;
}

static esp_err_t send_busy_error(httpd_req_t* req, const char* message) {
    httpd_resp_set_status(req, "409 Conflict");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_sendstr(req, message);
}

static esp_err_t send_html_chunked(httpd_req_t* req, const char* html) {
    const size_t CHUNK = 4096;
    size_t len = strlen(html);
    size_t offset = 0;
    while (offset < len) {
        size_t to_send = (len - offset) > CHUNK ? CHUNK : (len - offset);
        esp_err_t r = httpd_resp_send_chunk(req, html + offset, to_send);
        if (r != ESP_OK) return r;
        offset += to_send;
    }
    return httpd_resp_send_chunk(req, nullptr, 0);
}

static esp_err_t dashboard_handler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return send_html_chunked(req, DASHBOARD_HTML);
}

static esp_err_t sessions_page_handler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return send_html_chunked(req, SESSIONS_HTML);
}

static esp_err_t media_redirect_handler(httpd_req_t* req) {
    char query[192] = {0};
    char location[320] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        snprintf(location, sizeof(location), "http://%s:%d/media?%s", WiFi.localIP().toString().c_str(), HTTP_MEDIA_PORT, query);
    } else {
        snprintf(location, sizeof(location), "http://%s:%d/media", WiFi.localIP().toString().c_str(), HTTP_MEDIA_PORT);
    }
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", location);
    return httpd_resp_send(req, nullptr, 0);
}

static esp_err_t session_page_handler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return send_html_chunked(req, SESSION_HTML);
}

// Streaming JSON handler — iterates SD and sends chunks immediately,
// so the HTTP client does not time out even with many sessions.
static esp_err_t sessions_data_handler(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    bool sd_ready = sd_storage_is_ready();

    // --- count total sessions first (fast: no file open inside dirs) ---
    int total = 0;
    if (sd_ready) {
        File root = SD.open(SD_ROOT_DIR);
        if (root && root.isDirectory()) {
            File e = root.openNextFile();
            while (e) {
                if (e.isDirectory()) total++;
                e.close();
                e = root.openNextFile();
            }
            root.close();
        }
    }

    // Parse ?offset=N&limit=N
    char query[64] = {0};
    char param[16] = {0};
    int offset_val = 0;
    int limit_val = 10;
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "offset", param, sizeof(param)) == ESP_OK)
            offset_val = atoi(param);
        memset(param, 0, sizeof(param));
        if (httpd_query_key_value(query, "limit", param, sizeof(param)) == ESP_OK) {
            limit_val = atoi(param);
            if (limit_val <= 0 || limit_val > 50) limit_val = 10;
        }
    }

    // Send header chunk immediately so client knows we're alive
    char header[128];
    snprintf(header, sizeof(header),
             "{\"sd_ready\":%s,\"total\":%d,\"offset\":%d,\"limit\":%d,\"sessions\":[",
             sd_ready ? "true" : "false", total, offset_val, limit_val);
    httpd_resp_send_chunk(req, header, strlen(header));

    // --- stream sessions offset..offset+limit ---
    if (sd_ready) {
        // Collect sorted session names (only names, no file open inside) — fast
        std::vector<String> names;
        names.reserve(total);
        File root2 = SD.open(SD_ROOT_DIR);
        if (root2 && root2.isDirectory()) {
            File e = root2.openNextFile();
            while (e) {
                if (e.isDirectory()) names.push_back(String(base_name(e.path())));
                e.close();
                e = root2.openNextFile();
            }
            root2.close();
        }
        std::sort(names.begin(), names.end(), [](const String& a, const String& b){ return a > b; });

        int end = offset_val + limit_val;
        if (end > (int)names.size()) end = (int)names.size();
        bool first = true;
        for (int i = offset_val; i < end; i++) {
            // Now open individual session dir to find video
            String sess_path = String(SD_ROOT_DIR) + "/" + names[i];
            String video_path = "";
            File dir = SD.open(sess_path.c_str());
            if (dir && dir.isDirectory()) {
                File f = dir.openNextFile();
                while (f) {
                    if (!f.isDirectory()) {
                        String n = base_name(f.path());
                        if (n.endsWith(".mp4") || n.endsWith(".MP4") ||
                            n.endsWith(".mov") || n.endsWith(".MOV") ||
                            n.endsWith(".avi") || n.endsWith(".AVI")) {
                            video_path = String(f.path());
                            f.close();
                            break;
                        }
                    }
                    f.close();
                    f = dir.openNextFile();
                }
                dir.close();
            }
            // Escape strings for JSON
            char entry[320];
            snprintf(entry, sizeof(entry),
                     "%s{\"name\":\"%s\",\"path\":\"%s\",\"video_path\":\"%s\"}",
                     first ? "" : ",",
                     names[i].c_str(),
                     sess_path.c_str(),
                     video_path.c_str());
            httpd_resp_send_chunk(req, entry, strlen(entry));
            first = false;
            yield(); // feed watchdog
        }
    }

    httpd_resp_send_chunk(req, "]}", 2);
    httpd_resp_send_chunk(req, nullptr, 0); // end chunked
    return ESP_OK;
}

static esp_err_t session_data_handler(httpd_req_t* req) {
    char query[192] = {0};
    char path[160] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "path", path, sizeof(path)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing path");
    }
    url_decode_inplace(path);
    String requested = String(path);
    requested.trim();
    if (!requested.startsWith(SD_ROOT_DIR) || requested.indexOf("..") >= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid session path");
    }

    auto sessions = scan_sessions(true, requested);
    if (sessions.empty()) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Session not found");
    }

    DynamicJsonDocument doc(16384);
    const SessionEntry& session = sessions.front();
    doc["name"] = session.name;
    doc["path"] = session.path;
    doc["video_path"] = session.video_path;
    doc["image_count"] = session.images.size();
    JsonArray images = doc["images"].to<JsonArray>();
    for (const MediaEntry& image : session.images) {
        JsonObject item = images.add<JsonObject>();
        item["name"] = image.name;
        item["path"] = image.path;
        item["size"] = image.size;
    }
    send_json(req, doc);
    return ESP_OK;
}

static esp_err_t status_handler(httpd_req_t* req) {
    JsonDocument doc;
    doc["wifi_ssid"] = WiFi.SSID();
    doc["wifi_rssi"] = WiFi.RSSI();
    doc["ip"] = WiFi.localIP().toString();
    doc["uptime_ms"] = millis();
    doc["local_time"] = local_time_string();
    doc["camera_ready"] = camera_is_ready();
    doc["camera_init_error"] = camera_last_error();
    doc["camera_resolution"] = camera_resolution_str();
    doc["camera_frame_size"] =
        camera_get_frame_size() == FRAMESIZE_QQVGA ? "QQVGA" :
        camera_get_frame_size() == FRAMESIZE_QVGA ? "QVGA" :
        camera_get_frame_size() == FRAMESIZE_VGA ? "VGA" :
        camera_get_frame_size() == FRAMESIZE_SVGA ? "SVGA" : "QVGA";
    doc["sd_ready"] = sd_storage_is_ready();
    doc["recording"] = sd_storage_is_recording();
    doc["saved_count"] = sd_storage_saved_count();
    doc["session_dir"] = sd_storage_session_dir();
    doc["video_path"] = sd_storage_video_path();
    doc["video_frame_count"] = sd_storage_video_frame_count();
    doc["avi_fps"] = 1000 / sd_storage_get_avi_frame_interval_ms();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["session_count"] = sd_storage_session_count_cached();
    doc["busy"] = g_media_transfer_busy.load();
    add_storage_stats(doc);
    send_json(req, doc);
    return ESP_OK;
}

static esp_err_t stream_handler(httpd_req_t* req) {
    camera_fb_t* fb = nullptr;
    esp_err_t result = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (result != ESP_OK) {
        return result;
    }
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    g_stream_clients.fetch_add(1);
    while (true) {
        fb = camera_snapshot();
        if (fb == nullptr) {
            result = ESP_FAIL;
            break;
        }
        char part_buf[64];
        size_t part_len = snprintf(part_buf, sizeof(part_buf), STREAM_PART, fb->len);
        result = httpd_resp_send_chunk(req, part_buf, part_len);
        if (result == ESP_OK) result = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
        if (result == ESP_OK) result = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        camera_fb_release(fb);
        fb = nullptr;
        if (result != ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(50));  // max ~20 FPS, reduces buffer pressure
    }
    g_stream_clients.fetch_sub(1);
    return result;
}

static esp_err_t snap_handler(httpd_req_t* req) {
    if (g_media_transfer_busy) {
        return send_busy_error(req, "Busy: media transfer in progress");
    }
    camera_fb_t* fb = camera_snapshot();
    if (fb == nullptr) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=snap.jpg");
    esp_err_t result = httpd_resp_send(req, (const char*)fb->buf, fb->len);
    camera_fb_release(fb);
    return result;
}

static esp_err_t capture_handler(httpd_req_t* req) {
    if (g_media_transfer_busy) {
        return send_busy_error(req, "Busy: media transfer in progress");
    }
    if (!sd_storage_is_ready()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD not ready");
    }
    if (!sd_storage_is_recording() && !sd_storage_start_session()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to start session");
    }
    camera_fb_t* fb = camera_snapshot();
    if (fb == nullptr) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    char path[128] = {0};
    bool ok = sd_storage_save_jpeg(fb->buf, fb->len, path, sizeof(path));
    camera_fb_release(fb);
    if (!ok) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    JsonDocument doc;
    doc["ok"] = true;
    doc["path"] = path;
    doc["saved_count"] = sd_storage_saved_count();
    send_json(req, doc);
    return ESP_OK;
}

static esp_err_t media_handler(httpd_req_t* req) {
    if (g_media_transfer_busy) {
        return send_busy_error(req, "Busy: another media transfer is already in progress");
    }

    char query[192] = {0};
    char path[160] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "path", path, sizeof(path)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing path");
    }

    url_decode_inplace(path);
    String requested_path = String(path);
    requested_path.trim();
    if (!requested_path.startsWith(SD_ROOT_DIR) || requested_path.indexOf("..") >= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid path");
    }

    File file = SD.open(requested_path.c_str(), FILE_READ);
    if (!file || file.isDirectory()) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
    }

    g_media_transfer_busy = true;
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    String bname = base_name(requested_path.c_str());
    char disp[128];
    snprintf(disp, sizeof(disp), "attachment; filename=\"%s\"", bname.c_str());
    if (is_jpeg_name(bname.c_str())) {
        httpd_resp_set_type(req, "image/jpeg");
    } else if (bname.endsWith(".mp4") || bname.endsWith(".MP4")) {
        httpd_resp_set_type(req, "video/mp4");
    } else if (bname.endsWith(".mov") || bname.endsWith(".MOV")) {
        httpd_resp_set_type(req, "video/quicktime");
    } else if (bname.endsWith(".avi") || bname.endsWith(".AVI")) {
        httpd_resp_set_type(req, "video/x-msvideo");
    } else {
        httpd_resp_set_type(req, "application/octet-stream");
    }
    httpd_resp_set_hdr(req, "Content-Disposition", disp);

    uint8_t buffer[2048];
    esp_err_t result = ESP_OK;
    while (file.available()) {
        size_t read_len = file.read(buffer, sizeof(buffer));
        if (read_len == 0) {
            break;
        }
        result = httpd_resp_send_chunk(req, (const char*)buffer, read_len);
        if (result != ESP_OK) {
            break;
        }
    }
    file.close();
    if (result == ESP_OK) {
        result = httpd_resp_send_chunk(req, nullptr, 0);
    }
    g_media_transfer_busy = false;
    return result;
}

static esp_err_t camera_config_handler(httpd_req_t* req) {
    if (g_media_transfer_busy) {
        return send_busy_error(req, "Busy: media transfer in progress");
    }
    if (sd_storage_is_recording()) {
        return send_busy_error(req, "Cannot change resolution during recording. Stop recording first.");
    }
    if (g_stream_clients.load() > 0) {
        // JS should have disconnected the stream before calling us; guard for safety
        return send_busy_error(req, "Stream still active. Disconnect preview first.");
    }
    char body[96] = {0};
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing body");
    }
    JsonDocument input;
    if (deserializeJson(input, body, len)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    }
    const char* frame_size = input["frame_size"] | "";
    framesize_t requested = FRAMESIZE_QVGA;
    if (strcmp(frame_size, "QQVGA") == 0) requested = FRAMESIZE_QQVGA;
    else if (strcmp(frame_size, "QVGA") == 0) requested = FRAMESIZE_QVGA;
    else if (strcmp(frame_size, "VGA") == 0) requested = FRAMESIZE_VGA;
    else if (strcmp(frame_size, "SVGA") == 0) requested = FRAMESIZE_SVGA;
    else return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unsupported frame size");

    bool ok = camera_set_frame_size(requested);
    JsonDocument doc;
    doc["ok"] = ok;
    doc["camera_resolution"] = camera_resolution_str();
    doc["camera_frame_size"] = frame_size;
    send_json(req, doc);
    return ok ? ESP_OK : ESP_FAIL;
}

static esp_err_t recording_config_handler(httpd_req_t* req) {
    if (g_media_transfer_busy) {
        return send_busy_error(req, "Busy: media transfer in progress");
    }
    char body[96] = {0};
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing body");
    }
    JsonDocument input;
    if (deserializeJson(input, body, len)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    }
    int avi_fps = input["avi_fps"] | 0;
    if (avi_fps != 2 && avi_fps != 5 && avi_fps != 10) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unsupported FPS");
    }
    sd_storage_set_avi_frame_interval_ms((uint32_t)(1000 / avi_fps));
    JsonDocument doc;
    doc["ok"] = true;
    doc["avi_fps"] = avi_fps;
    send_json(req, doc);
    return ESP_OK;
}

static esp_err_t record_start_handler(httpd_req_t* req) {
    if (g_media_transfer_busy) {
        return send_busy_error(req, "Busy: media transfer in progress");
    }
    JsonDocument doc;
    bool ok = sd_storage_is_recording() || sd_storage_start_session();
    doc["ok"] = ok;
    doc["recording"] = sd_storage_is_recording();
    doc["session_dir"] = sd_storage_session_dir();
    doc["video_path"] = sd_storage_video_path();
    send_json(req, doc);
    return ok ? ESP_OK : ESP_FAIL;
}

static esp_err_t record_stop_handler(httpd_req_t* req) {
    if (g_media_transfer_busy) {
        return send_busy_error(req, "Busy: media transfer in progress");
    }
    sd_storage_stop_session();
    JsonDocument doc;
    doc["ok"] = true;
    doc["recording"] = false;
    doc["saved_count"] = sd_storage_saved_count();
    doc["video_path"] = sd_storage_video_path();
    doc["video_frame_count"] = sd_storage_video_frame_count();
    send_json(req, doc);
    return ESP_OK;
}

static void register_api_routes(httpd_handle_t server) {
    httpd_uri_t root_uri = {.uri = "/", .method = HTTP_GET, .handler = dashboard_handler, .user_ctx = nullptr};
    httpd_uri_t sessions_uri = {.uri = "/sessions", .method = HTTP_GET, .handler = sessions_page_handler, .user_ctx = nullptr};
    httpd_uri_t session_uri = {.uri = "/session", .method = HTTP_GET, .handler = session_page_handler, .user_ctx = nullptr};
    httpd_uri_t sessions_data_uri = {.uri = "/sessions-data", .method = HTTP_GET, .handler = sessions_data_handler, .user_ctx = nullptr};
    httpd_uri_t session_data_uri = {.uri = "/session-data", .method = HTTP_GET, .handler = session_data_handler, .user_ctx = nullptr};
    httpd_uri_t media_redirect_uri = {.uri = "/media", .method = HTTP_GET, .handler = media_redirect_handler, .user_ctx = nullptr};
    httpd_uri_t snap_uri = {.uri = "/snap", .method = HTTP_GET, .handler = snap_handler, .user_ctx = nullptr};
    httpd_uri_t capture_uri = {.uri = "/capture", .method = HTTP_GET, .handler = capture_handler, .user_ctx = nullptr};
    httpd_uri_t status_uri = {.uri = "/status", .method = HTTP_GET, .handler = status_handler, .user_ctx = nullptr};
    httpd_uri_t camera_config_uri = {.uri = "/camera/config", .method = HTTP_POST, .handler = camera_config_handler, .user_ctx = nullptr};
    httpd_uri_t recording_config_uri = {.uri = "/recording/config", .method = HTTP_POST, .handler = recording_config_handler, .user_ctx = nullptr};
    httpd_uri_t record_start_uri = {.uri = "/record/start", .method = HTTP_POST, .handler = record_start_handler, .user_ctx = nullptr};
    httpd_uri_t record_stop_uri = {.uri = "/record/stop", .method = HTTP_POST, .handler = record_stop_handler, .user_ctx = nullptr};

    httpd_register_uri_handler(server, &root_uri);
    httpd_register_uri_handler(server, &sessions_uri);
    httpd_register_uri_handler(server, &session_uri);
    httpd_register_uri_handler(server, &sessions_data_uri);
    httpd_register_uri_handler(server, &session_data_uri);
    httpd_register_uri_handler(server, &media_redirect_uri);
    httpd_register_uri_handler(server, &snap_uri);
    httpd_register_uri_handler(server, &capture_uri);
    httpd_register_uri_handler(server, &status_uri);
    httpd_register_uri_handler(server, &camera_config_uri);
    httpd_register_uri_handler(server, &recording_config_uri);
    httpd_register_uri_handler(server, &record_start_uri);
    httpd_register_uri_handler(server, &record_stop_uri);
}

static void register_media_routes(httpd_handle_t server) {
    httpd_uri_t media_uri = {.uri = "/media", .method = HTTP_GET, .handler = media_handler, .user_ctx = nullptr};
    httpd_register_uri_handler(server, &media_uri);
}

void web_server_start() {
    if (g_stream_server == nullptr) {
        httpd_config_t stream_config = HTTPD_DEFAULT_CONFIG();
        stream_config.server_port = HTTP_STREAM_PORT;
        stream_config.ctrl_port = 32769;
        stream_config.max_uri_handlers = 2;
        stream_config.stack_size = 16384;
        stream_config.max_open_sockets = 2;
        if (httpd_start(&g_stream_server, &stream_config) == ESP_OK) {
            httpd_uri_t stream_uri = {.uri = "/stream", .method = HTTP_GET, .handler = stream_handler, .user_ctx = nullptr};
            httpd_register_uri_handler(g_stream_server, &stream_uri);
        }
    }

    if (g_media_server == nullptr) {
        httpd_config_t media_config = HTTPD_DEFAULT_CONFIG();
        media_config.server_port = HTTP_MEDIA_PORT;
        media_config.ctrl_port = 32770;
        media_config.max_uri_handlers = 2;
        media_config.stack_size = 12288;
        media_config.max_open_sockets = 1;
        if (httpd_start(&g_media_server, &media_config) == ESP_OK) {
            register_media_routes(g_media_server);
        }
    }

    if (g_api_server != nullptr) {
        return;
    }

    httpd_config_t api_config = HTTPD_DEFAULT_CONFIG();
    api_config.server_port = HTTP_SERVER_PORT;
    api_config.max_uri_handlers = 14;
    api_config.stack_size = 12288;
    api_config.max_open_sockets = 3;
    if (httpd_start(&g_api_server, &api_config) != ESP_OK) {
        g_api_server = nullptr;
        return;
    }
    register_api_routes(g_api_server);
}

void web_server_stop() {
    if (g_api_server != nullptr) {
        httpd_stop(g_api_server);
        g_api_server = nullptr;
    }
    if (g_stream_server != nullptr) {
        httpd_stop(g_stream_server);
        g_stream_server = nullptr;
    }
    if (g_media_server != nullptr) {
        httpd_stop(g_media_server);
        g_media_server = nullptr;
    }
}

bool web_server_is_running() {
    return g_api_server != nullptr;
}

bool web_server_media_busy() {
    return g_media_transfer_busy;
}

void web_server_update() {
    if (!sd_storage_is_recording() || g_media_transfer_busy) {
        return;
    }

    unsigned long now = millis();
    camera_fb_t* fb = camera_snapshot();
    if (fb == nullptr) {
        return;
    }

    if (now - g_last_jpeg_record_ms >= JPEG_SAVE_INTERVAL_MS) {
        g_last_jpeg_record_ms = now;
        sd_storage_save_jpeg(fb->buf, fb->len, nullptr, 0);
    }
    if (now - g_last_avi_record_ms >= sd_storage_get_avi_frame_interval_ms()) {
        g_last_avi_record_ms = now;
        sd_storage_append_video_frame(fb->buf, fb->len, (uint32_t)fb->width, (uint32_t)fb->height);
    }
    camera_fb_release(fb);
}
