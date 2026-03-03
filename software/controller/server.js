require('dotenv').config();
const express = require('express');
const cors = require('cors');
const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');
const WebSocket = require('ws');

// SQLite sequence library (software-side)
const {
    DB_PATH,
    listSequences,
    getSequence,
    upsertSequence,
    generateUploadLinesFromData
} = require('./database/sqlite');

const app = express();

const PORT = process.env.APP_PORT || 3000;
const COMM_MODE = process.env.COMM_MODE || 'WIFI';
const ESP32_IP = process.env.ESP32_IP || '192.168.4.1';
const WS_PORT = process.env.WS_PORT || 81;
const SERIAL_PATH = process.env.SERIAL_PORT;
const BAUD_RATE = 115200;

// runtime-configurable connection settings.
// We keep COMM_MODE as the startup default, but allow the UI to switch transport
// via /api/connect and /api/disconnect without restarting Node.
let activeTransport = COMM_MODE; // 'WIFI' | 'SERIAL'
let currentEsp32Ip = ESP32_IP;
let currentWsPort = WS_PORT;
let currentSerialPath = SERIAL_PATH;

app.use(cors());
app.use(express.json());

// ============ LOG BUFFER (UI SUPPORT) ============
// The UI needs to display recent logs (Logs tab + Sync panel). We keep a small,
// in-memory ring buffer of the most recent lines. only records what we already print.
const LOG_BUFFER_MAX = 500;
const logBuffer = [];

function pushLog(source, message) {
    const line = {
        ts: new Date().toISOString(),
        source: source, // e.g. 'ESP32', 'CTRL', 'WS', 'SERIAL'
        message: message
    };

    logBuffer.push(line);
    if (logBuffer.length > LOG_BUFFER_MAX) {
        logBuffer.shift();
    }
}

// ============ CONTROLLER STATE (UI MIRROR) ============
// The UI needs to show the latest status from the ESP32 (Status tab) and also
const controllerState = {
    transport: activeTransport,    // 'WIFI' | 'SERIAL'
    connected: false,
    wsTarget: `ws://${currentEsp32Ip}:${currentWsPort}`,
    serialPort: currentSerialPath || null,

    lastCommand: null,             // { ts, transport, cmd }
    lastAck: null,                 // { ts, raw, fields }
    lastStatus: null,              // { ts, raw, fields }
    lastEvent: null,               // { ts, raw }
    lastError: null,               // { ts, raw }

    counters: {
        lines: 0,
        ack: 0,
        status: 0,
        evt: 0,
        err: 0
    }
};

function snapshotControllerState() {
    // Safe copy for API responses
    return JSON.parse(JSON.stringify(controllerState));
}

function parseKeyValuePairs(line) {
    // Example: "STATUS mode=MANUAL running=0 seq=1 step=0"
    const parts = String(line).split(' ').slice(1);
    const out = {};

    for (const p of parts) {
        const eq = p.indexOf('=');
        if (eq === -1) continue;

        const k = p.slice(0, eq).trim();
        const v = p.slice(eq + 1).trim();

        if (!k) continue;
        out[k] = v;
    }

    return out;
}

function ingestEsp32Line(msg) {
    controllerState.counters.lines += 1;

    // The mock server may prefix lines like:
    //   "[MOCK-ESP32] ACK cmd=m"
    // Real firmware might also prefix logs in the future.
    // We normalize by stripping a single leading "[... ] " prefix if present.
    let normalized = String(msg);
    const m = normalized.match(/^\[[^\]]+\]\s+(.*)$/);
    if (m && m[1]) {
        normalized = m[1];
    }

    if (normalized.startsWith('ACK ')) {
        controllerState.counters.ack += 1;
        controllerState.lastAck = {
            ts: new Date().toISOString(),
            raw: msg,
            fields: parseKeyValuePairs(normalized)
        };
        return;
    }

    if (normalized.startsWith('STATUS ')) {
        controllerState.counters.status += 1;
        controllerState.lastStatus = {
            ts: new Date().toISOString(),
            raw: msg,
            fields: parseKeyValuePairs(normalized)
        };
        return;
    }

    if (normalized.startsWith('EVT ')) {
        controllerState.counters.evt += 1;
        controllerState.lastEvent = {
            ts: new Date().toISOString(),
            raw: msg
        };
        return;
    }

    if (normalized.startsWith('ERR ')) {
        controllerState.counters.err += 1;
        controllerState.lastError = {
            ts: new Date().toISOString(),
            raw: msg
        };
        return;
    }
}

function refreshConnectionState() {
    // Keep the mirror in sync with the actual transport connections.
    const wsOpen = !!(ws && ws.readyState === WebSocket.OPEN);
    const serialOpen = !!(port && port.isOpen);

    controllerState.transport = activeTransport;
    controllerState.connected = wsOpen || serialOpen;
    controllerState.wsTarget = `ws://${currentEsp32Ip}:${currentWsPort}`;
    controllerState.serialPort = currentSerialPath || null;
}

// ============ SERIAL MODE SETUP ============

let port;
let parser;

function closeSerialPortAsync() {
    return new Promise((resolve) => {
        if (!port || !port.isOpen) {
            resolve({ closed: true, alreadyClosed: true });
            return;
        }

        console.log('[SERIAL] Closing port...');
        port.close((err) => {
            if (err) {
                console.error('[SERIAL] Close error:', err.message);
                resolve({ closed: false, error: err.message });
                return;
            }
            resolve({ closed: true });
        });
    });
}

async function connectSerial(pathOverride) {
    const pathToUse = pathOverride || currentSerialPath;

    if (!pathToUse) {
        console.error('[SERIAL] SERIAL_PORT is not set');
        return { error: 'SERIAL_PORT not set' };
    }

    // If already open on the requested port, do nothing.
    if (port && port.isOpen) {
        const existingPath = port.path || currentSerialPath;
        if (existingPath === pathToUse) {
            return { success: true, mode: 'serial', alreadyOpen: true, port: pathToUse };
        }

        // Port is open on a different path; close it before reopening.
        console.log(`[SERIAL] Port already open on ${existingPath}; switching to ${pathToUse}...`);
        await closeSerialPortAsync();

        // Clear references so we don't keep old listeners around.
        port = undefined;
        parser = undefined;
    }

    console.log(`[INIT] Starting in SERIAL mode on ${pathToUse}...`);

    try {
        currentSerialPath = pathToUse;
        port = new SerialPort({ path: currentSerialPath, baudRate: BAUD_RATE });
        parser = port.pipe(new ReadlineParser({ delimiter: '\n' }));

        // Wait for the port to actually open before returning.
        await new Promise((resolve, reject) => {
            port.on('open', () => {
                console.log('[SERIAL] Port Open');
                resolve();
            });
            port.on('error', (err) => {
                console.error('[SERIAL] Error: ', err.message);
                reject(err);
            });
        });

        // Print incoming serial data from the ESP32
        parser.on('data', (data) => {
            const msg = data.toString().trim();
            if (msg.length === 0) return;

            console.log(`[ESP32] ${msg}`);
            // keep a copy for the UI Logs tab.
            pushLog('ESP32', msg);
            ingestEsp32Line(msg);
        });

        return { success: true, mode: 'serial', port: currentSerialPath };
    } catch (err) {
        console.error(`[FATAL] Could not open serial port: ${err.message}`);
        return { error: `Could not open serial port: ${err.message}` };
    }
}

function disconnectSerial() {
    // Nothing to do
    if (!port) {
        return { success: true, mode: 'serial', alreadyClosed: true };
    }

    // If open, close it.
    if (port.isOpen) {
        console.log('[SERIAL] Closing port...');
        try {
            port.close((err) => {
                if (err) {
                    console.error('[SERIAL] Close error:', err.message);
                }
            });
        } catch (e) {
            console.error('[SERIAL] Close error:', e.message);
        }
    }

    port = undefined;
    parser = undefined;

    return { success: true, mode: 'serial', closed: true };
}

// Startup behavior preserved: open serial immediately if COMM_MODE=SERIAL.
if (COMM_MODE === 'SERIAL') {
    connectSerial().catch((e) => console.error('[SERIAL] Startup connect error:', e.message));
}

// ============ WEBSOCKET MODE SETUP ============

let ws;                         // WebSocket client instance
let wsConnected = false;        // Connection state
let wsReconnectTimer = null;    // Reconnect timer reference
let wsReconnectDelay = 1000;    // Initial reconnect delay (ms)
const WS_MAX_RECONNECT_DELAY = 10000;  // Cap at 10 seconds

// Connects (or reconnects) the WebSocket client to the ESP32.
// Uses exponential backoff: 1s -> 2s -> 4s -> 8s -> 10s (capped).
function connectWebSocket(ipOverride, portOverride) {
    if (activeTransport !== 'WIFI') return Promise.resolve(false);

    if (ipOverride) currentEsp32Ip = ipOverride;
    if (portOverride) currentWsPort = portOverride;

    const wsUrl = `ws://${currentEsp32Ip}:${currentWsPort}`;

    // If already connected to the same target, skip reconnect.
    if (ws && ws.readyState === WebSocket.OPEN) {
        const currentUrl = `ws://${currentEsp32Ip}:${currentWsPort}`;
        if (currentUrl === wsUrl) {
            console.log(`[WS] Already connected to ${wsUrl}`);
            return Promise.resolve(true);
        }
        // Different target — close old connection first.
        try { ws.close(); } catch (_) {}
        wsConnected = false;
    }

    // Cancel any pending reconnect.
    if (wsReconnectTimer) {
        clearTimeout(wsReconnectTimer);
        wsReconnectTimer = null;
    }

    console.log(`[WS] Connecting to ${wsUrl}...`);

    ws = new WebSocket(wsUrl);

    const connectPromise = new Promise((resolve) => {
        const timeout = setTimeout(() => resolve(false), 5000);

        ws.once('open', () => {
            clearTimeout(timeout);
            resolve(true);
        });

        ws.once('error', () => {
            clearTimeout(timeout);
            resolve(false);
        });
    });

    ws.on('open', () => {
        wsConnected = true;
        wsReconnectDelay = 1000;  // Reset backoff on successful connect
        console.log(`[WS] Connected to ESP32 at ${wsUrl}`);
    });

    // Incoming messages are the ESP32's log output — print them like serial
    ws.on('message', (data) => {
        // data arrives as a Buffer; convert to string and print
        const msg = data.toString().trim();
        if (msg.length > 0) {
            console.log(`[ESP32] ${msg}`);
            // keep a copy for the UI Logs tab.
            pushLog('ESP32', msg);
            ingestEsp32Line(msg);
        }
    });

    // Capture a reference so the close/error handlers only act on the
    // current connection.  If the user presses Connect again (creating a
    // new ws), the old socket's close event must NOT trigger a reconnect
    // that would overwrite the new connection.
    const thisWs = ws;

    thisWs.on('close', () => {
        if (ws === thisWs) {
            wsConnected = false;
            console.log('[WS] Connection closed');
            scheduleReconnect();
        }
    });

    thisWs.on('error', (err) => {
        // Suppress noisy ECONNREFUSED errors during reconnect attempts
        if (err.code !== 'ECONNREFUSED') {
            console.error(`[WS] Error: ${err.message}`);
        }
        // 'close' event will fire after error, triggering reconnect
    });

    return connectPromise;
}

// Schedules a reconnection attempt with exponential backoff.
function scheduleReconnect() {
    if (activeTransport !== 'WIFI') return; // Do not reconnect if transport switched away.
    if (wsReconnectTimer) return;  // Already scheduled

    console.log(`[WS] Reconnecting in ${wsReconnectDelay / 1000}s...`);
    wsReconnectTimer = setTimeout(() => {
        wsReconnectTimer = null;
        // Skip if something else already established a connection.
        if (ws && ws.readyState === WebSocket.OPEN) return;
        connectWebSocket();
    }, wsReconnectDelay);

    // Exponential backoff: double the delay each time, capped at max
    wsReconnectDelay = Math.min(wsReconnectDelay * 2, WS_MAX_RECONNECT_DELAY);
}

function disconnectWebSocket() {
    // stop reconnect attempts when user disconnects.
    if (wsReconnectTimer) {
        clearTimeout(wsReconnectTimer);
        wsReconnectTimer = null;
    }

    wsReconnectDelay = 1000;
    wsConnected = false;

    if (ws) {
        console.log('[WS] Closing connection...');
        try {
            ws.close();
        } catch (e) {
            console.error('[WS] Close error:', e.message);
        }
    }

    ws = undefined;

    return { success: true, mode: 'websocket', closed: true };
}

if (COMM_MODE === 'WIFI') {
    console.log(`[INIT] Starting in WIFI (WebSocket) mode targeting ${currentEsp32Ip}...`);
    connectWebSocket();
}

// ============ COMMAND TRANSLATION ============

// Translates API endpoint + query parameters into a single-character serial
// command. This is the shared command format used by both serial and WebSocket.
function translateToSerialCmd(endpoint, query) {
    if (endpoint === '/api/modes') {
        if (query.mode === 'manual')   return 'm';
        if (query.mode === 'guided')   return 'a';
        if (query.mode === 'teaching') return 'f';
    }
    
    if (endpoint === '/api/seq/control') {
        if (query.cmd === 'start') return 's';
        if (query.cmd === 'stop')  return 'x';
        if (query.cmd === 'next')  return 'n';
        if (query.cmd === 'prev')  return 'p';
    }

    if (endpoint === '/api/seq/select') {
        if (query.id !== undefined) return query.id.toString(); 
    }

    if (endpoint === '/api/test') {
        if (query.target === 'leds')   return 't';
        if (query.target === 'servos') return 'u';
    }

    if (endpoint === '/api/seq/list') return 'l';
    if (endpoint === '/api/status')   return '?';

    return null;
}

// ============ COMMAND DISPATCH ============

// Both WIFI and SERIAL modes now use the same fire-and-forget pattern:
//   1. Translate the API call to a single-char command
//   2. Send it over the active channel (WebSocket or serial)
//   3. Return immediately with a confirmation
// The ESP32's response (log output) streams back asynchronously.

async function sendCommand(endpoint, method, query = {}) {
    const cmd = translateToSerialCmd(endpoint, query);

    if (!cmd) {
        console.warn(`[CMD] No mapping for ${endpoint} ${JSON.stringify(query)}`);
        return { error: 'Command not supported' };
    }

    // record the last command sent for UI debugging / state mirror
    controllerState.lastCommand = {
        ts: new Date().toISOString(),
        transport: activeTransport,
        cmd: cmd
    };

    if (activeTransport === 'WIFI') {
        return sendWsCommand(cmd);
    }

    if (activeTransport === 'SERIAL') {
        return sendSerialCommand(cmd);
    }

    return { error: 'No active transport selected' };
}

// Sends a raw multi-character line (used for sequence upload U/S/E lines).
// Uses the same transport framing rules as other commands.
function sendRawLine(line) {
  if (typeof line !== 'string' || line.trim().length === 0) {
    return { error: 'Empty upload line' };
  }

  // Record for UI debugging
  controllerState.lastCommand = {
    ts: new Date().toISOString(),
    transport: activeTransport,
    cmd: line
  };

  if (activeTransport === 'WIFI') {
    return sendWsCommand(line);
  }

  if (activeTransport === 'SERIAL') {
    return sendSerialCommand(line);
  }

  return { error: 'No active transport selected' };
}

// ============ TRANSPORT FRAMING (DEMO 2 WORKAROUND) ============
// For Demo 2, firmware_v4 routes WebSocket commands by message length:
//   - length === 1  -> treated as a single-char command
//   - length  >  1  -> treated as a sequence upload command string
// Therefore, we MUST NOT append a newline for single-char WS commands.
// Serial, however, only dispatches after receiving a newline, so we do append it.

function trimLineEndings(s) {
    return s.replace(/[\r\n]+$/g, '');
}

function frameForWebSocket(cmd) {
    const clean = trimLineEndings(String(cmd));

    // Single-char commands: send exactly one character.
    if (clean.length === 1) {
        return clean;
    }

    // Multi-line / upload strings: safe to newline-terminate.
    // (Firmware upload parsing stops at \n or \0.)
    return clean.endsWith('\n') ? clean : `${clean}\n`;
}

function frameForSerial(cmd) {
    const clean = trimLineEndings(String(cmd));
    return `${clean}\n`;
}

function sendWsCommand(cmd) {
    if (!ws || !wsConnected) {
        console.error('[WS] Not connected to ESP32');
        return { error: 'WebSocket not connected' };
    }

    const payload = frameForWebSocket(cmd);

    // for single-char commands, payload == cmd.
    console.log(`[WS] Sending: '${cmd}' -> '${payload.replace(/\n/g, '\\n')}'`);

    // record controller actions for UI debugging.
    pushLog('CTRL', `WS send: ${cmd}`);

    ws.send(payload);
    return { success: true, mode: 'websocket', cmd: cmd };
}

function sendSerialCommand(cmd) {
    if (!port || !port.isOpen) {
        console.error('[SERIAL] Port closed');
        return { error: 'Serial port closed' };
    }

    // The firmware's USB-Serial command handler only dispatches a command
    // when it receives a line ending (\n or \r). Without this, single-char
    // commands can sit in the firmware's input buffer and never execute.
    // WebSocket mode does not need this because each WS message is already
    // a complete "frame".
    const serialPayload = frameForSerial(cmd);

    console.log(`[SERIAL] Sending: '${cmd}' (with newline)`);
    // record controller actions for UI debugging.
    pushLog('CTRL', `SERIAL send: ${cmd}`);
    port.write(serialPayload);

    return { success: true, mode: 'serial', cmd: cmd };
}


// ============ API ROUTES ============

// Connect/disconnect endpoints for the UI.
// This lets us switch between WIFI and SERIAL without restarting the Node controller.
// Body examples:
//   { "transport": "WIFI", "esp32Ip": "192.168.4.1", "wsPort": 81 }
//   { "transport": "SERIAL", "serialPort": "/dev/cu.usbmodemXXXX" }
app.post('/api/connect', async (req, res) => {
    try {
        const body = req.body || {};
        const transport = String(body.transport || '').toUpperCase();

        if (transport !== 'WIFI' && transport !== 'SERIAL') {
            res.status(400).json({ error: 'transport must be WIFI or SERIAL' });
            return;
        }

        // Close the other transport first (lowest-risk: avoid two open pipes).
        if (transport === 'WIFI') {
            disconnectSerial();
            activeTransport = 'WIFI';
            refreshConnectionState();

            const ip = body.esp32Ip ? String(body.esp32Ip) : undefined;
            const wsPort = body.wsPort !== undefined ? Number(body.wsPort) : undefined;

            const wsOk = await connectWebSocket(ip, wsPort);
            refreshConnectionState();
            res.json({ success: true, result: { transport: 'WIFI', connect: true, ws: wsOk } });
            return;
        }

        // SERIAL
        disconnectWebSocket();
        activeTransport = 'SERIAL';
        refreshConnectionState();

        const serialPort = body.serialPort ? String(body.serialPort) : undefined;
        const serialResult = await connectSerial(serialPort);
        refreshConnectionState();
        res.json({ success: true, result: { transport: 'SERIAL', connect: true, serial: serialResult } });
    } catch (e) {
        res.status(500).json({ error: e.message });
    }
});

app.post('/api/disconnect', (req, res) => {
    try {
        const wsResult = disconnectWebSocket();
        const serialResult = disconnectSerial();
        refreshConnectionState();
        res.json({ success: true, result: { websocket: wsResult, serial: serialResult } });
    } catch (e) {
        res.status(500).json({ error: e.message });
    }
});

app.post('/api/modes', async (req, res) => {
    try {
        const result = await sendCommand('/api/modes', 'POST', req.query);
        res.json(result);
    } catch (e) { res.status(500).json({ error: e.message }); }
});

app.post('/api/seq/control', async (req, res) => {
    try {
        const result = await sendCommand('/api/seq/control', 'POST', req.query);
        res.json(result);
    } catch (e) { res.status(500).json({ error: e.message }); }
});

app.post('/api/seq/select', async (req, res) => {
    try {
        const result = await sendCommand('/api/seq/select', 'POST', req.query);
        res.json(result);
    } catch (e) { res.status(500).json({ error: e.message }); }
});

app.get('/api/seq/list', async (req, res) => {
    try {
        const result = await sendCommand('/api/seq/list', 'GET');
        res.json(result);
    } catch (e) { res.status(500).json({ error: e.message }); }
});

app.post('/api/test', async (req, res) => {
    try {
        const result = await sendCommand('/api/test', 'POST', req.query);
        res.json(result);
    } catch (e) { res.status(500).json({ error: e.message }); }
});

app.get('/api/status', async (req, res) => {
    try {
        const result = await sendCommand('/api/status', 'GET');
        res.json(result);
    } catch (e) { res.status(503).json({ online: false }); }
});

// Simple health endpoint for UI and for no-hardware testing.
// only reports current controller state.
app.get('/api/health', (req, res) => {
    const mode = activeTransport || (process.env.COMM_MODE || 'WIFI');

    const wsConnectedVal = !!(ws && ws.readyState === WebSocket.OPEN);
    const serialOpen = !!(port && port.isOpen);
    refreshConnectionState();

    res.json({
        ok: true,
        mode: mode,
        appPort: process.env.APP_PORT || 3000,
        wifi: {
            target: `ws://${currentEsp32Ip}:${currentWsPort}`,
            connected: wsConnectedVal
        },
        serial: {
            port: currentSerialPath || null,
            open: serialOpen
        }
    });
});

// Returns the most recent controller/ESP32 log lines for the UI.
// Query:
//   tail=<n> (default 200, max LOG_BUFFER_MAX)

app.get('/api/logs', (req, res) => {
    const rawTail = req.query.tail;
    const requested = rawTail ? Number(rawTail) : 200;

    // Validate tail
    let tail = Number.isFinite(requested) ? Math.floor(requested) : 200;
    if (tail <= 0) tail = 200;
    if (tail > LOG_BUFFER_MAX) tail = LOG_BUFFER_MAX;

    const start = Math.max(logBuffer.length - tail, 0);
    const items = logBuffer.slice(start);

    res.json({
        ok: true,
        max: LOG_BUFFER_MAX,
        returned: items.length,
        items: items
    });
});

// Returns the controller state mirror for the UI.
app.get('/api/state', (req, res) => {
    refreshConnectionState();
    res.json({ ok: true, state: snapshotControllerState() });
});

// Resets the in-memory state mirror (useful in Settings/Debug later).
app.post('/api/state/reset', (req, res) => {
    controllerState.transport = activeTransport;
    controllerState.connected = false;
    controllerState.wsTarget = `ws://${currentEsp32Ip}:${currentWsPort}`;
    controllerState.serialPort = currentSerialPath || null;

    controllerState.lastCommand = null;
    controllerState.lastAck = null;
    controllerState.lastStatus = null;
    controllerState.lastEvent = null;
    controllerState.lastError = null;

    controllerState.counters = { lines: 0, ack: 0, status: 0, evt: 0, err: 0 };

    res.json({ ok: true, state: snapshotControllerState() });
});


// ============ DB API (SOFTWARE SEQUENCE LIBRARY) ============

// Lists software-side sequences stored in SQLite.
app.get('/api/db/sequences', (req, res) => {
  try {
    res.json({ ok: true, items: listSequences(), dbPath: DB_PATH });
  } catch (e) {
    res.status(500).json({ ok: false, error: e.message });
  }
});

// Gets one sequence from SQLite.
app.get('/api/db/sequences/:id', (req, res) => {
  try {
    const id = String(req.params.id || '').trim();
    if (!id) {
      res.status(400).json({ ok: false, error: 'Missing id' });
      return;
    }

    const seq = getSequence(id);
    if (!seq) {
      res.status(404).json({ ok: false, error: 'Sequence not found' });
      return;
    }

    res.json({ ok: true, item: seq });
  } catch (e) {
    res.status(500).json({ ok: false, error: e.message });
  }
});

// Creates/updates a software-side sequence in SQLite.
// Body example:
// {
//   "id": "twinkle",
//   "name": "Twinkle_Twinkle",
//   "description": "Demo song",
//   "steps": [ {"k":0,"c":"FF0000","d":300}, ... ]
// }
// NOTE: uploadLines are generated on-demand during upload if not stored.
app.post('/api/db/sequences', (req, res) => {
  try {
    const body = req.body || {};
    const id = String(body.id || '').trim();
    const name = String(body.name || '').trim();
    const description = body.description ? String(body.description) : '';
    const steps = body.steps;

    if (!id) {
      res.status(400).json({ ok: false, error: 'Missing id' });
      return;
    }
    if (!name) {
      res.status(400).json({ ok: false, error: 'Missing name' });
      return;
    }
    if (!Array.isArray(steps)) {
      res.status(400).json({ ok: false, error: 'steps must be an array' });
      return;
    }

    // Store the software model. Do not require uploadLines.
    const seq = {
      id,
      name,
      description,
      data: { steps },
      uploadLines: []
    };

    upsertSequence(seq);

    res.json({ ok: true, message: 'Saved sequence', item: getSequence(id) });
  } catch (e) {
    res.status(500).json({ ok: false, error: e.message });
  }
});

// Seeds the demo preset sequences into SQLite (mirroring the firmware preset library).
// NOTE: uploadLines are sent verbatim to the firmware, so ensure they match the firmware_v4 upload protocol.
app.post('/api/db/sequences/seed', (req, res) => {
  try {
    // ============ FINGER COLOUR MAP (Demo 2) ============
    // One colour scheme that works for both:
    // - 3-key demos (Thumb/Index/Middle)
    // - 12-key demos (Thumb/Index/Middle/Ring/Pinky)
    const FINGER_COLOR = {
      thumb:  'FF0000', // Red
      index:  'FFFF00', // Yellow
      middle: '00FF00', // Green
      ring:   'FF8000', // Orange
      pinky:  '0000FF', // Blue
      none:   'FFFFFF'  // Fallback
    };

    function colorForFinger(f) {
      const key = String(f || '').toLowerCase();
      return FINGER_COLOR[key] || FINGER_COLOR.none;
    }

    // Legacy 3-key mapping used by older demo presets (keys 0..2).
    // We interpret:
    //   0 -> C (thumb), 1 -> D (index), 2 -> E (middle)
    // so the same finger map screen remains valid.
    function colorFor3KeyIndex(k) {
      switch (Number(k)) {
        case 0: return colorForFinger('thumb');
        case 1: return colorForFinger('index');
        case 2: return colorForFinger('middle');
        default: return colorForFinger('none');
      }
    }

    // 12-key chromatic mapping (C4..B4 => keyIndex 0..11).
    // Natural notes follow a beginner right-hand position:
    //   C->thumb, D->index, E->middle, F->ring, G->pinky
    // Sharps inherit the nearest natural note's finger.
    // Notes outside C..G (A, A#, B) reuse thumb/index for simplicity.
    function colorFor12KeyIndex(k) {
      switch (Number(k)) {
        case 0:  return colorForFinger('thumb');  // C
        case 1:  return colorForFinger('thumb');  // C#
        case 2:  return colorForFinger('index');  // D
        case 3:  return colorForFinger('index');  // D#
        case 4:  return colorForFinger('middle'); // E
        case 5:  return colorForFinger('ring');   // F
        case 6:  return colorForFinger('ring');   // F#
        case 7:  return colorForFinger('pinky');  // G
        case 8:  return colorForFinger('pinky');  // G#
        case 9:  return colorForFinger('thumb');  // A
        case 10: return colorForFinger('thumb');  // A#
        case 11: return colorForFinger('index');  // B
        default: return colorForFinger('none');
      }
    }

    const presets = [
      {
        id: '0',
        name: 'Ping Pong',
        description: 'Sequence 0: alternating pattern across keys 0-2.',
        data: {
          steps: [
            { k: 0, c: colorFor3KeyIndex(0), d: 500 },
            { k: 1, c: colorFor3KeyIndex(1), d: 500 },
            { k: 2, c: colorFor3KeyIndex(2), d: 500 },
            { k: 1, c: colorFor3KeyIndex(1), d: 500 },
            { k: 0, c: colorFor3KeyIndex(0), d: 500 }
          ]
        },
        uploadLines: []
      },
      {
        id: '1',
        name: 'Up & Down',
        description: 'Sequence 1: three-key ascending/descending (adapted).',
        data: {
          steps: [
            { k: 0, c: colorFor3KeyIndex(0), d: 400 },
            { k: 1, c: colorFor3KeyIndex(1), d: 400 },
            { k: 1, c: colorFor3KeyIndex(1), d: 400 },
            { k: 0, c: colorFor3KeyIndex(0), d: 400 },
            { k: 2, c: colorFor3KeyIndex(2), d: 400 },
            { k: 0, c: colorFor3KeyIndex(0), d: 400 }
          ]
        },
        uploadLines: []
      },
      {
        id: '2',
        name: 'Quick Repeat',
        description: 'Sequence 2: repeating pattern across keys 0-2.',
        data: {
          steps: [
            { k: 1, c: colorFor3KeyIndex(1), d: 500 },
            { k: 1, c: colorFor3KeyIndex(1), d: 500 },
            { k: 0, c: colorFor3KeyIndex(0), d: 500 },
            { k: 2, c: colorFor3KeyIndex(2), d: 500 },
            { k: 2, c: colorFor3KeyIndex(2), d: 500 },
            { k: 1, c: colorFor3KeyIndex(1), d: 500 }
          ]
        },
        uploadLines: []
      },
      {
        id: '3',
        name: 'Sweep',
        description: 'Sequence 3: slow-fast-slow arc across RGB keys (0,1,2).',
        data: {
          steps: [
            { k: 0, c: colorFor3KeyIndex(0), d: 600 },
            { k: 1, c: colorFor3KeyIndex(1), d: 500 },
            { k: 2, c: colorFor3KeyIndex(2), d: 400 },
            { k: 0, c: colorFor3KeyIndex(0), d: 300 },
            { k: 1, c: colorFor3KeyIndex(1), d: 300 },
            { k: 2, c: colorFor3KeyIndex(2), d: 300 },
            { k: 0, c: colorFor3KeyIndex(0), d: 300 },
            { k: 1, c: colorFor3KeyIndex(1), d: 300 },
            { k: 2, c: colorFor3KeyIndex(2), d: 300 },
            { k: 0, c: colorFor3KeyIndex(0), d: 300 },
            { k: 1, c: colorFor3KeyIndex(1), d: 400 },
            { k: 2, c: colorFor3KeyIndex(2), d: 500 },
            { k: 0, c: colorFor3KeyIndex(0), d: 600 },
            { k: 1, c: colorFor3KeyIndex(1), d: 700 }
          ]
        },
        uploadLines: []
      },
      {
        id: '4',
        name: 'Syncopated',
        description: 'Sequence 4: irregular rhythm across all keys.',
        data: {
          steps: [
            { k: 0, c: colorFor3KeyIndex(0), d: 300 },
            { k: 2, c: colorFor3KeyIndex(2), d: 300 },
            { k: 1, c: colorFor3KeyIndex(1), d: 600 },
            { k: 0, c: colorFor3KeyIndex(0), d: 300 },
            { k: 2, c: colorFor3KeyIndex(2), d: 300 },
            { k: 1, c: colorFor3KeyIndex(1), d: 300 },
            { k: 0, c: colorFor3KeyIndex(0), d: 300 },
            { k: 1, c: colorFor3KeyIndex(1), d: 300 },
            { k: 2, c: colorFor3KeyIndex(2), d: 300 },
            { k: 0, c: colorFor3KeyIndex(0), d: 500 },
            { k: 1, c: colorFor3KeyIndex(1), d: 300 },
            { k: 2, c: colorFor3KeyIndex(2), d: 300 },
            { k: 0, c: colorFor3KeyIndex(0), d: 400 },
            { k: 1, c: colorFor3KeyIndex(1), d: 300 },
            { k: 2, c: colorFor3KeyIndex(2), d: 300 },
            { k: 0, c: colorFor3KeyIndex(0), d: 700 }
          ]
        },
        uploadLines: []
      },
      {
        id: '5',
        name: 'Ode to Joy',
        description: 'Sequence 5: Ode to Joy (adapted for 3 keys).',
        data: {
          steps: [
            { k: 1, c: colorFor3KeyIndex(1), d: 400 },
            { k: 1, c: colorFor3KeyIndex(1), d: 400 },
            { k: 0, c: colorFor3KeyIndex(0), d: 400 },
            { k: 0, c: colorFor3KeyIndex(0), d: 400 },
            { k: 0, c: colorFor3KeyIndex(0), d: 400 },
            { k: 0, c: colorFor3KeyIndex(0), d: 400 },
            { k: 1, c: colorFor3KeyIndex(1), d: 400 },
            { k: 1, c: colorFor3KeyIndex(1), d: 400 },
            { k: 2, c: colorFor3KeyIndex(2), d: 400 },
            { k: 2, c: colorFor3KeyIndex(2), d: 400 },
            { k: 1, c: colorFor3KeyIndex(1), d: 400 },
            { k: 1, c: colorFor3KeyIndex(1), d: 400 },
            { k: 1, c: colorFor3KeyIndex(1), d: 600 },
            { k: 2, c: colorFor3KeyIndex(2), d: 400 },
            { k: 2, c: colorFor3KeyIndex(2), d: 800 }
          ]
        },
        uploadLines: []
      },
      {
        id: '6',
        name: 'Lullaby',
        description: 'Sequence 6: gentle arpeggio (adapted for 3 keys).',
        data: {
          steps: [
            { k: 2, c: colorFor3KeyIndex(2), d: 500 },
            { k: 1, c: colorFor3KeyIndex(1), d: 500 },
            { k: 0, c: colorFor3KeyIndex(0), d: 500 },
            { k: 1, c: colorFor3KeyIndex(1), d: 500 },
            { k: 2, c: colorFor3KeyIndex(2), d: 500 },
            { k: 1, c: colorFor3KeyIndex(1), d: 500 },
            { k: 0, c: colorFor3KeyIndex(0), d: 700 },
            { k: 0, c: colorFor3KeyIndex(0), d: 300 },
            { k: 1, c: colorFor3KeyIndex(1), d: 500 },
            { k: 2, c: colorFor3KeyIndex(2), d: 500 },
            { k: 1, c: colorFor3KeyIndex(1), d: 400 },
            { k: 0, c: colorFor3KeyIndex(0), d: 600 },
            { k: 1, c: colorFor3KeyIndex(1), d: 500 },
            { k: 2, c: colorFor3KeyIndex(2), d: 900 }
          ]
        },
        uploadLines: []
      },
      {
        id: '7',
        name: 'Mary Had a Lamb',
        description: 'Sequence 7: Mary Had a Little Lamb (adapted for 3 keys).',
        data: {
          steps: [
            { k: 1, c: colorFor3KeyIndex(1), d: 400 },
            { k: 2, c: colorFor3KeyIndex(2), d: 400 },
            { k: 2, c: colorFor3KeyIndex(2), d: 400 },
            { k: 2, c: colorFor3KeyIndex(2), d: 400 },
            { k: 1, c: colorFor3KeyIndex(1), d: 400 },
            { k: 1, c: colorFor3KeyIndex(1), d: 400 },
            { k: 1, c: colorFor3KeyIndex(1), d: 800 },
            { k: 2, c: colorFor3KeyIndex(2), d: 400 },
            { k: 2, c: colorFor3KeyIndex(2), d: 400 },
            { k: 2, c: colorFor3KeyIndex(2), d: 800 },
            { k: 1, c: colorFor3KeyIndex(1), d: 400 },
            { k: 0, c: colorFor3KeyIndex(0), d: 400 },
            { k: 0, c: colorFor3KeyIndex(0), d: 800 }
          ]
        },
        uploadLines: []
      },
      {
        id: '8',
        name: 'Mary Had a Little Lamb (12-key)',
        description: 'Right-hand only. Thumb=Red (C), Index=Yellow (D), Middle=Green (E), Ring=Orange (F), Pinky=Blue (G). Slow pace for guided/teaching tests.',
        data: {
          steps: [
            { k: 4, c: colorFor12KeyIndex(4), d: 700 },
            { k: 2, c: colorFor12KeyIndex(2), d: 700 },
            { k: 0, c: colorFor12KeyIndex(0), d: 700 },
            { k: 2, c: colorFor12KeyIndex(2), d: 700 },
            { k: 4, c: colorFor12KeyIndex(4), d: 700 },
            { k: 4, c: colorFor12KeyIndex(4), d: 700 },
            { k: 4, c: colorFor12KeyIndex(4), d: 1000 },

            { k: 2, c: colorFor12KeyIndex(2), d: 700 },
            { k: 2, c: colorFor12KeyIndex(2), d: 700 },
            { k: 2, c: colorFor12KeyIndex(2), d: 1000 },

            { k: 4, c: colorFor12KeyIndex(4), d: 700 },
            { k: 7, c: colorFor12KeyIndex(7), d: 700 },
            { k: 7, c: colorFor12KeyIndex(7), d: 1000 },

            { k: 4, c: colorFor12KeyIndex(4), d: 700 },
            { k: 2, c: colorFor12KeyIndex(2), d: 700 },
            { k: 0, c: colorFor12KeyIndex(0), d: 700 },
            { k: 2, c: colorFor12KeyIndex(2), d: 700 },
            { k: 4, c: colorFor12KeyIndex(4), d: 700 },
            { k: 4, c: colorFor12KeyIndex(4), d: 700 },
            { k: 4, c: colorFor12KeyIndex(4), d: 1000 },

            { k: 4, c: colorFor12KeyIndex(4), d: 700 },
            { k: 2, c: colorFor12KeyIndex(2), d: 700 },
            { k: 2, c: colorFor12KeyIndex(2), d: 700 },
            { k: 4, c: colorFor12KeyIndex(4), d: 700 },
            { k: 2, c: colorFor12KeyIndex(2), d: 700 },
            { k: 0, c: colorFor12KeyIndex(0), d: 1200 }
          ]
        },
        uploadLines: []
      }
    ];

    for (const s of presets) {
      upsertSequence(s);
    }

    res.json({ ok: true, message: 'Seeded preset sequences', items: listSequences() });
  } catch (e) {
    res.status(500).json({ ok: false, error: e.message });
  }
});

// Uploads one DB sequence to the device (over active transport).
// This overwrites the device's previously uploaded sequence (firmware holds only one uploaded sequence).
app.post('/api/db/sequences/:id/upload', async (req, res) => {
  try {
    refreshConnectionState();
    if (!controllerState.connected) {
      res.status(400).json({ ok: false, error: 'Not connected to device transport' });
      return;
    }

    const id = String(req.params.id || '').trim();
    if (!id) {
      res.status(400).json({ ok: false, error: 'Missing id' });
      return;
    }

    const seq = getSequence(id);
    if (!seq) {
      res.status(404).json({ ok: false, error: 'Sequence not found' });
      return;
    }

    let lines = Array.isArray(seq.uploadLines) ? seq.uploadLines : [];

    // If uploadLines are not stored, generate them from the software model.
    if (!lines.length) {
      const gen = generateUploadLinesFromData(seq.id, seq.name, seq.data);
      if (!gen.ok) {
        res.status(400).json({ ok: false, error: gen.error });
        return;
      }
      lines = gen.lines;
    }

    // Send lines one-by-one. Firmware parsing is line-based.
    const sent = [];
    for (const line of lines) {
      const r = sendRawLine(String(line));
      sent.push({ line, result: r });
      if (r && r.error) {
        res.status(500).json({ ok: false, error: r.error, sent });
        return;
      }
    }

    res.json({ ok: true, uploaded: id, sentCount: sent.length, sent });
  } catch (e) {
    res.status(500).json({ ok: false, error: e.message });
  }
});

// ============ START SERVER ============

app.listen(PORT, () => {
    console.log(`\nOPEN OCTAVE CONTROLLER`);
    console.log(`Server running at: http://localhost:${PORT}`);
    console.log(`Mode: ${activeTransport}\n`);
});
