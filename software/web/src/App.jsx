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

function SeqSelect({ onSelect }) {
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

  const [lastResult, setLastResult] = useState(null);

  const timerRef = useRef(null);

  const isConnected = useMemo(() => {
    if (!health) return false;
    return Boolean(health?.wifi?.connected || health?.serial?.open);
  }, [health]);

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
    } catch (e) {
      setLastResult({ error: e.message });
    }
  }

  useEffect(() => {
    refreshHealth();
    refreshLogs();
    startAutoLogs();
    return () => stopAutoLogs();
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
            className={tab === 'logs' ? 'nav-btn is-active' : 'nav-btn'}
            onClick={() => setTab('logs')}
          >
            Logs
          </button>
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
              <h2>Last API result</h2>
              <pre className="pre">{lastResult ? pretty(lastResult) : '(none yet)'}</pre>
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
                  <button className="btn" onClick={() => runCommand('mode', { mode: 'manual' })}>
                    Manual (m)
                  </button>
                  <button className="btn" onClick={() => runCommand('mode', { mode: 'guided' })}>
                    Guided (a)
                  </button>
                  <button className="btn" onClick={() => runCommand('mode', { mode: 'teaching' })}>
                    Teaching (f)
                  </button>
                </div>
              </div>

              <div className="card">
                <h2>Sequence</h2>
                <div className="btn-row">
                  <button className="btn" onClick={() => runCommand('seq', { action: 'start' })}>
                    Start (s)
                  </button>
                  <button className="btn" onClick={() => runCommand('seq', { action: 'stop' })}>
                    Stop (x)
                  </button>
                  <button className="btn" onClick={() => runCommand('seq', { action: 'next' })}>
                    Next (n)
                  </button>
                  <button className="btn" onClick={() => runCommand('seq', { action: 'prev' })}>
                    Prev (p)
                  </button>
                </div>

                <SeqSelect onSelect={(id) => runCommand('seqSelect', { id })} />

                <div className="row mt">
                  <button
                    className="btn btn-secondary"
                    onClick={() => runCommand('currentSeq', {})}
                  >
                    Print Current Seq (l)
                  </button>
                  <button
                    className="btn btn-secondary"
                    onClick={() => runCommand('status', {})}
                  >
                    Help/Status (?)
                  </button>
                </div>
              </div>

              <div className="card">
                <h2>Tests</h2>
                <div className="btn-row">
                  <button className="btn" onClick={() => runCommand('test', { target: 'leds' })}>
                    Test LEDs (t)
                  </button>
                  <button className="btn" onClick={() => runCommand('test', { target: 'servos' })}>
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
                </div>
              </div>

              <pre className="pre pre-logs">
                {logs.length
                  ? logs.map(
                      (x) => `[${x.ts}] ${x.source}: ${x.message}`
                    ).join('\n')
                  : '(no logs yet)'}
              </pre>

              <div className="hint">
                This loads from <code>GET /api/logs</code>. With the mock ESP32,
                you should at least see <code>[MOCK-ESP32] hello</code>.
              </div>
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