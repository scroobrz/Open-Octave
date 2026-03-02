import { useEffect, useMemo, useRef, useState } from 'react';

function pretty(obj) {
  return JSON.stringify(obj, null, 2);
}

async function apiGet(path) {
  const res = await fetch(path);
  return res.json();
}

async function apiPost(path) {
  const res = await fetch(path, { method: 'POST' });
  return res.json();
}

function Badge({ connected }) {
  const cls = connected ? 'badge badge-online' : 'badge badge-offline';
  const text = connected ? 'Connected' : 'Disconnected';
  return <div className={cls}>{text}</div>;
}

function SeqSelect({ onSelect, disabled }) {
  const [value, setValue] = useState('');

  return (
    <div className="row mt">
      <input
        className="input"
        type="number"
        min="0"
        max="9"
        placeholder="Sequence ID (0-9)"
        value={value}
        onChange={(e) => setValue(e.target.value)}
      />
      <button
        className="btn"
        disabled={disabled}
        onClick={() => {
          const id = Number(value);
          if (!Number.isFinite(id) || id < 0 || id > 9) return;
          onSelect(id);
        }}
      >
        Select
      </button>
    </div>
  );
}

export default function App() {
  const [tab, setTab] = useState('connect');

  const [health, setHealth] = useState(null);
  const [healthError, setHealthError] = useState('');
    // ============ CONNECT TAB STATE (Transport Selector) ============
  const [transportChoice, setTransportChoice] = useState('WIFI'); // 'WIFI' | 'SERIAL'
  const [wifiIp, setWifiIp] = useState('127.0.0.1'); // use 192.168.4.1 for real ESP32 AP
  const [wifiPort, setWifiPort] = useState(8081);    // use 81 for real ESP32 firmware_v4
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
  const [seqListResult, setSeqListResult] = useState(null);
  const [seqListError, setSeqListError] = useState('');

  // ============ SOFTWARE SEQUENCE LIBRARY (SQLite) ============
  const [dbSeqItems, setDbSeqItems] = useState([]);
  const [dbSeqError, setDbSeqError] = useState('');
  const [selectedDbSeqId, setSelectedDbSeqId] = useState('');
  const [selectedDbSeq, setSelectedDbSeq] = useState(null);
  const [dbActionBusy, setDbActionBusy] = useState(false);
    // Last upload summary (for demo verification)
  const [lastUploadSummary, setLastUploadSummary] = useState(null);

  // ============ DB CREATE SEQUENCE (paste JSON) ============
  const [dbCreateJson, setDbCreateJson] = useState(
    pretty({
      id: 'my_sequence_id',
      name: 'My Sequence Name',
      description: 'Optional description',
      steps: [
        { k: 0, c: 'P', d: 300 },
        { k: 1, c: 'P', d: 300 }
      ]
    })
  );
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

  async function refreshSeqList() {
    try {
      setSeqListError('');
      const data = await apiGet('/api/seq/list');
      setSeqListResult(data);

      // Keep lastResult in sync too (useful for global debugging)
      setLastResult(data);

      await refreshHealth();
      await refreshLogs();
      await refreshControllerState();
    } catch (e) {
      setSeqListResult(null);
      setSeqListError(`Failed to load /api/seq/list: ${e.message}`);
    }
  }

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

  async function loadSelectedDbSequence(id) {
    try {
      if (!id) {
        setSelectedDbSeq(null);
        return;
      }
      setDbSeqError('');
      const data = await apiGet(`/api/db/sequences/${encodeURIComponent(id)}`);
      setSelectedDbSeq(data?.item || null);
    } catch (e) {
      setSelectedDbSeq(null);
      setDbSeqError(`Failed to load /api/db/sequences/${id}: ${e.message}`);
    }
  }

  async function uploadSelectedDbSequence() {
    try {
      if (!selectedDbSeqId) return;
      setDbActionBusy(true);
      setDbSeqError('');

      const data = await apiPost(`/api/db/sequences/${encodeURIComponent(selectedDbSeqId)}/upload`);
      setLastResult(data);
      setLastUploadSummary({
        ts: new Date().toISOString(),
        sequenceId: selectedDbSeqId,
        ok: Boolean(data?.ok),
        sentCount: data?.sentCount ?? null,
        error: data?.error || null
      });

      // After upload, refresh device-side view for verification.
      await refreshSeqList();
      await refreshLogs();
      await refreshControllerState();
    } catch (e) {
      setDbSeqError(`Failed to upload sequence: ${e.message}`);
    } finally {
      setDbActionBusy(false);
    }
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

      // Optionally auto-select the saved sequence
      const newId = String(payload.id).trim();
      setSelectedDbSeqId(newId);
      await loadSelectedDbSequence(newId);
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
      try {
        statusResp = await apiGet('/api/status');
        statusOk = true;
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
        seqListOk = true;
        setSeqListResult(seqListResp);
        setSeqListError('');
      } catch (e) {
        seqListResp = { error: e.message };
        seqListOk = false;
        setSeqListError(`Failed to load /api/seq/list: ${e.message}`);
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

      if (kind === 'mode') {
        result = await apiPost(`/api/modes?mode=${encodeURIComponent(data.mode)}`);
      } else if (kind === 'seq') {
        result = await apiPost(`/api/seq/control?cmd=${encodeURIComponent(data.action)}`);
      } else if (kind === 'seqSelect') {
        result = await apiPost(`/api/seq/select?id=${encodeURIComponent(data.id)}`);
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
    <div className="app dark">
      <aside className="sidebar">
        <div className="brand">
          <div className="brand-title">Open Octave</div>
          <div className="brand-subtitle">Controller UI</div>
        </div>

        <nav className="nav">
          <button
            className={tab === 'connect' ? 'nav-btn is-active' : 'nav-btn'}
            onClick={() => setTab('connect')}
          >
            Connect
          </button>
          <button
            className={tab === 'control' ? 'nav-btn is-active' : 'nav-btn'}
            onClick={() => setTab('control')}
          >
            Control
          </button>
          <button
            className={tab === 'sequences' ? 'nav-btn is-active' : 'nav-btn'}
            onClick={() => setTab('sequences')}
          >
            Sequences
          </button>
          <button
            className={tab === 'logs' ? 'nav-btn is-active' : 'nav-btn'}
            onClick={() => setTab('logs')}
          >
            Logs
          </button>
          <button
            className={tab === 'settings' ? 'nav-btn is-active' : 'nav-btn'}
            onClick={() => setTab('settings')}
          >
            Settings
          </button>
        </nav>

        <div className="sidebar-footer">
          <Badge connected={isConnected} />
        </div>
      </aside>

      <main className="main">
        {tab === 'sequences' && (
          <section className="panel">
            <h1>Sequences</h1>

            <div className="card">
              <h2>Software library (SQLite)</h2>

              <div className="row">
                <div className="label">
                  Loads from <code>GET /api/db/sequences</code>. Upload sends <code>U/S/E</code> lines to the device.
                </div>

                <div className="btn-row">
                  <button className="btn btn-secondary" onClick={refreshDbSequences} type="button" disabled={dbActionBusy}>
                    Refresh DB
                  </button>
                  <button className="btn btn-secondary" onClick={seedDemoSequences} type="button" disabled={dbActionBusy}>
                    Seed demos
                  </button>
                </div>
              </div>

              {dbSeqError ? <pre className="pre">{dbSeqError}</pre> : null}
              {lastUploadSummary ? (
                <pre className="pre">{pretty({ lastUpload: lastUploadSummary })}</pre>
              ) : null}

              <div className="row mt">
                <div className="label">Select a sequence</div>
                <select
                  className="input"
                  value={selectedDbSeqId}
                  onChange={async (e) => {
                    const id = e.target.value;
                    setSelectedDbSeqId(id);
                    await loadSelectedDbSequence(id);
                  }}
                >
                  <option value="">(none)</option>
                  {dbSeqItems.map((it) => (
                    <option key={it.id} value={it.id}>
                      {it.name} ({it.id}){typeof it.stepCount === 'number' ? ` • ${it.stepCount} steps` : ''}
                    </option>
                  ))}
                </select>
              </div>

              <div className="row mt">
                <div className="label">
                  Selected: <b>{selectedDbSeqId || 'none'}</b>
                </div>

                <div className="btn-row">
                  <button
                    className="btn"
                    type="button"
                    disabled={!isConnected || !selectedDbSeqId || dbActionBusy}
                    onClick={uploadSelectedDbSequence}
                  >
                    Upload to device
                  </button>
                  <button
                    className="btn btn-secondary"
                    type="button"
                    disabled={!selectedDbSeqId || dbActionBusy}
                    onClick={() => setLastResult(selectedDbSeq || { note: 'No sequence loaded yet' })}
                  >
                    Preview JSON
                  </button>
                </div>
              </div>

              <div className="hint">
                Demo 2 model: firmware stores only the default sequence + the last uploaded sequence.
                The full library lives in SQLite.
              </div>
            </div>

            <div className="row" style={{ marginBottom: 12 }}>
              <div className="label">
                Connected: <b>{String(isConnected)}</b>
                {'  '}|{'  '}
                Transport: <b>{ctrlState?.transport || health?.mode || 'n/a'}</b>
                {'  '}|{'  '}
                Last cmd: <b>{ctrlState?.lastCommand?.cmd || 'n/a'}</b>
              </div>
              <div className="btn-row">
                <button className="btn btn-secondary" onClick={refreshControllerState} type="button">
                  Refresh State
                </button>
              </div>
            </div>

            <div className="card">
              <h2>Current sequence on device (firmware 'l')</h2>

              <div className="row">
                <div className="label">
                  Loads from <code>GET /api/seq/list</code> (maps to firmware <code>l</code>)
                </div>
                <div className="btn-row">
                  <button
                    className="btn"
                    disabled={!isConnected}
                    onClick={refreshSeqList}
                    type="button"
                  >
                    Refresh (l)
                  </button>
                </div>
              </div>

              <div className="row mt">
                <div className="label">Command result (controller)</div>
              </div>

              {seqListError ? (
                <pre className="pre">{seqListError}</pre>
              ) : (
                <pre className="pre">{seqListResult ? pretty(seqListResult) : '(not loaded)'}</pre>
              )}

              <div className="row mt">
                <div className="label">Device output (from logs)</div>
              </div>
              <pre className="pre pre-logs">
                {logs.length
                  ? logs
                      .slice(-30)
                      .map((x) => `[${x.ts}] ${x.source}: ${x.message}`)
                      .join('\n')
                  : '(no logs yet)'}
              </pre>

              <div className="hint">
                Note: <code>GET /api/seq/list</code> triggers the firmware <code>l</code> command. The API response above only confirms the command was sent.
                The actual printed sequence content is streamed back as log lines.
              </div>
            </div>
          </section>
        )}
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
                    WiFi (WebSocket)
                  </button>
                  <button
                    className={transportChoice === 'SERIAL' ? 'btn' : 'btn btn-secondary'}
                    onClick={() => setTransportChoice('SERIAL')}
                    type="button"
                  >
                    USB Serial
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
                <div className="label">
                  Active: <b>{health?.mode || 'UNKNOWN'}</b>
                  {'  '}|{'  '}
                  WS: <b>{health?.wifi?.target || 'n/a'}</b>
                  {'  '}|{'  '}
                  Serial: <b>{health?.serial?.port || 'n/a'}</b>
                </div>

                <div className="btn-row">
                  <button className="btn" onClick={connectSelectedTransport} type="button">
                    Connect
                  </button>
                  <button className="btn btn-secondary" onClick={disconnectAll} type="button">
                    Disconnect
                  </button>
                  <button className="btn btn-secondary" onClick={refreshHealth} type="button">
                    Refresh
                  </button>
                </div>
              </div>

              {healthError ? (
                <pre className="pre">{healthError}</pre>
              ) : (
                <pre className="pre">{health ? pretty(health) : '(not loaded)'}</pre>
              )}

              <div className="hint">
                Demo 2 note: WebSocket single-character commands are sent without a newline.
                Serial commands are newline-terminated.
              </div>
            </div>

            <div className="card">
              <h2>Sync</h2>

              <div className="row" style={{ marginBottom: 8 }}>
                <div className="label">
                  Phase: <b>{syncState.phase}</b>
                </div>
                <div className="label">
                  Showing last 50 logs
                </div>
              </div>
              <div className="hint">
                Phases: <b>idle</b> → <b>status</b> → <b>seq_list</b> → <b>refresh</b> → <b>complete</b>.<br />
                If stuck:
                <ul>
                  <li><b>status</b>: /api/status not supported yet, or firmware not responding. Check logs for ERR/timeout.</li>
                  <li><b>seq_list</b>: /api/seq/list not supported yet, or firmware not responding. Check logs for ERR/timeout.</li>
                  <li><b>refresh</b>: controller/UI refresh calls failed. Try Refresh Health/Logs, or restart Node.</li>
                </ul>
              </div>

              <div className="row">
                <div className="label">
                  Status: <b>{syncState.statusOk === null ? '—' : syncState.statusOk ? 'OK' : 'FAILED'}</b>
                  {'  '}|{'  '}
                  Current seq (l): <b>{syncState.seqListOk === null ? '—' : syncState.seqListOk ? 'OK' : 'FAILED'}</b>
                </div>

                <div className="btn-row">
                  <button className="btn" onClick={runSync} disabled={!isConnected || syncState.running} type="button">
                    {syncState.running ? 'Syncing…' : 'Run Sync'}
                  </button>
                  <button className="btn btn-secondary" onClick={refreshSeqList} disabled={!isConnected || syncState.running} type="button">
                    Refresh current seq (l)
                  </button>
                </div>
              </div>

              {syncState.error ? <pre className="pre">{syncState.error}</pre> : null}

              <div className="row mt">
                <div className="label">/api/status response</div>
              </div>
              <pre className="pre">{syncState.statusResp ? pretty(syncState.statusResp) : '(not run)'}</pre>

              <div className="row mt">
                <div className="label">/api/seq/list response (firmware 'l')</div>
              </div>
              <pre className="pre">{syncState.seqListResp ? pretty(syncState.seqListResp) : '(not run)'}</pre>

              <div className="row mt">
                <div className="label">Sync log preview (latest 50)</div>
              </div>
              <pre className="pre pre-logs">
                {logs.length
                  ? logs
                      .slice(-50)
                      .map((x) => `[${x.ts}] ${x.source}: ${x.message}`)
                      .join('\n')
                  : '(no logs yet)'}
              </pre>

              <div className="hint">
                This sync is PRD-aligned but best-effort. If an endpoint fails, it likely means the current firmware/controller build does not support it yet.
              </div>
            </div>

            <div className="card">
              <h2>Last API result</h2>
              <pre className="pre">{lastResult ? pretty(lastResult) : '(none yet)'}</pre>
            </div>

            <div className="card">
              <h2>Controller state (/api/state)</h2>

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

              <div className="hint">
                This is the backend state mirror used for Demo 2 verification.
              </div>
            </div>
          </section>
        )}

        {tab === 'control' && (
          <section className="panel">
            <h1>Control</h1>

            <div className="grid">
              <div className="card">
                <h2>Mode</h2>
                <div className="btn-row">
                  <button className="btn" disabled={controlsDisabled} onClick={() => runCommand('mode', { mode: 'manual' })}>
                    Manual (m)
                  </button>
                  <button className="btn" disabled={controlsDisabled} onClick={() => runCommand('mode', { mode: 'guided' })}>
                    Guided (a)
                  </button>
                  <button className="btn" disabled={controlsDisabled} onClick={() => runCommand('mode', { mode: 'teaching' })}>
                    Teaching (f)
                  </button>
                </div>
              </div>

              <div className="card">
                <h2>Sequence</h2>
                <div className="btn-row">
                  <button className="btn" disabled={controlsDisabled} onClick={() => runCommand('seq', { action: 'start' })}>
                    Start (s)
                  </button>
                  <button className="btn" disabled={controlsDisabled} onClick={() => runCommand('seq', { action: 'stop' })}>
                    Stop (x)
                  </button>
                  <button className="btn" disabled={controlsDisabled} onClick={() => runCommand('seq', { action: 'next' })}>
                    Next (n)
                  </button>
                  <button className="btn" disabled={controlsDisabled} onClick={() => runCommand('seq', { action: 'prev' })}>
                    Prev (p)
                  </button>
                </div>

                <SeqSelect
                  onSelect={(id) => runCommand('seqSelect', { id })}
                  disabled={controlsDisabled}
                />

                <div className="row mt">
                  <button
                    className="btn btn-secondary"
                    disabled={controlsDisabled}
                    onClick={() => runCommand('currentSeq', {})}
                  >
                    Print current seq (l)
                  </button>
                  <button
                    className="btn btn-secondary"
                    disabled={controlsDisabled}
                    onClick={() => runCommand('status', {})}
                  >
                    Help/Status (?)
                  </button>
                </div>
              </div>

              <div className="card">
                <h2>Tests</h2>
                <div className="btn-row">
                  <button className="btn" disabled={controlsDisabled} onClick={() => runCommand('test', { target: 'leds' })}>
                    Test LEDs (t)
                  </button>
                  <button className="btn" disabled={controlsDisabled} onClick={() => runCommand('test', { target: 'servos' })}>
                    Test Servos (u)
                  </button>
                </div>
              </div>
            </div>

            <div className="card">
              <h2>Last API result</h2>
              <pre className="pre">
                {lastResult ? pretty(lastResult) : '(none yet)'}
              </pre>
            </div>
          </section>
        )}

        {tab === 'logs' && (
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

        {tab === 'settings' && (
          <section className="panel">
            <h1>Settings / Debug</h1>


            <div className="card">
              <h2>Connection</h2>

              <div className="row">
                <div className="label">
                  Connected: <b>{String(isConnected)}</b>
                  {'  '}|{'  '}
                  Transport: <b>{ctrlState?.transport || health?.mode || 'n/a'}</b>
                </div>

                <div className="btn-row">
                  <button className="btn btn-secondary" onClick={disconnectAll} type="button">
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
              </div>

              <div className="hint">
                Reset clears the in-memory controller mirror (lastCommand/lastAck counters) without restarting Node.
              </div>
            </div>

            <div className="card">
              <h2>Quick Diagnostics</h2>
              <div className="btn-row">
                <button className="btn btn-secondary" disabled={!isConnected} onClick={() => runCommand('status', {})}>
                  Status (GET /api/status)
                </button>
                <button className="btn btn-secondary" disabled={!isConnected} onClick={() => runCommand('currentSeq', {})}>
                  Current seq (GET /api/seq/list, l)
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

              <div className="hint">
                NOTE: A true raw command sender will be added later. For Demo 2 we stick to existing endpoints to avoid breaking hardware.
              </div>
            </div>

            <div className="card">
              <h2>Create / Update sequence (SQLite)</h2>

              <div className="row">
                <div className="label">
                  Paste JSON and save to the controller DB via <code>POST /api/db/sequences</code>.
                  Uploading replaces the device’s previously uploaded sequence.
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
                      setDbCreateJson(
                        pretty({
                          id: 'my_sequence_id',
                          name: 'My Sequence Name',
                          description: 'Optional description',
                          steps: [
                            { k: 0, c: 'P', d: 300 },
                            { k: 1, c: 'P', d: 300 }
                          ]
                        })
                      );
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
                Required fields: <code>id</code>, <code>name</code>, <code>steps</code> (array of step objects).
                Steps should include <code>k</code> (key index), <code>c</code> (command), and <code>d</code> (duration ms).
              </div>
            </div>

            <div className="card">
              <h2>Last API result</h2>
              <pre className="pre">{lastResult ? pretty(lastResult) : '(none yet)'}</pre>
            </div>
          </section>
        )}
      </main>

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

.sidebar {
  border-right: 1px solid var(--border);
  background: var(--card);
  padding: 16px;
  display: flex;
  flex-direction: column;
  gap: 14px;
}

.brand-title { font-weight: 700; letter-spacing: 0.3px; }
.brand-subtitle { color: var(--muted-foreground); font-size: 12px; margin-top: 2px; }

.nav { display: flex; flex-direction: column; gap: 8px; margin-top: 8px; }

.nav-btn {
  background: transparent;
  border: 1px solid var(--border);
  color: var(--foreground);
  padding: 10px 12px;
  border-radius: var(--radius);
  cursor: pointer;
  text-align: left;
}

.nav-btn.is-active {
  border-color: var(--ring);
  box-shadow: 0 0 0 1px rgba(229, 9, 20, 0.25) inset;
}

.sidebar-footer { margin-top: auto; }

.badge {
  display: inline-block;
  padding: 6px 10px;
  border-radius: 999px;
  font-size: 12px;
  border: 1px solid var(--border);
  background: var(--muted);
  color: var(--foreground);
}

.badge-offline { border-color: var(--destructive); }
.badge-online { border-color: var(--primary); }

.main { padding: 18px 22px; }

.panel { max-width: 1100px; }

h1 { margin: 0 0 14px 0; font-size: 22px; }
h2 { margin: 0 0 10px 0; font-size: 16px; color: var(--foreground); }

.card {
  background: var(--card);
  border: 1px solid var(--border);
  border-radius: var(--radius);
  padding: 14px;
  margin-bottom: 14px;
  box-shadow: var(--shadow-sm);
}

.grid {
  display: grid;
  gap: 14px;
  grid-template-columns: repeat(3, minmax(220px, 1fr));
}

@media (max-width: 980px) {
  .app { grid-template-columns: 1fr; }
  .sidebar { flex-direction: row; align-items: center; }
  .nav { flex-direction: row; }
  .grid { grid-template-columns: 1fr; }
}

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

.label { color: var(--muted-foreground); font-size: 13px; }

.btn-row {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}

.btn {
  background: var(--primary);
  border: 1px solid var(--primary);
  color: var(--primary-foreground);
  padding: 10px 12px;
  border-radius: var(--radius);
  cursor: pointer;
}

.btn:hover {
  filter: brightness(1.05);
}

.btn:disabled {
  opacity: 0.55;
  cursor: not-allowed;
  filter: none;
}

.btn-secondary {
  background: transparent;
  border: 1px solid var(--border);
  color: var(--foreground);
}

.btn-secondary:hover {
  border-color: var(--ring);
}

.input {
  background: var(--input);
  border: 1px solid var(--border);
  color: var(--foreground);
  padding: 10px 10px;
  border-radius: var(--radius);
  width: 100%;
  font-family: var(--font-sans);
}

.input-small { width: 100px; }

.pre {
  margin: 10px 0 0 0;
  background: var(--popover);
  border: 1px solid var(--border);
  border-radius: var(--radius);
  padding: 12px;
  overflow: auto;
  max-height: 260px;
  font-size: 12px;
  line-height: 1.45;
  white-space: pre-wrap;
  word-break: break-word;
  color: var(--foreground);
  font-family: var(--font-mono);
}

.pre-logs { max-height: 520px; }

.hint { color: var(--muted-foreground); font-size: 12px; margin-top: 10px; }
.mt { margin-top: 10px; }
`;