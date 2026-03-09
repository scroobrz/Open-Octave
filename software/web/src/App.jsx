import { useEffect, useMemo, useRef, useState } from 'react';
import COLORS from '../../shared/colors.json';

function pretty(obj) {
  return JSON.stringify(obj, null, 2);
}

async function readErrorPayload(res) {
  // Best-effort: try JSON first, fallback to text.
  try {
    const data = await res.json();
    if (data && typeof data === 'object') return data;
    return { error: String(data) };
  } catch {
    try {
      const text = await res.text();
      return { error: text || `HTTP ${res.status}` };
    } catch {
      return { error: `HTTP ${res.status}` };
    }
  }
}

async function apiGet(path) {
  const res = await fetch(path);

  // Important: treat non-2xx as failure so caller try/catch works correctly.
  if (!res.ok) {
    const payload = await readErrorPayload(res);
    const msg =
      typeof payload?.error === 'string' && payload.error.trim()
        ? payload.error
        : `GET ${path} failed: ${res.status} ${res.statusText}`;
    throw new Error(msg);
  }

  return res.json();
}

async function apiPost(path) {
  const res = await fetch(path, { method: 'POST' });

  // Important: treat non-2xx as failure so caller try/catch works correctly.
  if (!res.ok) {
    const payload = await readErrorPayload(res);
    const msg =
      typeof payload?.error === 'string' && payload.error.trim()
        ? payload.error
        : `POST ${path} failed: ${res.status} ${res.statusText}`;
    throw new Error(msg);
  }

  return res.json();
}

function Badge({ connected }) {
  const cls = connected ? 'badge badge-online' : 'badge badge-offline';
  const text = connected ? 'Connected' : 'Disconnected';
  return (
    <div className={cls}>
      <span className="badge-dot" />
      {text}
    </div>
  );
}


export default function App() {
  const [tab, setTab] = useState('connect');
  const [uiMode, setUiMode] = useState(() => localStorage.getItem('oo-ui-mode') || 'user'); // 'user' | 'developer'
  const [colorMode, setColorMode] = useState(() => localStorage.getItem('oo-color-mode') || 'default'); // 'default' | 'colorblind'

  // User help modal: finger colour map (right hand)
  const [fingerHelpOpen, setFingerHelpOpen] = useState(false);

  function openFingerHelp() {
    setFingerHelpOpen(true);
  }

  function closeFingerHelp() {
    setFingerHelpOpen(false);
  }

  // Persist developer mode toggle across page reloads.
  useEffect(() => {
    localStorage.setItem('oo-ui-mode', uiMode);
    document.documentElement.dataset.theme = uiMode;
  }, [uiMode]);

  // Persist colourblind mode toggle.
  useEffect(() => {
    localStorage.setItem('oo-color-mode', colorMode);
  }, [colorMode]);

  // Resolve active finger colours based on colour mode.
  const activeFingerColors = useMemo(() => {
    if (colorMode === 'colorblind' && COLORS.alternativePalettes?.colorblind) {
      return COLORS.alternativePalettes.colorblind.fingerColors;
    }
    return COLORS.fingerColors;
  }, [colorMode]);

  // Map canonical hex → display hex for rendering swatches/LEDs.
  const colorDisplayMap = useMemo(() => {
    const map = {};
    for (const finger of COLORS.fingerOrder) {
      const original = COLORS.fingerColors[finger].toUpperCase();
      const display = activeFingerColors[finger].toUpperCase();
      map[original] = display;
    }
    return map;
  }, [activeFingerColors]);

  function displayColor(hex) {
    const clean = String(hex || '').trim().toUpperCase().replace('#', '');
    return colorDisplayMap[clean] || clean;
  }

  // If user switches back to User mode, force them onto safe tabs.
  useEffect(() => {
    if (uiMode === 'user' && tab === 'logs') {
      setTab('connect');
    }
  }, [uiMode, tab]);

  const [health, setHealth] = useState(null);
  const [healthError, setHealthError] = useState('');
    // ============ CONNECT TAB STATE (Transport Selector) ============
  const [transportChoice, setTransportChoice] = useState('WIFI'); // 'WIFI' | 'SERIAL'
  const [wifiIp, setWifiIp] = useState('192.168.4.1');
  const [wifiPort, setWifiPort] = useState(81);
  const [serialPortPath, setSerialPortPath] = useState('');

  const [logs, setLogs] = useState([]);
  const [tail, setTail] = useState(200);
  const [autoLogs, setAutoLogs] = useState(true);
    // ============ LOGS TAB UI CONTROLS ============
  const [logsPrefixedOnly, setLogsPrefixedOnly] = useState(false);
  const [logsSearch, setLogsSearch] = useState('');
  const [logsCopyMsg, setLogsCopyMsg] = useState('');

  const [lastResult, setLastResult] = useState(null);
    // ============ CONTROLLER STATE MIRROR (from backend) ============
  const [ctrlState, setCtrlState] = useState(null);
  const [ctrlStateError, setCtrlStateError] = useState('');


  // ============ SEQUENCES TAB STATE ============

  // ============ SOFTWARE SEQUENCE LIBRARY (SQLite) ============
  const [dbSeqItems, setDbSeqItems] = useState([]);
  const [dbSeqError, setDbSeqError] = useState('');
  const [selectedDbSeqId, setSelectedDbSeqId] = useState('');
  const [selectedDbSeq, setSelectedDbSeq] = useState(null);
  const [dbActionBusy, setDbActionBusy] = useState(false);
  const [selectionStatus, setSelectionStatus] = useState(null);
  const [uploadLog, setUploadLog] = useState([]);
  const [uploadLogVisible, setUploadLogVisible] = useState(false);

  // ============ USER: SEQUENCE DETAILS MODAL ============
  const [seqModalOpen, setSeqModalOpen] = useState(false);
  const [seqModalLoading, setSeqModalLoading] = useState(false);
  const [seqModalError, setSeqModalError] = useState('');
  const [seqModalSeq, setSeqModalSeq] = useState(null); // full sequence object (includes steps)

  // ============ DB CREATE SEQUENCE (paste JSON) ============
  const defaultTemplate = {
    id: '10',
    name: 'My Sequence',
    description: 'Optional description',
    steps: [
      { k: 0, c: COLORS.fingerColors.thumb, d: 300 },
      { k: 1, c: COLORS.fingerColors.index, d: 300 }
    ]
  };
  const [dbCreateJson, setDbCreateJson] = useState(pretty(defaultTemplate));
  const [dbCreateMsg, setDbCreateMsg] = useState('');
  const [dbCreateErr, setDbCreateErr] = useState('');
  const [dbCreateBusy, setDbCreateBusy] = useState(false);

  // ============ CONNECT TAB SYNC ============
  const [syncState, setSyncState] = useState({
    running: false,
    phase: 'idle',       // 'idle' | 'status' | 'seq_list' | 'refresh' | 'complete'
    statusOk: null,      // true/false/null
    seqListOk: null,     // true/false/null
    statusResp: null,
    seqListResp: null,
    error: ''
  });

  async function refreshDbSequences() {
    try {
      setDbSeqError('');
      const data = await apiGet('/api/db/sequences');
      setDbSeqItems(Array.isArray(data?.items) ? data.items : []);
    } catch (e) {
      setDbSeqItems([]);
      setDbSeqError(`Failed to load /api/db/sequences: ${e.message}`);
    }
  }

  async function seedDemoSequences() {
    try {
      setDbActionBusy(true);
      setDbSeqError('');
      const data = await apiPost('/api/db/sequences/seed');
      setLastResult(data);
      await refreshDbSequences();
    } catch (e) {
      setDbSeqError(`Failed to seed demo sequences: ${e.message}`);
    } finally {
      setDbActionBusy(false);
    }
  }

  async function selectSequence(id) {
    if (!id) {
      setSelectedDbSeqId('');
      setSelectedDbSeq(null);
      setSelectionStatus(null);
      return;
    }

    setSelectedDbSeqId(id);
    setDbActionBusy(true);
    setDbSeqError('');

    try {
      const seqData = await apiGet(`/api/db/sequences/${encodeURIComponent(id)}`);
      setSelectedDbSeq(seqData?.item || null);
      const name = seqData?.item?.name || id;

      if (isConnected) {
        const data = await apiPost(`/api/db/sequences/${encodeURIComponent(id)}/upload`);
        setLastResult(data);
        setSelectionStatus({
          sequenceId: id,
          name,
          ok: Boolean(data?.ok),
          steps: data?.sentCount != null ? Math.max(0, data.sentCount - 2) : null,
          error: data?.error || null
        });

        // Capture protocol log for developer mode display
        if (Array.isArray(data?.sent)) {
          setUploadLog(data.sent.map(entry => ({
            line: entry.line || '',
            ok: entry.result === 'ok'
          })));
          setUploadLogVisible(false);
        }

        await refreshHealth();
        await refreshLogs();
        await refreshControllerState();
      } else {
        setSelectionStatus({
          sequenceId: id,
          name,
          ok: false,
          steps: null,
          error: 'Not connected — connect to the device first'
        });
      }
    } catch (e) {
      setDbSeqError(`Failed to select sequence: ${e.message}`);
    } finally {
      setDbActionBusy(false);
    }
  }

  // Normalizer for sequence objects for modal display
  function normalizeSequenceForModal(item) {
    if (!item || typeof item !== 'object') return null;

    // Clone so we don't mutate shared objects.
    const out = { ...item };

    // Steps might be an array already, or a JSON string, or stored under a different key.
    let steps = out.steps;

    if (!steps && out.stepsJson) steps = out.stepsJson;
    if (!steps && out.steps_json) steps = out.steps_json;
    if (!steps && out.sequenceSteps) steps = out.sequenceSteps;

    // If steps is a JSON string, parse it.
    if (typeof steps === 'string') {
      try {
        const parsed = JSON.parse(steps);
        steps = parsed;
      } catch {
        // Leave as-is; we will handle non-array below.
      }
    }

    // Some APIs return { steps: { items: [...] } }.
    if (steps && typeof steps === 'object' && !Array.isArray(steps) && Array.isArray(steps.items)) {
      steps = steps.items;
    }

    // Current DB/API shape: { data: { steps: [...] } }
    if (!steps && out.data && typeof out.data === 'object' && Array.isArray(out.data.steps)) {
      steps = out.data.steps;
    }

    // If `data` is a JSON string, try parsing it and extracting steps.
    if (!steps && typeof out.data === 'string') {
      try {
        const parsedData = JSON.parse(out.data);
        if (parsedData && typeof parsedData === 'object' && Array.isArray(parsedData.steps)) {
          steps = parsedData.steps;
        }
      }  catch (e) {
        // ignore
      }
    }

    // Only accept arrays for display.
    out.steps = Array.isArray(steps) ? steps : [];

    return out;
  }

  async function openSequenceModal(id) {
    if (!id) return;

    setSeqModalOpen(true);
    setSeqModalLoading(true);
    setSeqModalError('');
    setSeqModalSeq(null);

    try {
      const data = await apiGet(`/api/db/sequences/${encodeURIComponent(id)}`);
      const item = data?.item || null;

      if (!item) {
        setSeqModalError('Not found.');
        return;
      }

      setSeqModalSeq(normalizeSequenceForModal(item));
    } catch (e) {
      setSeqModalError(`Failed to load: ${e.message}`);
    } finally {
      setSeqModalLoading(false);
    }
  }

  function closeSequenceModal() {
    setSeqModalOpen(false);
    setSeqModalLoading(false);
    setSeqModalError('');
    setSeqModalSeq(null);
  }

  const HEX_TO_NAME = {
    // Default palette
    '00B4D8': 'Blue',
    '4ECB71': 'Green',
    'FFD700': 'Yellow',
    'FF6B35': 'Orange',
    'E8368F': 'Pink',
    // CB-friendly palette (same names)
    '0072B2': 'Blue',
    '009E73': 'Green',
    'F0E442': 'Yellow',
    'D55E00': 'Orange',
    'CC79A7': 'Pink',
  };

  function hexColorName(hex) {
    const clean = String(hex || '').trim().toUpperCase().replace('#', '');
    return HEX_TO_NAME[clean] || hex;
  }

  const HEX_TO_FINGER = useMemo(() => {
    const map = {};
    for (const finger of COLORS.fingerOrder) {
      const name = finger.charAt(0).toUpperCase() + finger.slice(1);
      // Map both default and active-palette hex values to the finger name.
      map[COLORS.fingerColors[finger].toUpperCase()] = name;
      map[activeFingerColors[finger].toUpperCase()] = name;
    }
    return map;
  }, [activeFingerColors]);

  function renderColorSwatch(hex) {
    const clean = String(hex || '').trim();
    const mapped = displayColor(clean);
    const css = mapped ? `#${mapped.replace('#', '')}` : '#000000';
    return (
      <span
        className="swatch"
        title={mapped || 'n/a'}
        style={{ backgroundColor: css }}
      />
    );
  }

  async function saveDbSequenceFromJson() {
    try {
      setDbCreateBusy(true);
      setDbCreateMsg('');
      setDbCreateErr('');

      let payload;
      try {
        payload = JSON.parse(dbCreateJson);
      } catch {
        setDbCreateErr('Invalid JSON. Please fix formatting and try again.');
        return;
      }

      // Basic client-side validation (server also validates)
      if (!payload || typeof payload !== 'object') {
        setDbCreateErr('JSON must be an object.');
        return;
      }
      if (!payload.id || !String(payload.id).trim()) {
        setDbCreateErr('Missing "id".');
        return;
      }
      if (!payload.name || !String(payload.name).trim()) {
        setDbCreateErr('Missing "name".');
        return;
      }
      if (!Array.isArray(payload.steps)) {
        setDbCreateErr('Missing "steps" array.');
        return;
      }

      const res = await fetch('/api/db/sequences', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
      });

      const data = await res.json();
      setLastResult(data);

      if (!data?.ok) {
        setDbCreateErr(data?.error || 'Failed to save sequence.');
        return;
      }

      setDbCreateMsg(`Saved sequence: ${payload.id}`);
      await refreshDbSequences();

      // Auto-select the saved sequence
      const newId = String(payload.id).trim();
      await selectSequence(newId);
    } catch (e) {
      setDbCreateErr(e.message);
    } finally {
      setDbCreateBusy(false);
    }
  }

  async function runSync() {
    setSyncState((s) => ({
      ...s,
      running: true,
      phase: 'status',
      statusOk: null,
      seqListOk: null,
      statusResp: null,
      seqListResp: null,
      error: ''
    }));

    try {
      // Step 1: Status query (if firmware/controller supports it)
      let statusResp = null;
      let statusOk = false;

      const isApiSuccess = (obj) => {
        // If backend returns a structured { ok: boolean } or { success: boolean }, respect it.
        // Otherwise, treat presence of `error` as failure and assume best-effort success.
        if (!obj || typeof obj !== 'object') return true;
        if (obj.error) return false;
        if (Object.prototype.hasOwnProperty.call(obj, 'ok')) return Boolean(obj.ok);
        if (Object.prototype.hasOwnProperty.call(obj, 'success')) return Boolean(obj.success);
        return true;
      };

      try {
        statusResp = await apiGet('/api/status');
        statusOk = isApiSuccess(statusResp);
      } catch (e) {
        statusResp = { error: e.message };
        statusOk = false;
      }

      // Step 2: Sequence list (if firmware/controller supports it)
      setSyncState((s) => ({ ...s, phase: 'seq_list' }));
      let seqListResp = null;
      let seqListOk = false;
      try {
        seqListResp = await apiGet('/api/seq/list');
        seqListOk = isApiSuccess(seqListResp);
      } catch (e) {
        seqListResp = { error: e.message };
        seqListOk = false;
      }

      setSyncState((s) => ({
        ...s,
        running: false,
        statusOk,
        seqListOk,
        statusResp,
        seqListResp
      }));

      // Refresh supporting views
      setSyncState((s) => ({ ...s, phase: 'refresh' }));
      await refreshHealth();
      await refreshLogs();
      await refreshControllerState();
      setSyncState((s) => ({ ...s, phase: 'complete' }));

      // Keep global lastResult helpful too
      setLastResult({
        sync: true,
        statusOk,
        seqListOk
      });
    } catch (e) {
      setSyncState((s) => ({ ...s, running: false, phase: 'idle', error: e.message }));
    }
  }

  const timerRef = useRef(null);
    const stateTimerRef = useRef(null);

  const isConnected = useMemo(() => {
    // Prefer backend state mirror, fallback to /api/health.
    if (ctrlState && typeof ctrlState.connected === 'boolean') {
      return ctrlState.connected;
    }

    if (!health) return false;
    return Boolean(health?.wifi?.connected || health?.serial?.open);
  }, [ctrlState, health]);

  const controlsDisabled = !isConnected;

  const displayedLogs = useMemo(() => {
    let items = Array.isArray(logs) ? logs : [];

    if (logsPrefixedOnly) {
      items = items.filter((x) => {
        const m = String(x.message || '');
        return (
          m.startsWith('ACK ') ||
          m.startsWith('STATUS ') ||
          m.startsWith('EVT ') ||
          m.startsWith('ERR ')
        );
      });
    }

    const q = String(logsSearch || '').trim().toLowerCase();
    if (q) {
      items = items.filter((x) => {
        const line = `[${x.ts}] ${x.source}: ${x.message}`.toLowerCase();
        return line.includes(q);
      });
    }

    return items;
  }, [logs, logsPrefixedOnly, logsSearch]);

  async function copyDisplayedLogs() {
    try {
      const text = displayedLogs.length
        ? displayedLogs.map((x) => `[${x.ts}] ${x.source}: ${x.message}`).join('\n')
        : '';

      if (!text) {
        setLogsCopyMsg('Nothing to copy.');
        return;
      }

      await navigator.clipboard.writeText(text);
      setLogsCopyMsg(`Copied ${displayedLogs.length} lines.`);
      setTimeout(() => setLogsCopyMsg(''), 2000);
    } catch (e) {
      setLogsCopyMsg(`Copy failed: ${e.message}`);
    }
  }

  function clearUiLogs() {
    setLogs([]);
    setLogsCopyMsg('Cleared UI logs (backend logs unchanged).');
    setTimeout(() => setLogsCopyMsg(''), 2000);
  }

  async function refreshHealth() {
    try {
      setHealthError('');
      const data = await apiGet('/api/health');
      setHealth(data);
    } catch (e) {
      setHealth(null);
      setHealthError(`Failed to load /api/health: ${e.message}`);
    }
  }

  async function refreshLogs() {
    const safeTail = Number.isFinite(tail)
      ? Math.max(1, Math.min(500, Math.floor(tail)))
      : 200;

    try {
      const data = await apiGet(`/api/logs?tail=${safeTail}`);
      setLogs(data.items || []);
    } catch (e) {
      setLogs([]);
    }
  }

    async function refreshControllerState() {
    try {
      setCtrlStateError('');
      const data = await apiGet('/api/state');
      setCtrlState(data?.state || null);
    } catch (e) {
      setCtrlState(null);
      setCtrlStateError(`Failed to load /api/state: ${e.message}`);
    }
  }

  function startAutoState() {
    stopAutoState();
    stateTimerRef.current = setInterval(() => {
      refreshControllerState();
    }, 1000);
  }

  function stopAutoState() {
    if (stateTimerRef.current) {
      clearInterval(stateTimerRef.current);
      stateTimerRef.current = null;
    }
  }

    async function connectSelectedTransport() {
    try {
      let body = {};

      if (transportChoice === 'WIFI') {
        body = {
          transport: 'WIFI',
          esp32Ip: wifiIp,
          wsPort: Number(wifiPort),
        };
      } else {
        body = {
          transport: 'SERIAL',
          serialPort: serialPortPath,
        };
      }

      const res = await fetch('/api/connect', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
      });

      const data = await res.json();
      setLastResult(data);

      await refreshHealth();
      await refreshLogs();
      await refreshControllerState();
    } catch (e) {
      setLastResult({ error: e.message });
    }
  }

  async function disconnectAll() {
    try {
      const res = await fetch('/api/disconnect', { method: 'POST' });
      const data = await res.json();
      setLastResult(data);

      await refreshHealth();
      await refreshLogs();
      await refreshControllerState();
    } catch (e) {
      setLastResult({ error: e.message });
    }
  }

  function startAutoLogs() {
    stopAutoLogs();
    timerRef.current = setInterval(() => {
      refreshLogs();
    }, 1000);
  }

  function stopAutoLogs() {
    if (timerRef.current) {
      clearInterval(timerRef.current);
      timerRef.current = null;
    }
  }

  async function runCommand(kind, data) {
    try {
      let result;

      if (kind === 'seq') {
        // Firmware v5: start commands include mode (guided/teaching)
        const params = new URLSearchParams({ cmd: data.action });
        if (data.mode) params.set('mode', data.mode);
        result = await apiPost(`/api/seq/control?${params}`);
      } else if (kind === 'test') {
        result = await apiPost(`/api/test?target=${encodeURIComponent(data.target)}`);
      } else if (kind === 'currentSeq') {
        result = await apiGet('/api/seq/list');
      } else if (kind === 'status') {
        result = await apiGet('/api/status');
      } else {
        result = { error: `Unknown command kind: ${kind}` };
      }

      setLastResult(result);

      // After sending a command, refresh health and logs
      await refreshHealth();
      await refreshLogs();
      await refreshControllerState();
    } catch (e) {
      setLastResult({ error: e.message });
    }
  }

  useEffect(() => {
    refreshHealth();
    refreshLogs();
    refreshControllerState();
    refreshDbSequences();

    startAutoLogs();
    startAutoState();

    return () => {
      stopAutoLogs();
      stopAutoState();
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => {
    if (autoLogs) startAutoLogs();
    else stopAutoLogs();
    return () => {};
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [autoLogs, tail]);

  return (
    <div className="app">
      <aside className="sidebar">
        <div className="brand">
          <img src="/open-octave-logo.png" alt="Open Octave" className="brand-logo" />
          <div className="brand-text">
            <div className="brand-title">Open Octave</div>
            <div className="brand-subtitle">
              {uiMode === 'user' ? 'User Interface' : 'Developer Interface'}
            </div>
          </div>

          <div className="btn-row" style={{ justifyContent: 'center', marginTop: 10 }}>
            <button
              className={uiMode === 'user' ? 'btn' : 'btn btn-secondary'}
              type="button"
              onClick={() => setUiMode('user')}
            >
              User
            </button>
            <button
              className={uiMode === 'developer' ? 'btn' : 'btn btn-secondary'}
              type="button"
              onClick={() => setUiMode('developer')}
            >
              Developer
            </button>
          </div>

        </div>

        <nav className="nav">
          <button
            className={tab === 'connect' ? 'nav-btn is-active' : 'nav-btn'}
            onClick={() => setTab('connect')}
          >
            Connect
          </button>
          <button
            className={tab === 'play' ? 'nav-btn is-active' : 'nav-btn'}
            onClick={() => setTab('play')}
          >
            Controls
          </button>
          {uiMode === 'developer' && (
            <button
              className={tab === 'logs' ? 'nav-btn is-active' : 'nav-btn'}
              onClick={() => setTab('logs')}
            >
              Logs
            </button>
          )}
        </nav>

        <div className="sidebar-footer">
          <Badge connected={isConnected} />
        </div>
      </aside>

      <main className="main">
        {tab === 'connect' && (
          <section className="panel">
            <h1>Connect</h1>

            <div className="card">
              <h2>Transport</h2>

              <div className="row">
                <div className="label">Select transport</div>
                <div className="btn-row">
                  <button
                    className={transportChoice === 'WIFI' ? 'btn' : 'btn btn-secondary'}
                    onClick={() => setTransportChoice('WIFI')}
                    type="button"
                  >
                    Wi-Fi
                  </button>
                  <button
                    className={transportChoice === 'SERIAL' ? 'btn' : 'btn btn-secondary'}
                    onClick={() => setTransportChoice('SERIAL')}
                    type="button"
                  >
                    Serial
                  </button>
                </div>
              </div>

              {transportChoice === 'WIFI' ? (
                <div className="row mt">
                  <input
                    className="input"
                    value={wifiIp}
                    onChange={(e) => setWifiIp(e.target.value)}
                    placeholder="ESP32 IP (e.g. 192.168.4.1 or 127.0.0.1 for mock)"
                  />
                  <input
                    className="input input-small"
                    type="number"
                    value={wifiPort}
                    onChange={(e) => setWifiPort(e.target.value)}
                    placeholder="Port"
                  />
                </div>
              ) : (
                <div className="row mt">
                  <input
                    className="input"
                    value={serialPortPath}
                    onChange={(e) => setSerialPortPath(e.target.value)}
                    placeholder="Serial port path (e.g. /dev/cu.usbmodemXXXX)"
                  />
                </div>
              )}

              <div className="row mt">
                <div className="pill-row">
                  <span className={isConnected ? 'pill pill-green' : 'pill pill-coral'}>
                    {isConnected ? 'Connected' : 'Not connected'}
                  </span>

                  {uiMode === 'developer' && (
                    <>
                      <span className="pill pill-teal">Active: {health?.mode || 'UNKNOWN'}</span>
                      <span className="pill pill-gold">WS: {health?.wifi?.target || 'n/a'}</span>
                      <span className="pill pill-muted">Serial: {health?.serial?.port || 'n/a'}</span>
                    </>
                  )}
                </div>

                <div className="btn-row">
                  <button className="btn btn-green" onClick={connectSelectedTransport} type="button">
                    Connect
                  </button>
                  <button className="btn btn-coral" onClick={disconnectAll} type="button">
                    Disconnect
                  </button>
                  <button className="btn btn-secondary" onClick={refreshHealth} type="button">
                    Refresh
                  </button>
                </div>
              </div>

              {healthError ? <pre className="pre">{healthError}</pre> : null}
            </div>

            {uiMode === 'developer' && (
              <div className="card">
                <h2>Sync</h2>

                <div className="row">
                  <div className="pill-row">
                    <span className={syncState.statusOk === null ? 'pill pill-muted' : syncState.statusOk ? 'pill pill-green' : 'pill pill-coral'}>
                      Status: {syncState.statusOk === null ? '—' : syncState.statusOk ? 'OK' : 'FAILED'}
                    </span>
                    <span className={syncState.seqListOk === null ? 'pill pill-muted' : syncState.seqListOk ? 'pill pill-green' : 'pill pill-coral'}>
                      Seq (c): {syncState.seqListOk === null ? '—' : syncState.seqListOk ? 'OK' : 'FAILED'}
                    </span>
                    <span className="pill pill-gold">Phase: {syncState.phase}</span>
                  </div>

                  <div className="btn-row">
                    <button className="btn" onClick={runSync} disabled={!isConnected || syncState.running} type="button">
                      {syncState.running ? 'Syncing…' : 'Run Sync'}
                    </button>
                  </div>
                </div>

                {syncState.error ? <pre className="pre">{syncState.error}</pre> : null}
              </div>
            )}

            {uiMode === 'developer' && (
              <details className="diagnostics">
                <summary>Diagnostics</summary>

                <div className="card">
                  <h2>Quick Diagnostics</h2>
                  <div className="btn-row">
                    <button className="btn btn-secondary" disabled={!isConnected} onClick={() => runCommand('status', {})}>
                      Status
                    </button>
                    <button className="btn btn-secondary" disabled={!isConnected} onClick={() => runCommand('currentSeq', {})}>
                      Current seq (c)
                    </button>
                    <button className="btn btn-secondary" disabled={!isConnected} onClick={() => refreshHealth()}>
                      Refresh Health
                    </button>
                    <button className="btn btn-secondary" disabled={!isConnected} onClick={() => refreshLogs()}>
                      Refresh Logs
                    </button>
                    <button className="btn btn-secondary" disabled={!isConnected} onClick={() => refreshControllerState()}>
                      Refresh State
                    </button>
                  </div>

                  <div className="btn-row mt">
                    <button className="btn btn-coral" onClick={disconnectAll} type="button">
                      Disconnect
                    </button>
                    <button
                      className="btn btn-secondary"
                      onClick={async () => {
                        try {
                          const data = await fetch('/api/state/reset', { method: 'POST' }).then((r) => r.json());
                          setLastResult(data);
                          await refreshControllerState();
                        } catch (e) {
                          setLastResult({ error: e.message });
                        }
                      }}
                      type="button"
                    >
                      Reset State Mirror
                    </button>
                  </div>

                  <div className="hint">
                    Reset clears the in-memory controller mirror (lastCommand/lastAck counters) without restarting Node.
                  </div>
                </div>

                <div className="card">
                  <h2>Controller State</h2>

                  <div className="row">
                    <div className="label">
                      Connected: <b>{String(ctrlState?.connected ?? false)}</b>
                      {'  '}|{'  '}
                      Transport: <b>{ctrlState?.transport || 'n/a'}</b>
                      {'  '}|{'  '}
                      Last cmd: <b>{ctrlState?.lastCommand?.cmd || 'n/a'}</b>
                    </div>

                    <div className="btn-row">
                      <button
                        className="btn btn-secondary"
                        onClick={refreshControllerState}
                        type="button"
                      >
                        Refresh
                      </button>
                    </div>
                  </div>

                  {ctrlStateError ? (
                    <pre className="pre">{ctrlStateError}</pre>
                  ) : (
                    <pre className="pre">{ctrlState ? pretty(ctrlState) : '(not loaded)'}</pre>
                  )}
                </div>

                <div className="card">
                  <h2>Raw Health</h2>
                  {healthError ? (
                    <pre className="pre">{healthError}</pre>
                  ) : (
                    <pre className="pre">{health ? pretty(health) : '(not loaded)'}</pre>
                  )}
                </div>

                <div className="card">
                  <h2>Sync Responses</h2>

                  <div className="row">
                    <div className="label">/api/status response</div>
                  </div>
                  <pre className="pre">{syncState.statusResp ? pretty(syncState.statusResp) : '(not run)'}</pre>

                  <div className="row mt">
                    <div className="label">/api/seq/list response</div>
                  </div>
                  <pre className="pre">{syncState.seqListResp ? pretty(syncState.seqListResp) : '(not run)'}</pre>
                </div>

                <div className="card">
                  <h2>Last API Result</h2>
                  <pre className="pre">{lastResult ? pretty(lastResult) : '(none yet)'}</pre>
                </div>
              </details>
            )}
          </section>
        )}

        {tab === 'play' && (
          <section className="panel">
            <div className="panel-top-row">
              <h1>Controls</h1>

              <button
                  className="btn btn-secondary finger-help-btn"
                  type="button"
                  onClick={openFingerHelp}
                >
                  Finger colours
                </button>
            </div>

            <div className="grid">
              <div className="card card-accent-teal">
                <h2>{uiMode === 'user' ? 'Play' : 'Sequence Control'}</h2>

                {selectedDbSeq ? (
                  <div className="label" style={{ marginBottom: 10 }}>
                    {uiMode === 'user' ? 'Current song' : 'Current sequence'}: <b>{selectedDbSeq.name}</b>
                  </div>
                ) : (
                  <div className="label" style={{ marginBottom: 10 }}>
                    {uiMode === 'user'
                      ? 'No song prepared yet \u2014 select one below'
                      : 'No sequence loaded yet \u2014 select one below'}
                  </div>
                )}

                <div className="btn-row">
                  <button className="btn btn-green" disabled={controlsDisabled} onClick={() => runCommand('seq', { action: 'start', mode: 'guided' })}>
                    {uiMode === 'user' ? 'Practice' : 'Start Guided'}
                  </button>
                  <button className="btn" disabled={controlsDisabled} onClick={() => runCommand('seq', { action: 'start', mode: 'teaching' })}>
                    {uiMode === 'user' ? 'Watch & Learn' : 'Start Teaching'}
                  </button>
                  <button className="btn btn-coral" disabled={controlsDisabled} onClick={() => runCommand('seq', { action: 'stop' })}>
                    Stop
                  </button>
                </div>

                {uiMode === 'user' && (
                  <div className="hint">
                    <b>Practice</b>: Follow the LEDs and press the right keys.<br />
                    <b>Watch & Learn</b>: The keyboard plays the song for you.
                  </div>
                )}
              </div>

              {uiMode === 'user' && (() => {
                const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
                const IS_BLACK = [false, true, false, true, false, false, true, false, true, false, true, false];

                // Build per-key data from the selected sequence
                const steps = selectedDbSeq?.data?.steps || [];
                const keyHits = new Array(12).fill(0);
                const keyColors = new Array(12).fill(null);
                for (const s of steps) {
                  const k = s?.k;
                  if (k >= 0 && k < 12) {
                    keyHits[k]++;
                    if (!keyColors[k]) keyColors[k] = s.c;
                  }
                }
                const maxHits = Math.max(...keyHits, 1);

                return (
                  <div className="card card-accent-green keyboard-vis-card">
                    <h2>Keyboard Visualisation</h2>

                    <div className="keyboard-vis">
                      {/* White keys layer */}
                      {NOTE_NAMES.map((note, i) => {
                        if (IS_BLACK[i]) return null;
                        const active = keyHits[i] > 0;
                        const color = keyColors[i] ? `#${displayColor(keyColors[i])}` : null;
                        const intensity = keyHits[i] / maxHits;

                        return (
                          <div
                            key={i}
                            className={`kb-key kb-white${active ? ' kb-active' : ''}`}
                            style={active ? {
                              '--kb-glow': color,
                              '--kb-intensity': intensity,
                            } : undefined}
                          >
                            {active && (
                              <span className="kb-led" style={{ backgroundColor: color }} />
                            )}
                            <span className="kb-note">{note}</span>
                            {active && <span className="kb-hits">{keyHits[i]}x</span>}
                          </div>
                        );
                      })}

                      {/* Black keys layer (absolutely positioned) */}
                      {NOTE_NAMES.map((note, i) => {
                        if (!IS_BLACK[i]) return null;
                        const active = keyHits[i] > 0;
                        const color = keyColors[i] ? `#${displayColor(keyColors[i])}` : null;
                        const intensity = keyHits[i] / maxHits;

                        // Position black keys between their neighbouring white keys.
                        // White key indices: C=0, D=1, E=2, F=3, G=4, A=5, B=6
                        // Black key positions (centred on the gap between white keys):
                        //   C#(1)→between 0-1, D#(3)→1-2, F#(6)→3-4, G#(8)→4-5, A#(10)→5-6
                        const BLACK_OFFSETS = { 1: 0.5, 3: 1.5, 6: 3.5, 8: 4.5, 10: 5.5 };
                        const offset = BLACK_OFFSETS[i];
                        const leftPercent = ((offset + 0.5) / 7) * 100;

                        return (
                          <div
                            key={i}
                            className={`kb-key kb-black${active ? ' kb-active' : ''}`}
                            style={{
                              left: `${leftPercent}%`,
                              ...(active ? { '--kb-glow': color, '--kb-intensity': intensity } : {}),
                            }}
                          >
                            {active && (
                              <span className="kb-led" style={{ backgroundColor: color }} />
                            )}
                            <span className="kb-note">{note}</span>
                            {active && <span className="kb-hits">{keyHits[i]}x</span>}
                          </div>
                        );
                      })}
                    </div>

                    {steps.length > 0 ? (
                      <div className="hint">
                        <b>{steps.length}</b> steps across <b>{keyHits.filter(h => h > 0).length}</b> keys
                      </div>
                    ) : (
                      <div className="hint">Select a song to see which keys are used.</div>
                    )}
                  </div>
                );
              })()}

              {uiMode === 'developer' && (
                <div className="card card-accent-green">
                  <h2>Tests</h2>
                  <div className="btn-row">
                    <button className="btn" disabled={controlsDisabled} onClick={() => runCommand('test', { target: 'leds' })}>
                      Test LEDs
                    </button>
                    <button className="btn" disabled={controlsDisabled} onClick={() => runCommand('test', { target: 'servos' })}>
                      Test Servos
                    </button>
                  </div>
                </div>
              )}
            </div>

            <div className="card card-accent-gold">
              <h2>{uiMode === 'user' ? 'Select Song' : 'Select Sequence'}</h2>

              <div className="row">
                <div className="label">
                  {uiMode === 'user'
                    ? 'Choose a song to prepare on the device.'
                    : 'Choose a sequence to load onto the device.'}
                </div>

                {uiMode === 'developer' && (
                  <div className="btn-row">
                    <button className="btn btn-secondary" onClick={refreshDbSequences} type="button" disabled={dbActionBusy}>
                      Refresh DB
                    </button>
                    <button className="btn btn-coral" onClick={seedDemoSequences} type="button" disabled={dbActionBusy}>
                      Seed demos
                    </button>
                  </div>
                )}
              </div>

              {dbSeqError ? <pre className="pre">{dbSeqError}</pre> : null}

              <div className="row mt">
                <select
                  className="input"
                  value={selectedDbSeqId}
                  disabled={dbActionBusy}
                  onChange={(e) => selectSequence(e.target.value)}
                >
                  <option value="">(none)</option>
                  {dbSeqItems.map((it) => (
                    <option key={it.id} value={it.id}>
                      {it.name}{uiMode === 'developer' ? ` (${it.id})` : ''}{typeof it.stepCount === 'number' ? ` • ${it.stepCount} steps` : ''}
                    </option>
                  ))}
                </select>

                {uiMode === 'developer' && (
                  <button
                    className="btn btn-secondary"
                    type="button"
                    disabled={!selectedDbSeqId || dbActionBusy}
                    onClick={() => setLastResult(selectedDbSeq || { note: 'No sequence loaded yet' })}
                  >
                    Preview JSON
                  </button>
                )}
              </div>

              {selectionStatus ? (
                <div className="hint mt" style={{ borderLeftColor: selectionStatus.ok ? 'var(--green)' : 'var(--secondary)' }}>
                  {selectionStatus.ok
                    ? `Selected '${selectionStatus.name}' (${selectionStatus.steps ?? '?'} steps) — ready to play`
                    : `${selectionStatus.name}: ${selectionStatus.error || 'FAILED'}`}
                </div>
              ) : (
                <div className="hint mt">
                  {uiMode === 'user'
                    ? 'After selecting a song, wait for "Ready to play".'
                    : 'Tip: This uses POST /api/db/sequences/:id/upload under the hood.'}
                </div>
              )}

              {uiMode === 'developer' && uploadLog.length > 0 && (
                <>
                  <button className="upload-log-toggle" type="button" onClick={() => setUploadLogVisible(v => !v)}>
                    {uploadLogVisible ? 'Hide' : 'Show'} Upload Protocol Log ({uploadLog.length} lines)
                  </button>
                  {uploadLogVisible && (
                    <div className="upload-log">
                      {uploadLog.map((entry, i) => (
                        <div key={i} className="upload-log-line">
                          <span>{entry.line}</span>
                          <span style={{ color: entry.ok ? 'var(--green)' : 'var(--secondary)' }}>{entry.ok ? '✓' : '✗'}</span>
                        </div>
                      ))}
                    </div>
                  )}
                </>
              )}
            </div>

            {uiMode === 'user' && (
              <div className="card card-accent-coral">
                <h2>Available Songs</h2>

                <div className="row">
                  <div className="label">
                    {dbSeqItems.length} song{dbSeqItems.length !== 1 ? 's' : ''} available
                  </div>
                </div>

                {dbSeqItems.length > 0 ? (
                  <table className="seq-table">
                    <thead>
                      <tr>
                        <th>Name</th>
                        <th>ID</th>
                        <th>Steps</th>
                      </tr>
                    </thead>
                    <tbody>
                      {dbSeqItems.map((it) => (
                        <tr
                          key={it.id}
                          className={
                            it.id === selectedDbSeqId
                              ? 'seq-row-active seq-row-clickable'
                              : 'seq-row-clickable'
                          }
                          onClick={() => openSequenceModal(it.id)}
                          role="button"
                          tabIndex={0}
                          onKeyDown={(e) => {
                            if (e.key === 'Enter' || e.key === ' ') openSequenceModal(it.id);
                          }}
                        >
                          <td>{it.name}</td>
                          <td>{it.id}</td>
                          <td>{typeof it.stepCount === 'number' ? it.stepCount : '—'}</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                ) : (
                  <div className="hint mt">
                    No songs in the library yet.
                  </div>
                )}

                <div className="hint">
                  This list reflects the internal song library stored in the system.
                </div>
              </div>
            )}

            {uiMode === 'developer' && (
              <div className="card card-accent-coral">
                <h2>Available Sequences</h2>

                <div className="row">
                  <div className="label">{dbSeqItems.length} sequence{dbSeqItems.length !== 1 ? 's' : ''} in library</div>
                  <div className="btn-row">
                    <button className="btn btn-secondary" onClick={refreshDbSequences} type="button" disabled={dbActionBusy}>
                      Refresh
                    </button>
                  </div>
                </div>

                {dbSeqItems.length > 0 ? (
                  <table className="seq-table">
                    <thead>
                      <tr>
                        <th>Name</th>
                        <th>ID</th>
                        <th>Steps</th>
                      </tr>
                    </thead>
                    <tbody>
                      {dbSeqItems.map((it) => (
                        <tr
                          key={it.id}
                          className={
                            it.id === selectedDbSeqId
                              ? 'seq-row-active seq-row-clickable'
                              : 'seq-row-clickable'
                          }
                          onClick={() => openSequenceModal(it.id)}
                          role="button"
                          tabIndex={0}
                          onKeyDown={(e) => {
                            if (e.key === 'Enter' || e.key === ' ') openSequenceModal(it.id);
                          }}
                        >
                          <td>{it.name}</td>
                          <td>{it.id}</td>
                          <td>{typeof it.stepCount === 'number' ? it.stepCount : '—'}</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                ) : (
                  <div className="hint mt">No sequences yet. Use <b>Seed demos</b> above or create one with the JSON editor below.</div>
                )}

                <div className="hint">
                  Sequences are stored in the controller's SQLite database and uploaded to the device via the serial protocol.
                </div>
              </div>
            )}

            {uiMode === 'developer' && (
              <div className="card card-accent-magenta">
                <h2>JSON Sequence Editor</h2>

                <div className="row">
                  <div className="label">
                    Paste JSON and save to the sequence library. Selecting it will load it onto the device.
                  </div>

                  <div className="btn-row">
                    <button className="btn" type="button" disabled={dbCreateBusy} onClick={saveDbSequenceFromJson}>
                      {dbCreateBusy ? 'Saving…' : 'Save to DB'}
                    </button>
                    <button
                      className="btn btn-secondary"
                      type="button"
                      disabled={dbCreateBusy}
                      onClick={() => {
                        setDbCreateMsg('');
                        setDbCreateErr('');
                        setDbCreateJson(pretty(defaultTemplate));
                      }}
                    >
                      Reset template
                    </button>
                  </div>
                </div>

                {dbCreateErr ? <pre className="pre">{dbCreateErr}</pre> : null}
                {dbCreateMsg ? <pre className="pre">{dbCreateMsg}</pre> : null}

                <textarea
                  className="input"
                  style={{ minHeight: 240, fontFamily: 'var(--font-mono)' }}
                  value={dbCreateJson}
                  onChange={(e) => setDbCreateJson(e.target.value)}
                />

                <div className="hint">
                  Required fields: <code>id</code> (numeric), <code>name</code>, <code>steps</code> (array of step objects).
                  Steps: <code>k</code> (key index 0–11), <code>c</code> (brand hex color — see palette), <code>d</code> (duration ms).<br />
                  Allowed colours: {COLORS.fingerOrder.map(f => hexColorName(COLORS.fingerColors[f])).join(', ')}
                </div>
              </div>
            )}

            {fingerHelpOpen && (
            <div className="modal-overlay" onClick={closeFingerHelp}>
              <div className="modal" onClick={(e) => e.stopPropagation()}>
                <div className="modal-header">
                  <div>
                    <div className="modal-title">Finger colour guide (right hand)</div>
                    <div className="modal-subtitle">Match the LED colour to the finger to press.</div>
                  </div>

                  <button className="btn btn-secondary" type="button" onClick={closeFingerHelp}>
                    Close
                  </button>
                </div>

                <div className="card" style={{ marginBottom: 0 }}>
                  <h2>Right hand</h2>

                  <div className="finger-map-wrap">
                    <svg
                      className="finger-map"
                      viewBox="0 0 520 260"
                      xmlns="http://www.w3.org/2000/svg"
                      role="img"
                      aria-label="Right hand finger colour map"
                    >
                      {/* Thumb */}
                      <circle cx="130" cy="185" r="18" fill={`#${activeFingerColors.thumb}`} />

                      {/* Index */}
                      <circle cx="175" cy="60" r="18" fill={`#${activeFingerColors.index}`} />

                      {/* Middle */}
                      <circle cx="235" cy="48" r="18" fill={`#${activeFingerColors.middle}`} />

                      {/* Ring */}
                      <circle cx="295" cy="60" r="18" fill={`#${activeFingerColors.ring}`} />

                      {/* Pinky */}
                      <circle cx="345" cy="78" r="18" fill={`#${activeFingerColors.pinky}`} />
                    </svg>
                  </div>

                  <div className="hint">
                    Tip: In Guided mode, wait for the LED colour, then press the matching finger.
                  </div>

                  <label className="cb-toggle" style={{ marginTop: 12 }}>
                    <input
                      type="checkbox"
                      checked={colorMode === 'colorblind'}
                      onChange={(e) => setColorMode(e.target.checked ? 'colorblind' : 'default')}
                    />
                    <span className="cb-toggle-track">
                      <span className="cb-toggle-knob" />
                    </span>
                    <span className="cb-toggle-label">Colourblind-friendly</span>
                  </label>
                </div>
              </div>
            </div>
          )}

          </section>
        )}

        {uiMode === 'developer' && tab === 'logs' && (
          <section className="panel">
            <h1>Logs</h1>

            <div className="card">
              <div className="row">
                <div className="row-left">
                  <button className="btn" onClick={refreshLogs}>
                    Refresh
                  </button>
                  <button
                    className="btn btn-secondary"
                    onClick={() => setAutoLogs((v) => !v)}
                  >
                    Auto: {autoLogs ? 'ON' : 'OFF'}
                  </button>
                  <button className="btn btn-secondary" onClick={copyDisplayedLogs}>
                    Copy
                  </button>
                  <button className="btn btn-secondary" onClick={clearUiLogs}>
                    Clear UI
                  </button>
                </div>

                <div className="row-right">
                  <div className="label">Tail</div>
                  <input
                    className="input input-small"
                    type="number"
                    min="1"
                    max="500"
                    value={tail}
                    onChange={(e) => setTail(Number(e.target.value))}
                  />

                  <div className="label">Search</div>
                  <input
                    className="input"
                    value={logsSearch}
                    onChange={(e) => setLogsSearch(e.target.value)}
                    placeholder="Find text in logs"
                  />

                  <button
                    className={logsPrefixedOnly ? 'btn' : 'btn btn-secondary'}
                    onClick={() => setLogsPrefixedOnly((v) => !v)}
                    type="button"
                  >
                    Prefixed only
                  </button>
                </div>
              </div>

              <div className="hint">
                Showing <b>{displayedLogs.length}</b> lines (filtered).
                {logsCopyMsg ? `  •  ${logsCopyMsg}` : ''}
              </div>

              <pre className="pre pre-logs">
                {displayedLogs.length
                  ? displayedLogs
                      .map((x) => `[${x.ts}] ${x.source}: ${x.message}`)
                      .join('\n')
                  : '(no logs yet)'}
              </pre>

              <div className="hint">
                This loads from <code>GET /api/logs</code>. Use <b>Prefixed only</b> to focus on protocol lines (<code>ACK/STATUS/EVT/ERR</code>).
                Search filters locally in the browser.
              </div>
            </div>
          </section>
        )}

      </main>

      {/* Sequence Details Modal overlay (User, Available Songs) */}
      {tab === 'play' && (
        <>
          {seqModalOpen && (
            <div className="modal-overlay" onClick={closeSequenceModal}>
              <div className="modal" onClick={(e) => e.stopPropagation()}>
                <div className="modal-header">
                  <div>
                    <div className="modal-title">
                      {seqModalSeq?.name || (seqModalLoading ? 'Loading…' : uiMode === 'user' ? 'Song details' : 'Sequence details')}
                    </div>
                    <div className="modal-subtitle">
                      ID: <b>{seqModalSeq?.id || '—'}</b>
                      {seqModalSeq?.description ? (
                        <>
                          {'  '}|{'  '}
                          <span>{seqModalSeq.description}</span>
                        </>
                      ) : null}
                    </div>
                  </div>

                  <button className="btn btn-secondary" type="button" onClick={closeSequenceModal}>
                    Close
                  </button>
                </div>

                {seqModalError ? <pre className="pre">{seqModalError}</pre> : null}

                {seqModalLoading ? (
                  <div className="hint mt">{uiMode === 'user' ? 'Loading song details…' : 'Loading sequence details…'}</div>
                ) : seqModalSeq && Array.isArray(seqModalSeq.steps) ? (
                  <div className="card" style={{ marginBottom: 0 }}>
                    <h2>Steps</h2>

                    {seqModalSeq.steps.length === 0 ? (
                      <div className="hint mt">{uiMode === 'user' ? 'This song has no steps.' : 'This sequence has no steps.'}</div>
                    ) : (
                      <table className="steps-table">
                        <thead>
                          <tr>
                            <th>#</th>
                            <th>Key</th>
                            <th>{uiMode === 'user' ? 'Finger' : 'Color'}</th>
                            <th>Duration (ms)</th>
                          </tr>
                        </thead>
                        <tbody>
                          {seqModalSeq.steps.map((s, idx) => {
                            const keyIndex = s?.k ?? s?.key ?? s?.note ?? s?.index ?? '—';
                            const colorHex = s?.c ?? s?.color ?? s?.colour ?? s?.hex ?? '—';
                            const durationMs = s?.d ?? s?.duration ?? s?.ms ?? s?.time ?? '—';

                            return (
                              <tr key={idx}>
                                <td>{idx + 1}</td>
                                <td>{keyIndex}</td>
                                <td>
                                  <div className="step-color">
                                    {renderColorSwatch(colorHex)}
                                    <span className="mono">
                                      {uiMode === 'user'
                                        ? (HEX_TO_FINGER[String(colorHex).toUpperCase()] || hexColorName(colorHex))
                                        : hexColorName(colorHex)}
                                    </span>
                                  </div>
                                </td>
                                <td>{durationMs}</td>
                              </tr>
                            );
                          })}
                        </tbody>
                      </table>
                    )}
                  </div>
                ) : (
                  <div className="hint mt">{uiMode === 'user' ? 'No steps found for this song.' : 'No steps found for this sequence.'}</div>
                )}
              </div>
            </div>
          )}
        </>
      )}

      <style>{styles}</style>
    </div>
  );
}

const styles = `
.app {
  display: grid;
  grid-template-columns: 260px 1fr;
  min-height: 100vh;
  background: var(--background);
  color: var(--foreground);
  font-family: var(--font-sans);
}

/* ===== SIDEBAR ===== */
.sidebar {
  background: var(--navy);
  padding: 20px 16px;
  display: flex;
  flex-direction: column;
  gap: 16px;
  color: #CBD5E1;
  min-height: 100vh;
  position: sticky;
  top: 0;
  align-self: start;
}

.brand {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 8px;
  position: relative;
  padding-bottom: 14px;
}

.brand-logo {
  width: 120px;
  height: auto;
  object-fit: contain;
}

.brand-text {
  text-align: center;
}

.brand-title {
  font-family: var(--font-heading);
  font-weight: 800;
  font-size: 24px;
  letter-spacing: 0.3px;
  background: var(--gradient);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  background-clip: text;
}

.brand-subtitle {
  color: #64748B;
  font-size: 13px;
  margin-top: 2px;
}

.brand::after {
  content: '';
  display: block;
  position: absolute;
  bottom: 0;
  left: 0;
  right: 0;
  height: 2px;
  background: var(--gradient);
  border-radius: 2px;
}

.nav { display: flex; flex-direction: column; gap: 6px; margin-top: 4px; }

.nav-btn {
  background: transparent;
  border: 1px solid transparent;
  color: #CBD5E1;
  padding: 10px 14px;
  border-radius: var(--radius);
  cursor: pointer;
  text-align: left;
  font-size: 14px;
  font-weight: 500;
  transition: background 0.15s, color 0.15s;
}

.nav-btn:hover {
  background: rgba(232, 54, 143, 0.08);
  color: #F1F5F9;
}

.nav-btn.is-active {
  background: color-mix(in srgb, var(--primary) 15%, transparent);
  color: var(--primary);
  border-left: 3px solid;
  border-image: var(--gradient) 1;
  border-top: none;
  border-right: none;
  border-bottom: none;
  padding-left: 11px;
}

.sidebar-footer { margin-top: auto; }

/* ===== BADGE ===== */
.badge {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 6px 12px;
  border-radius: 999px;
  font-size: 12px;
  font-weight: 600;
}

.badge-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  display: inline-block;
}

.badge-offline {
  background: rgba(185, 28, 28, 0.15);
  color: #FCA5A5;
}

.badge-offline .badge-dot {
  background: #b91c1c;
}

.badge-online {
  background: rgba(78, 203, 113, 0.15);
  color: #86EFAC;
}

.badge-online .badge-dot {
  background: var(--green);
}

/* ===== MAIN CONTENT ===== */
.main {
  padding: 32px;
  overflow-y: auto;
  position: relative;
}

.main::before {
  content: '';
  position: absolute;
  top: 0;
  left: 0;
  width: 2px;
  height: 100%;
  background: linear-gradient(to bottom, var(--primary), var(--green), var(--gold), var(--secondary), var(--accent));
}

.panel { max-width: 1200px; }

h1 {
  margin: 0 0 20px 0;
  font-size: 30px;
  font-weight: 700;
  font-family: var(--font-heading);
  color: var(--foreground);
}

h2 {
  margin: 0 0 12px 0;
  font-size: 22px;
  font-weight: 600;
  font-family: var(--font-heading);
  color: var(--foreground);
  padding-bottom: 8px;
  border-bottom: 2px solid var(--border);
  background-image: var(--gradient);
  background-size: 100% 2px;
  background-position: bottom left;
  background-repeat: no-repeat;
  border-bottom: none;
}

/* ===== CARDS ===== */
.card {
  background: var(--card);
  border: 1px solid var(--border);
  border-radius: 12px;
  padding: 24px;
  margin-bottom: 16px;
  box-shadow: var(--shadow-md);
  transition: box-shadow 0.2s, transform 0.2s;
}

.card:hover {
  box-shadow: 0 4px 16px rgba(0, 180, 216, 0.08);
}

.grid {
  display: grid;
  gap: 16px;
  grid-template-columns: repeat(2, 1fr);
}

@media (max-width: 980px) {
  .app { grid-template-columns: 1fr; }
  .sidebar {
    flex-direction: row;
    align-items: center;
    padding: 12px 16px;
  }
  .brand::after { display: none; }
  .nav { flex-direction: row; flex-wrap: wrap; }
  .sidebar-footer { margin-top: 0; margin-left: auto; }
  .grid { grid-template-columns: 1fr; }
  .main { padding: 20px 16px; }
  .main::before { display: none; }
}

/* ===== LAYOUT ===== */
.row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
}

.row-left, .row-right {
  display: flex;
  align-items: center;
  gap: 8px;
}

.label {
  color: var(--muted-foreground);
  font-size: 13px;
}

.btn-row {
  display: flex;
  flex-wrap: nowrap;
  gap: 8px;
}

/* ===== BUTTONS ===== */
.btn {
  background: var(--primary);
  border: 1px solid var(--primary);
  color: var(--primary-foreground);
  padding: 10px 24px;
  border-radius: var(--radius);
  cursor: pointer;
  font-weight: 600;
  font-size: 14px;
  font-family: var(--font-sans);
  transition: filter 0.15s, transform 0.15s;
}

.btn:hover {
  filter: brightness(0.9);
  transform: scale(1.02);
}

.btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
  filter: none;
  transform: none;
}

.btn-sm {
  padding: 6px 14px;
  font-size: 12px;
}

/* ===== CB TOGGLE PILL ===== */
.cb-toggle {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 10px;
  cursor: pointer;
  user-select: none;
}

.cb-toggle input {
  position: absolute;
  opacity: 0;
  width: 0;
  height: 0;
}

.cb-toggle-track {
  position: relative;
  width: 38px;
  height: 20px;
  background: var(--border);
  border-radius: 10px;
  transition: background 0.25s;
  flex-shrink: 0;
}

.cb-toggle input:checked + .cb-toggle-track {
  background: var(--primary);
}

.cb-toggle-knob {
  position: absolute;
  top: 2px;
  left: 2px;
  width: 16px;
  height: 16px;
  background: #fff;
  border-radius: 50%;
  transition: transform 0.25s;
  box-shadow: 0 1px 3px rgba(0,0,0,0.3);
}

.cb-toggle input:checked + .cb-toggle-track .cb-toggle-knob {
  transform: translateX(18px);
}

.cb-toggle-label {
  font-size: 12px;
  color: var(--muted);
}

.btn-secondary {
  background: transparent;
  border: 2px solid var(--primary);
  color: var(--primary);
}

.btn-secondary:hover {
  background: rgba(0, 180, 216, 0.06);
  filter: none;
}

/* ===== INPUTS ===== */
.input {
  background: var(--input);
  border: 1px solid var(--border);
  color: var(--foreground);
  padding: 10px 12px;
  border-radius: var(--radius);
  width: 100%;
  font-family: var(--font-sans);
  font-size: 14px;
  transition: border-color 0.15s, box-shadow 0.15s;
}

.input:focus {
  outline: none;
  border-color: var(--ring);
  box-shadow: 0 0 0 3px rgba(0, 180, 216, 0.12);
}

.input-small { width: 100px; }

/* ===== PRE / CODE ===== */
.pre {
  margin: 10px 0 0 0;
  background: #F8F9FA;
  border: 1px solid var(--border);
  border-radius: var(--radius);
  padding: 14px;
  overflow: auto;
  max-height: 260px;
  font-size: 13px;
  line-height: 1.5;
  white-space: pre-wrap;
  word-break: break-word;
  color: var(--foreground);
  font-family: var(--font-mono);
}

.pre-logs { max-height: 520px; }

/* ===== BUTTON VARIANTS ===== */
.btn-coral {
  background: var(--secondary);
  border-color: var(--secondary);
  color: #fff;
}

.btn-coral:hover {
  filter: brightness(0.9);
}

.btn-accent {
  background: var(--accent);
  border-color: var(--accent);
  color: #fff;
}

.btn-accent:hover {
  filter: brightness(0.9);
}

.btn-green {
  background: var(--green);
  border-color: var(--green);
  color: #fff;
}

.btn-green:hover {
  filter: brightness(0.9);
}

.btn-gold {
  background: var(--gold);
  border-color: var(--gold);
  color: #1A2B3C;
}

.btn-gold:hover {
  filter: brightness(0.9);
}

/* ===== CARD ACCENT BORDERS ===== */
.card-accent-teal {
  border-top: 3px solid var(--primary);
}

.card-accent-coral {
  border-top: 3px solid var(--secondary);
}

.card-accent-magenta {
  border-top: 3px solid var(--accent);
}

.card-accent-green {
  border-top: 3px solid var(--green);
}

.card-accent-gold {
  border-top: 3px solid var(--gold);
}

/* ===== KEYBOARD VISUALISATION ===== */
.keyboard-vis-card {
  display: flex;
  flex-direction: column;
}

.keyboard-vis {
  position: relative;
  display: flex;
  gap: 2px;
  padding: 12px 4px 8px;
  height: 140px;
}

.kb-key {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: flex-end;
  gap: 4px;
  padding: 6px 2px 8px;
  border-radius: 0 0 6px 6px;
  text-align: center;
  transition: box-shadow 0.3s, transform 0.15s;
  min-width: 0;
}

.kb-white {
  flex: 1;
  background: var(--card);
  border: 1.5px solid var(--border);
  height: 100%;
  position: relative;
  z-index: 1;
}

.kb-black {
  position: absolute;
  top: 12px;
  width: calc(100% / 7 * 0.58);
  height: 55%;
  transform: translateX(-50%);
  background: var(--navy, #0D1B2A);
  border: 1.5px solid var(--navy, #0D1B2A);
  color: #8899AA;
  z-index: 2;
  border-radius: 0 0 4px 4px;
  box-shadow: 0 2px 4px rgba(0,0,0,0.3);
}

[data-theme="developer"] .kb-white {
  background: #1A2B3C;
}

.kb-active {
  transform: translateY(-2px);
  box-shadow:
    0 2px 12px color-mix(in srgb, var(--kb-glow, #fff) calc(var(--kb-intensity, 0.5) * 80%), transparent),
    inset 0 -3px 0 color-mix(in srgb, var(--kb-glow, #fff) 40%, transparent);
}

.kb-active.kb-black {
  transform: translateX(-50%) translateY(-2px);
}

.kb-active.kb-white {
  border-color: color-mix(in srgb, var(--kb-glow, #fff) 60%, transparent);
}

.kb-active.kb-black {
  border-color: color-mix(in srgb, var(--kb-glow, #fff) 50%, transparent);
}

.kb-led {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  box-shadow: 0 0 6px 2px currentColor;
  flex-shrink: 0;
}

.kb-note {
  font-size: 11px;
  font-weight: 700;
  font-family: var(--font-mono);
  margin-top: auto;
}

.kb-black .kb-note {
  font-size: 9px;
}

.kb-hits {
  font-size: 9px;
  font-weight: 600;
  color: var(--muted-foreground);
  font-family: var(--font-mono);
}

.kb-black .kb-hits {
  font-size: 8px;
}

/* ===== PILLS ===== */
.pill-row {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 6px;
}

.pill {
  display: inline-block;
  padding: 3px 10px;
  border-radius: 999px;
  font-size: 12px;
  font-weight: 600;
  white-space: nowrap;
}

.pill-teal {
  background: rgba(0, 180, 216, 0.1);
  color: #0097B2;
}

.pill-coral {
  background: rgba(255, 107, 53, 0.1);
  color: #E55A2B;
}

.pill-green {
  background: rgba(78, 203, 113, 0.1);
  color: #35A85C;
}

.pill-gold {
  background: rgba(255, 215, 0, 0.15);
  color: #B8960B;
}

.pill-magenta {
  background: rgba(232, 54, 143, 0.1);
  color: #C82A78;
}

.pill-muted {
  background: var(--muted);
  color: var(--muted-foreground);
}

/* ===== UTILITIES ===== */
.hint {
  color: var(--muted-foreground);
  font-size: 12px;
  margin-top: 10px;
  line-height: 1.5;
  border-left: 3px solid var(--gold);
  padding-left: 10px;
}

.mt { margin-top: 10px; }

/* ===== SEQUENCE TABLE ===== */
.seq-table {
  width: 100%;
  border-collapse: collapse;
  margin-top: 10px;
  font-size: 13px;
}

.seq-table th {
  text-align: left;
  padding: 8px 12px;
  border-bottom: 2px solid var(--border);
  color: var(--muted-foreground);
  font-weight: 600;
  font-size: 12px;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.seq-table td {
  padding: 6px 12px;
  border-bottom: 1px solid var(--border);
}

.seq-table tbody tr:hover {
  background: rgba(0, 180, 216, 0.04);
}

.seq-row-active {
  background: rgba(0, 180, 216, 0.08);
}

.seq-row-active td:first-child {
  font-weight: 600;
}

/* ===== DIAGNOSTICS COLLAPSIBLE ===== */
details.diagnostics {
  margin-top: 16px;
}

details.diagnostics summary {
  cursor: pointer;
  font-weight: 600;
  font-size: 16px;
  font-family: var(--font-heading);
  color: var(--muted-foreground);
  padding: 12px 0;
}

details.diagnostics summary:hover {
  color: var(--primary);
}

details.diagnostics[open] summary {
  color: var(--primary);
}

/* ===== MODAL (Sequence Details) ===== */
.modal-overlay {
  position: fixed;
  inset: 0;
  background: rgba(2, 6, 23, 0.65);
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 18px;
  z-index: 9999;
}

.modal {
  width: min(920px, 100%);
  max-height: 85vh;
  overflow: auto;
  background: var(--card);
  border: 1px solid var(--border);
  border-radius: 14px;
  box-shadow: 0 16px 48px rgba(0, 0, 0, 0.35);
  padding: 18px;
}

.modal-header {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 12px;
  margin-bottom: 12px;
}

.modal-title {
  font-family: var(--font-heading);
  font-weight: 800;
  font-size: 20px;
  color: var(--foreground);
}

.modal-subtitle {
  margin-top: 4px;
  color: var(--muted-foreground);
  font-size: 13px;
}

.seq-row-clickable {
  cursor: pointer;
}

.seq-row-clickable:hover {
  background: rgba(0, 180, 216, 0.04);
}

/* ===== STEPS TABLE ===== */
.steps-table {
  width: 100%;
  border-collapse: collapse;
  margin-top: 10px;
  font-size: 13px;
}

.steps-table th {
  text-align: left;
  padding: 8px 12px;
  border-bottom: 2px solid var(--border);
  color: var(--muted-foreground);
  font-weight: 600;
  font-size: 12px;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.steps-table td {
  padding: 8px 12px;
  border-bottom: 1px solid var(--border);
  vertical-align: middle;
}

.step-color {
  display: inline-flex;
  align-items: center;
  gap: 10px;
}

.swatch {
  width: 18px;
  height: 18px;
  border-radius: 6px;
  border: 1px solid var(--border);
  display: inline-block;
}

/* ===== Controls header row + help button ===== */
.panel-top-row {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 12px;
}

.finger-help-btn {
  white-space: nowrap;
}

/* ===== Finger map ===== */
.finger-map-wrap {
  display: flex;
  justify-content: center;
  padding: 10px 0 4px;
}

.finger-map {
  width: 100%;
  max-width: 560px;
  height: auto;
}

.mono {
  font-family: var(--font-mono);
}

/* ===== DARK MODE (developer theme) OVERRIDES ===== */
[data-theme="developer"] .pre {
  background: #0D1B2A;
}

[data-theme="developer"] code {
  background: #162336;
}

[data-theme="developer"] .pill-teal {
  background: rgba(0, 180, 216, 0.2);
  color: #4DD4EC;
}

[data-theme="developer"] .pill-coral {
  background: rgba(255, 107, 53, 0.2);
  color: #FF8C66;
}

[data-theme="developer"] .pill-green {
  background: rgba(78, 203, 113, 0.2);
  color: #6EE090;
}

[data-theme="developer"] .pill-gold {
  background: rgba(255, 215, 0, 0.2);
  color: #FFE04D;
}

[data-theme="developer"] .pill-magenta {
  background: rgba(232, 54, 143, 0.2);
  color: #F06AAE;
}

[data-theme="developer"] .seq-table tbody tr:hover {
  background: rgba(0, 180, 216, 0.08);
}

/* ===== UPLOAD LOG ===== */
.upload-log-toggle {
  background: none;
  border: none;
  color: var(--muted-foreground);
  font-size: 12px;
  cursor: pointer;
  padding: 4px 0;
  font-family: var(--font-sans);
}

.upload-log-toggle:hover {
  color: var(--primary);
}

.upload-log {
  margin-top: 6px;
  padding: 10px;
  background: var(--muted);
  border-radius: var(--radius);
  font-family: var(--font-mono);
  font-size: 12px;
  line-height: 1.8;
}

.upload-log-line {
  display: flex;
  justify-content: space-between;
  gap: 12px;
}
`;