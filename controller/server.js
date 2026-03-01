require('dotenv').config();
const express = require('express');
const cors = require('cors');
const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');
const WebSocket = require('ws');

const app = express();
const PORT = process.env.APP_PORT || 3000;
const COMM_MODE = process.env.COMM_MODE || 'WIFI';
const ESP32_IP = process.env.ESP32_IP || '192.168.4.1';
const WS_PORT = process.env.WS_PORT || 81;
const SERIAL_PATH = process.env.SERIAL_PORT;
const BAUD_RATE = 115200;

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

// ============ SERIAL MODE SETUP ============

let port;
let parser;

if (COMM_MODE === 'SERIAL') {
    console.log(`[INIT] Starting in SERIAL mode on ${SERIAL_PATH}...`);
    try {
        port = new SerialPort({ path: SERIAL_PATH, baudRate: BAUD_RATE });
        parser = port.pipe(new ReadlineParser({ delimiter: '\n' }));
        
        port.on('open', () => console.log('[SERIAL] Port Open'));
        port.on('error', (err) => console.error('[SERIAL] Error: ', err.message));
        
        // Print incoming serial data from the ESP32
        parser.on('data', (data) => {
            const msg = data.toString().trim();
            if (msg.length === 0) return;

            console.log(`[ESP32] ${msg}`);
            // keep a copy for the UI Logs tab.
            pushLog('ESP32', msg);
        });
        
    } catch (err) {
        console.error(`[FATAL] Could not open serial port: ${err.message}`);
    }
}

// ============ WEBSOCKET MODE SETUP ============

let ws;                         // WebSocket client instance
let wsConnected = false;        // Connection state
let wsReconnectTimer = null;    // Reconnect timer reference
let wsReconnectDelay = 1000;    // Initial reconnect delay (ms)
const WS_MAX_RECONNECT_DELAY = 10000;  // Cap at 10 seconds

// Connects (or reconnects) the WebSocket client to the ESP32.
// Uses exponential backoff: 1s -> 2s -> 4s -> 8s -> 10s (capped).
function connectWebSocket() {
    if (COMM_MODE !== 'WIFI') return;
    
    const wsUrl = `ws://${ESP32_IP}:${WS_PORT}`;
    
    console.log(`[WS] Connecting to ${wsUrl}...`);
    
    ws = new WebSocket(wsUrl);
    
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
        }
    });
    
    ws.on('close', () => {
        wsConnected = false;
        console.log('[WS] Connection closed');
        scheduleReconnect();
    });
    
    ws.on('error', (err) => {
        // Suppress noisy ECONNREFUSED errors during reconnect attempts
        if (err.code !== 'ECONNREFUSED') {
            console.error(`[WS] Error: ${err.message}`);
        }
        // 'close' event will fire after error, triggering reconnect
    });
}

// Schedules a reconnection attempt with exponential backoff.
function scheduleReconnect() {
    if (wsReconnectTimer) return;  // Already scheduled
    
    console.log(`[WS] Reconnecting in ${wsReconnectDelay / 1000}s...`);
    wsReconnectTimer = setTimeout(() => {
        wsReconnectTimer = null;
        connectWebSocket();
    }, wsReconnectDelay);
    
    // Exponential backoff: double the delay each time, capped at max
    wsReconnectDelay = Math.min(wsReconnectDelay * 2, WS_MAX_RECONNECT_DELAY);
}

if (COMM_MODE === 'WIFI') {
    console.log(`[INIT] Starting in WIFI (WebSocket) mode targeting ${ESP32_IP}...`);
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

    if (COMM_MODE === 'WIFI') {
        return sendWsCommand(cmd);
    }

    if (COMM_MODE === 'SERIAL') {
        return sendSerialCommand(cmd);
    }
}

function sendWsCommand(cmd) {
    if (!ws || !wsConnected) {
        console.error('[WS] Not connected to ESP32');
        return { error: 'WebSocket not connected' };
    }

    console.log(`[WS] Sending: '${cmd}'`);
    // record controller actions for UI debugging.
    pushLog('CTRL', `WS send: ${cmd}`);
    ws.send(cmd);
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
    const serialPayload = `${cmd}\n`;

    console.log(`[SERIAL] Sending: '${cmd}' (with newline)`);
    // record controller actions for UI debugging.
    pushLog('CTRL', `SERIAL send: ${cmd}`);
    port.write(serialPayload);

    return { success: true, mode: 'serial', cmd: cmd };
}

// ============ API ROUTES ============

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
    const mode = process.env.COMM_MODE || 'WIFI';

    const wsConnected = !!(ws && ws.readyState === WebSocket.OPEN);
    const serialOpen = !!(port && port.isOpen);

    res.json({
        ok: true,
        mode: mode,
        appPort: process.env.APP_PORT || 3000,
        wifi: {
            target: `ws://${process.env.ESP32_IP || '192.168.4.1'}:${process.env.WS_PORT || 81}`,
            connected: wsConnected
        },
        serial: {
            port: process.env.SERIAL_PORT || null,
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

// ============ START SERVER ============

app.listen(PORT, () => {
    console.log(`\nOPEN OCTAVE CONTROLLER`);
    console.log(`Server running at: http://localhost:${PORT}`);
    console.log(`Mode: ${COMM_MODE}\n`);
});
