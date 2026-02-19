require('dotenv').config();
const express = require('express');
const cors = require('cors');
const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');
const WebSocket = require('ws');

const app = express();
const PORT = process.env.APP_PORT || 3000;

const COMM_MODE = process.env.COMM_MODE || 'WIFI';
const ESP_HOST = process.env.ESP32_HOST;    // e.g. "192.168.4.1"
const WS_PORT = process.env.WS_PORT || 81;  // WebSocket port on the ESP32
const SERIAL_PATH = process.env.SERIAL_PORT || '/dev/ttyACM0';
const BAUD_RATE = 115200;

app.use(cors());
app.use(express.json());

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
            console.log(`[ESP32] ${data.toString().trim()}`);
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
    
    // Strip any trailing slash and protocol from ESP_HOST for the WS URL
    const host = ESP_HOST.replace(/^https?:\/\//, '').replace(/\/$/, '');
    const wsUrl = `ws://${host}:${WS_PORT}`;
    
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
    console.log(`[INIT] Starting in WIFI (WebSocket) mode targeting ${ESP_HOST}...`);
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
    ws.send(cmd);
    return { success: true, mode: 'websocket', cmd: cmd };
}

function sendSerialCommand(cmd) {
    if (!port || !port.isOpen) {
        console.error('[SERIAL] Port closed');
        return { error: 'Serial port closed' };
    }

    console.log(`[SERIAL] Sending: '${cmd}'`);
    port.write(cmd);
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

// ============ START SERVER ============

app.listen(PORT, () => {
    console.log(`\nOPEN OCTAVE CONTROLLER`);
    console.log(`Server running at: http://localhost:${PORT}`);
    console.log(`Mode: ${COMM_MODE}\n`);
});
