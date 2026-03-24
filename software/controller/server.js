require('dotenv').config();
const express = require('express');
const cors = require('cors');
const multer = require('multer');
const { importMidiBufferToSequence } = require('./services/midi_import');
const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');
const WebSocket = require('ws');


// SQLite sequence library (software-side)
const {
    DB_PATH,
    listSequences,
    getSequence,
    upsertSequence,
    deleteSequence,
    generateUploadLinesFromData,
    computeMaxKey
} = require('./database/sqlite');

// Shared colour definitions (single source of truth for controller + frontend)
const COLORS = require('../shared/colors.json');

const app = express();

const PORT = process.env.APP_PORT || 3000;
// const WS_PORT = process.env.WS_PORT || 81;
// laptop hosting
// Use a non-privileged default WebSocket port so the controller can run on
// macOS/Windows/Linux laptops without requiring sudo/admin privileges.
const WS_PORT = Number(process.env.WS_PORT || 8081);
const SERIAL_PATH = process.env.SERIAL_PORT;
const BAUD_RATE = 115200;
// Demo stability patch: be more tolerant of short WiFi/WebSocket hiccups
// so the UI does not flicker between connected and disconnected too quickly.
const MODULE_STALE_TIMEOUT_MS = Number(process.env.MODULE_STALE_TIMEOUT_MS || 30000);
const MODULE_CLEANUP_INTERVAL_MS = Number(process.env.MODULE_CLEANUP_INTERVAL_MS || 5000);

// Module counter — assigns labels (Module A, Module B, ...) in connect order
let moduleCounter = 0;

app.use(cors());
app.use(express.json());

// midi import
// In-memory upload for teacher MIDI imports.
const upload = multer({
    storage: multer.memoryStorage(),
    limits: {
        fileSize: 2 * 1024 * 1024 // 2 MB is plenty for teacher-uploaded MIDI files
    }
});

// ============ LOG BUFFER (UI SUPPORT) ============
const LOG_BUFFER_MAX = 500;
const logBuffer = [];

function pushLog(source, message) {
    const line = {
        ts: new Date().toISOString(),
        source: source,
        message: message
    };

    logBuffer.push(line);
    if (logBuffer.length > LOG_BUFFER_MAX) {
        logBuffer.shift();
    }
}

// ============ MODULE REGISTRY ============
// In-memory registry of connected ESP32 modules, keyed by IP address.
const modules = new Map();

function getModuleLabel(ip) {
    // Reuse existing label if module has connected before
    const existing = modules.get(ip);
    if (existing && existing.label) return existing.label;

    // Assign next letter (Module A, Module B, ...)
    const letter = String.fromCharCode(65 + moduleCounter);
    moduleCounter++;
    return `Module ${letter}`;
}

function registerModule(ip, ws) {
    const existing = modules.get(ip);
    const now = new Date().toISOString();
    const entry = {
        ip,
        ws,
        connected: true,
        connectedAt: now,
        lastSeenAt: now,
        chainLength: existing?.chainLength || 1,
        totalKeys: existing?.totalKeys || 12,
        currentSequenceId: existing?.currentSequenceId || null,
        currentSequenceName: existing?.currentSequenceName || null,
        // octaveOffset: 0, // octave offset
        lastStatus: existing?.lastStatus || null,
        lastAck: existing?.lastAck || null,
        lastError: existing?.lastError || null,
        label: getModuleLabel(ip)
    };
    modules.set(ip, entry);
    pushLog('WS', `Module registered: ${entry.label} (${ip}), chain=${entry.chainLength}`);
    console.log(`[WS] Module registered: ${entry.label} (${ip})`);
    return entry;
}

function touchModule(ip) {
    const entry = modules.get(ip);
    if (!entry) return null;
    entry.lastSeenAt = new Date().toISOString();
    return entry;
}

function unregisterModule(ip) {
    const entry = modules.get(ip);
    if (entry) {
        const wasConnected = entry.connected;
        entry.ws = null;
        entry.connected = false;
        if (wasConnected) {
            pushLog('WS', `Module disconnected: ${entry.label} (${ip})`);
            console.log(`[WS] Module disconnected: ${entry.label} (${ip})`);
        }
    }
}

function getModuleSnapshot(entry) {
    return {
        ip: entry.ip,
        label: entry.label,
        connected: entry.connected,
        connectedAt: entry.connectedAt,
        lastSeenAt: entry.lastSeenAt,
        chainLength: entry.chainLength,
        totalKeys: entry.totalKeys,
        currentSequenceId: entry.currentSequenceId,
        currentSequenceName: entry.currentSequenceName,
        // octaveOffset: entry.octaveOffset ?? 0, // octave offset
        lastStatus: entry.lastStatus
    };
}

function getAllModuleSnapshots() {
    const result = [];
    for (const entry of modules.values()) {
        if (entry.connected) {
            result.push(getModuleSnapshot(entry));
        }
    }
    return result;
}

// ============ MESSAGE PARSING ============

function parseKeyValuePairs(line) {
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

function ingestEsp32Line(msg, moduleIp) {
    let normalized = String(msg);
    const m = normalized.match(/^\[[^\]]+\]\s+(.*)$/);
    if (m && m[1]) {
        normalized = m[1];
    }

    touchModule(moduleIp);
    // Handle HELLO protocol
    if (normalized.startsWith('HELLO ')) {
        const fields = parseKeyValuePairs(normalized);
        const chainLength = parseInt(fields.modules, 10) || 1;
        const entry = modules.get(moduleIp);
        if (entry) {
            entry.chainLength = chainLength;
            entry.totalKeys = chainLength * 12;
            pushLog('WS', `${entry.label} (${moduleIp}): HELLO modules=${chainLength}`);
            console.log(`[WS] ${entry.label}: HELLO modules=${chainLength}, totalKeys=${entry.totalKeys}`);
        }
        return;
    }

    const entry = modules.get(moduleIp);

    if (normalized.startsWith('ACK ')) {
        if (entry) {
            entry.lastAck = {
                ts: new Date().toISOString(),
                raw: msg,
                fields: parseKeyValuePairs(normalized)
            };
        }
        return;
    }

    if (normalized.startsWith('STATUS ')) {
        const fields = parseKeyValuePairs(normalized);
        if (entry) {
            const octaveOffset = parseInt(fields.octaveOffset, 10);
            // entry.octaveOffset = Number.isFinite(octaveOffset) ? octaveOffset : (entry.octaveOffset ?? 0); // octave offset
            entry.lastStatus = {
                ts: new Date().toISOString(),
                running: fields.running === '1',
                seq: parseInt(fields.seq, 10) || -1,
                step: parseInt(fields.step, 10) || -1,
                mode: fields.mode || 'N/A',
                // octaveOffset: entry.octaveOffset // octave offset
            };
        }
        return;
    }

    if (normalized.startsWith('ERR ')) {
        if (entry) {
            entry.lastError = {
                ts: new Date().toISOString(),
                raw: msg
            };
        }
        return;
    }
}


function cleanupStaleModules() {
    const now = Date.now();

    for (const [ip, entry] of modules) {
        if (!entry.connected) continue;

        const lastSeenMs = entry.lastSeenAt ? Date.parse(entry.lastSeenAt) : NaN;
        const staleByTime = !Number.isFinite(lastSeenMs) || (now - lastSeenMs > MODULE_STALE_TIMEOUT_MS);
        const socketMissing = !entry.ws;
        const socketClosed = entry.ws && entry.ws.readyState !== WebSocket.OPEN;

        // Demo stability patch: only force-remove immediately if the socket is
        // truly missing/closed. For stale modules, wait for the longer timeout
        // before removing them so short hiccups do not flicker the UI.
        if (socketMissing || socketClosed) {
            if (entry.ws) {
                try {
                    entry.ws.terminate();
                } catch (_) {}
            }
            unregisterModule(ip);
            continue;
        }

        if (staleByTime) {
            try {
                entry.ws.ping();
            } catch (_) {}

            if (entry.ws && entry.ws.readyState !== WebSocket.OPEN) {
                try {
                    entry.ws.terminate();
                } catch (_) {}
                unregisterModule(ip);
            }
        }
    }
}

// ============ COMMAND ROUTING ============

function sendToModule(ip, command) {
    const entry = modules.get(ip);
    if (!entry || !entry.ws || entry.ws.readyState !== WebSocket.OPEN) {
        return { error: `Module ${ip} not connected` };
    }

    const payload = frameForWebSocket(command);
    entry.ws.send(payload);
    pushLog('CTRL', `-> ${entry.label} (${ip}): ${command}`);
    return { success: true, module: ip, cmd: command };
}

function broadcastToAll(command) {
    const results = [];
    for (const [ip, entry] of modules) {
        if (entry.connected && entry.ws && entry.ws.readyState === WebSocket.OPEN) {
            const payload = frameForWebSocket(command);
            entry.ws.send(payload);
            pushLog('CTRL', `-> ${entry.label} (${ip}): ${command}`);
            results.push({ module: ip, success: true });
        }
    }
    return results;
}

// ============ TRANSPORT FRAMING ============

function trimLineEndings(s) {
    return s.replace(/[\r\n]+$/g, '');
}

function frameForWebSocket(cmd) {
    const clean = trimLineEndings(String(cmd));
    if (clean.length === 1) {
        return clean;
    }
    return clean.endsWith('\n') ? clean : `${clean}\n`;
}

function frameForSerial(cmd) {
    const clean = trimLineEndings(String(cmd));
    return `${clean}\n`;
}

// ============ SERIAL MODE SETUP (legacy fallback) ============

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
    const pathToUse = pathOverride || SERIAL_PATH;

    if (!pathToUse) {
        console.error('[SERIAL] SERIAL_PORT is not set');
        return { error: 'SERIAL_PORT not set' };
    }

    if (port && port.isOpen) {
        const existingPath = port.path || SERIAL_PATH;
        if (existingPath === pathToUse) {
            return { success: true, mode: 'serial', alreadyOpen: true, port: pathToUse };
        }
        console.log(`[SERIAL] Port already open on ${existingPath}; switching to ${pathToUse}...`);
        await closeSerialPortAsync();
        port = undefined;
        parser = undefined;
    }

    console.log(`[INIT] Starting SERIAL mode on ${pathToUse}...`);

    try {
        port = new SerialPort({ path: pathToUse, baudRate: BAUD_RATE });
        parser = port.pipe(new ReadlineParser({ delimiter: '\n' }));

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

        parser.on('data', (data) => {
            const msg = data.toString().trim();
            if (msg.length === 0) return;
            console.log(`[ESP32-SERIAL] ${msg}`);
            pushLog('ESP32', msg);
            ingestEsp32Line(msg, 'serial');
        });

        return { success: true, mode: 'serial', port: pathToUse };
    } catch (err) {
        console.error(`[FATAL] Could not open serial port: ${err.message}`);
        return { error: `Could not open serial port: ${err.message}` };
    }
}

function disconnectSerial() {
    if (!port) {
        return { success: true, mode: 'serial', alreadyClosed: true };
    }
    if (port.isOpen) {
        console.log('[SERIAL] Closing port...');
        try {
            port.close((err) => {
                if (err) console.error('[SERIAL] Close error:', err.message);
            });
        } catch (e) {
            console.error('[SERIAL] Close error:', e.message);
        }
    }
    port = undefined;
    parser = undefined;
    return { success: true, mode: 'serial', closed: true };
}

function sendSerialCommand(cmd) {
    if (!port || !port.isOpen) {
        console.error('[SERIAL] Port closed');
        return { error: 'Serial port closed' };
    }
    const serialPayload = frameForSerial(cmd);
    console.log(`[SERIAL] Sending: '${cmd}'`);
    pushLog('CTRL', `SERIAL send: ${cmd}`);
    port.write(serialPayload);
    return { success: true, mode: 'serial', cmd: cmd };
}

// Helper: send a raw line to a specific module or via serial fallback
function sendRawLine(line, moduleIp) {
    if (typeof line !== 'string' || line.trim().length === 0) {
        return { error: 'Empty upload line' };
    }

    if (moduleIp && moduleIp !== 'serial') {
        return sendToModule(moduleIp, line);
    }

    // Serial fallback
    if (port && port.isOpen) {
        return sendSerialCommand(line);
    }

    return { error: 'No transport available' };
}

// Sends all upload lines to a single module. Returns { ok, sent, error }.
function uploadLinesToModule(ip, lines) {
    const sent = [];
    for (const line of lines) {
        const r = sendToModule(ip, String(line));
        sent.push({ line, result: r });
        if (r && r.error) {
            return { ok: false, error: r.error, sent };
        }
    }
    return { ok: true, sent, sentCount: sent.length };
}

// Broadcasts all upload lines to every connected module. Returns per-module results.
function uploadLinesToAllModules(lines) {
    const moduleResults = [];
    for (const [ip, entry] of modules) {
        if (!entry.connected || !entry.ws || entry.ws.readyState !== WebSocket.OPEN) continue;
        const result = uploadLinesToModule(ip, lines);
        moduleResults.push({ module: ip, sentCount: result.sentCount, failed: !result.ok });
    }
    return moduleResults;
}

// Sends all upload lines via serial. Returns { ok, sent, error }.
function uploadLinesToSerial(lines) {
    const sent = [];
    for (const line of lines) {
        const r = sendSerialCommand(String(line));
        sent.push({ line, result: r });
        if (r && r.error) {
            return { ok: false, error: r.error, sent };
        }
    }
    return { ok: true, sent, sentCount: sent.length };
}

// Marks a module's current sequence after a successful upload.
function markModuleSequence(ip, seq) {
    const entry = modules.get(ip);
    if (entry) {
        entry.currentSequenceId = seq.id;
        entry.currentSequenceName = seq.name;
    }
}

// Marks all connected modules' current sequence after a successful upload.
function markAllModulesSequence(seq) {
    for (const entry of modules.values()) {
        if (entry.connected) {
            entry.currentSequenceId = seq.id;
            entry.currentSequenceName = seq.name;
        }
    }
}

// ============ COMMAND TRANSLATION (firmware v5) ============

function translateToSerialCmd(endpoint, query) {
    if (endpoint === '/api/seq/control') {
        if (query.cmd === 'start') {
            if (query.mode === 'guided')  return 'g';
            if (query.mode === 'teaching') return 't';
            return 'g';
        }
        if (query.cmd === 'stop')  return 'x';
    }

    if (endpoint === '/api/test') {
        if (query.target === 'leds')   return 'l';
        if (query.target === 'servos') return 's';
    }

    if (endpoint === '/api/seq/list') return 'c';
    if (endpoint === '/api/status')   return '?';

    return null;
}

// ============ API ROUTES ============

// Connect serial (legacy fallback for single-module debugging)
app.post('/api/connect', async (req, res) => {
    try {
        const body = req.body || {};
        const transport = String(body.transport || '').toUpperCase();

        if (transport === 'SERIAL') {
            const serialPort = body.serialPort ? String(body.serialPort) : undefined;
            const serialResult = await connectSerial(serialPort);
            res.json({ success: true, result: { transport: 'SERIAL', serial: serialResult } });
            return;
        }

        // WiFi modules connect inward; no outbound connection to establish.
        res.json({
            success: true,
            result: {
                transport: 'WIFI',
                note: 'Modules connect to the controller WS server automatically. No outbound connection needed.'
            }
        });
    } catch (e) {
        res.status(500).json({ error: e.message });
    }
});

app.post('/api/disconnect', (req, res) => {
    try {
        const body = req.body || {};
        const moduleIp = body.module;

        if (moduleIp) {
            // Disconnect a specific module
            const entry = modules.get(moduleIp);
            if (entry && entry.ws) {
                entry.ws.close();
            }
            res.json({ success: true, disconnected: moduleIp });
            return;
        }

        // Disconnect all
        for (const [ip, entry] of modules) {
            if (entry.ws) {
                try { entry.ws.close(); } catch (_) {}
            }
        }
        const serialResult = disconnectSerial();
        res.json({ success: true, result: { serial: serialResult, modulesDisconnected: modules.size } });
    } catch (e) {
        res.status(500).json({ error: e.message });
    }
});

app.post('/api/seq/control', async (req, res) => {
    try {
        const cmd = translateToSerialCmd('/api/seq/control', req.query);
        if (!cmd) {
            res.status(400).json({ error: 'Invalid command' });
            return;
        }

        const moduleIp = req.query.module;
        if (moduleIp === 'all' || !moduleIp) {
            const results = broadcastToAll(cmd);
            res.json({ success: true, cmd, results });
        } else {
            const result = sendToModule(moduleIp, cmd);
            res.json(result);
        }
    } catch (e) { res.status(500).json({ error: e.message }); }
});

app.get('/api/seq/list', async (req, res) => {
    try {
        const cmd = translateToSerialCmd('/api/seq/list', {});
        const moduleIp = req.query.module;
        if (moduleIp) {
            const result = sendToModule(moduleIp, cmd);
            res.json(result);
        } else {
            const results = broadcastToAll(cmd);
            res.json({ success: true, cmd, results });
        }
    } catch (e) { res.status(500).json({ error: e.message }); }
});

app.post('/api/test', async (req, res) => {
    try {
        const cmd = translateToSerialCmd('/api/test', req.query);
        if (!cmd) {
            res.status(400).json({ error: 'Invalid test target' });
            return;
        }

        const moduleIp = req.query.module;
        if (moduleIp === 'all' || !moduleIp) {
            const results = broadcastToAll(cmd);
            res.json({ success: true, cmd, results });
        } else {
            const result = sendToModule(moduleIp, cmd);
            res.json(result);
        }
    } catch (e) { res.status(500).json({ error: e.message }); }
});

app.get('/api/status', async (req, res) => {
    try {
        const moduleIp = req.query.module;
        if (moduleIp) {
            const entry = modules.get(moduleIp);
            if (!entry) {
                res.status(404).json({ error: 'Module not found' });
                return;
            }
            res.json({ ok: true, module: getModuleSnapshot(entry) });
        } else {
            res.json({ ok: true, modules: getAllModuleSnapshots() });
        }
    } catch (e) { res.status(503).json({ online: false }); }
});

// Health endpoint — returns module list with connection status
app.get('/api/health', (req, res) => {
    const connectedModules = getAllModuleSnapshots();
    const serialOpen = !!(port && port.isOpen);

    res.json({
        ok: true,
        wsServerPort: WS_PORT,
        appPort: PORT,
        connectedModules: connectedModules.length,
        modules: connectedModules,
        serial: {
            port: SERIAL_PATH || null,
            open: serialOpen
        }
    });
});

// Returns the shared colour palette
app.get('/api/colors', (req, res) => {
    res.json({ ok: true, ...COLORS });
});

// Returns log lines
app.get('/api/logs', (req, res) => {
    const rawTail = req.query.tail;
    const requested = rawTail ? Number(rawTail) : 200;
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

// Returns per-module state
app.get('/api/state', (req, res) => {
    const connectedModules = getAllModuleSnapshots();
    const serialOpen = !!(port && port.isOpen);

    res.json({
        ok: true,
        state: {
            wsServerPort: WS_PORT,
            connectedModules: connectedModules.length,
            modules: connectedModules,
            serial: {
                port: SERIAL_PATH || null,
                open: serialOpen
            }
        }
    });
});

app.post('/api/state/reset', (req, res) => {
    // Clear last status on all modules
    for (const entry of modules.values()) {
        entry.lastStatus = null;
        entry.lastAck = null;
        entry.lastError = null;
    }
    res.json({ ok: true, state: { modules: getAllModuleSnapshots() } });
});

// ============ MODULE ENDPOINTS ============

// laptop hosting
// IMPORTANT: keep the explicit /all routes above the parameterised /:ip routes.
// Otherwise Express matches "/all/..." as if "all" were a module IP.

// POST /api/modules/all/upload — upload sequence to ALL connected modules
app.post('/api/modules/all/upload', (req, res) => {
    try {
        const body = req.body || {};
        const sequenceId = body.sequenceId;
        const colorMode = String(body.colorMode || req.query.colorMode || 'default');

        if (!sequenceId) {
            res.status(400).json({ ok: false, error: 'Missing sequenceId in body' });
            return;
        }

        const seq = getSequence(sequenceId);
        if (!seq) {
            res.status(404).json({ ok: false, error: 'Sequence not found' });
            return;
        }

        const gen = generateUploadLinesFromData(seq.id, seq.name, seq.data, colorMode);
        if (!gen.ok) {
            res.status(400).json({ ok: false, error: gen.error });
            return;
        }

        const moduleResults = uploadLinesToAllModules(gen.lines);
        for (const mr of moduleResults) {
            if (!mr.failed) markModuleSequence(mr.module, seq);
        }

        res.json({ ok: true, uploaded: seq.id, modules: moduleResults });
    } catch (e) {
        res.status(500).json({ ok: false, error: e.message });
    }
});

// POST /api/modules/all/control — start/stop or test on ALL connected modules
app.post('/api/modules/all/control', (req, res) => {
    try {
        const body = req.body || {};
        const cmd = body.cmd;
        const mode = body.mode;

        // laptop hosting / module power control
        let serialCmd;
        if (cmd === 'start') {
            serialCmd = mode === 'teaching' ? 't' : 'g';
        } else if (cmd === 'stop') {
            serialCmd = 'x';
        } else if (cmd === 'led_test') {
            serialCmd = 'l';
        } else if (cmd === 'servo_test') {
            serialCmd = 's';
        } else if (cmd === 'power_toggle') {
            serialCmd = 'o';
        } else {
            res.status(400).json({ ok: false, error: 'cmd must be start, stop, led_test, servo_test, or power_toggle' });
            return;
        }

        const results = broadcastToAll(serialCmd);
        res.json({ ok: true, cmd: serialCmd, results });
    } catch (e) {
        res.status(500).json({ ok: false, error: e.message });
    }
});

// GET /api/modules — full module registry
app.get('/api/modules', (req, res) => {
    const all = [];
    for (const entry of modules.values()) {
        all.push(getModuleSnapshot(entry));
    }
    res.json({ ok: true, modules: all });
});

// POST /api/modules/:ip/upload — upload sequence to specific module
app.post('/api/modules/:ip/upload', (req, res) => {
    try {
        const ip = req.params.ip;
        const entry = modules.get(ip);

        if (!entry || !entry.connected) {
            res.status(404).json({ ok: false, error: `Module ${ip} not connected` });
            return;
        }

        const body = req.body || {};
        const sequenceId = body.sequenceId;
        const colorMode = String(body.colorMode || req.query.colorMode || 'default');

        if (!sequenceId) {
            res.status(400).json({ ok: false, error: 'Missing sequenceId in body' });
            return;
        }

        const seq = getSequence(sequenceId);
        if (!seq) {
            res.status(404).json({ ok: false, error: 'Sequence not found' });
            return;
        }

        const gen = generateUploadLinesFromData(seq.id, seq.name, seq.data, colorMode);
        if (!gen.ok) {
            res.status(400).json({ ok: false, error: gen.error });
            return;
        }

        const result = uploadLinesToModule(ip, gen.lines);
        if (!result.ok) {
            res.status(500).json({ ok: false, error: result.error, sent: result.sent });
            return;
        }

        markModuleSequence(ip, seq);
        res.json({ ok: true, uploaded: seq.id, module: ip, sentCount: result.sentCount, sent: result.sent });
    } catch (e) {
        res.status(500).json({ ok: false, error: e.message });
    }
});


// POST /api/modules/:ip/control — start/stop sequence or run hardware tests on a specific module
app.post('/api/modules/:ip/control', (req, res) => {
    try {
        const ip = req.params.ip;
        const body = req.body || {};
        const cmd = body.cmd; // 'start' | 'stop' | 'led_test' | 'servo_test'
        const mode = body.mode; // 'guided' | 'teaching'

        // laptop hosting / module power control
        let serialCmd;
        if (cmd === 'start') {
            serialCmd = mode === 'teaching' ? 't' : 'g';
        } else if (cmd === 'stop') {
            serialCmd = 'x';
        } else if (cmd === 'led_test') {
            serialCmd = 'l';
        } else if (cmd === 'servo_test') {
            serialCmd = 's';
        } else if (cmd === 'power_toggle') {
            serialCmd = 'o';
        } else {
            res.status(400).json({ ok: false, error: 'cmd must be start, stop, led_test, servo_test, or power_toggle' });
            return;
        }

        const result = sendToModule(ip, serialCmd);
        res.json({ ok: !result.error, ...result });
    } catch (e) {
        res.status(500).json({ ok: false, error: e.message });
    }
});

// POST /api/modules/:ip/octave-offset — set chain octave offset on a specific module
// app.post('/api/modules/:ip/octave-offset', (req, res) => {
//     try {
//         const ip = req.params.ip;
//         const body = req.body || {};
//         const rawOffset = body.octaveOffset;
//         const octaveOffset = Number(rawOffset);

//         if (!Number.isInteger(octaveOffset)) {
//             res.status(400).json({ ok: false, error: 'octaveOffset must be an integer' });
//             return;
//         }

//         if (octaveOffset < 0 || octaveOffset > 2) {
//             res.status(400).json({ ok: false, error: 'octaveOffset must be between 0 and 2' });
//             return;
//         }

//         const result = sendToModule(ip, `O v=${octaveOffset}`);
//         if (result.error) {
//             res.status(400).json({ ok: false, error: result.error });
//             return;
//         }

//         const entry = modules.get(ip);
//         if (entry) {
//             entry.octaveOffset = octaveOffset; // octave offset
//         }

//         res.json({ ok: true, module: ip, octaveOffset, result });
//     } catch (e) {
//         res.status(500).json({ ok: false, error: e.message });
//     }
// });

// midi import
// POST /api/midi/import — upload a MIDI file, convert it into an Open Octave
// sequence, save it into SQLite, and return the created item + metadata.
app.post('/api/midi/import', upload.single('file'), (req, res) => {
    try {
        if (!req.file) {
            res.status(400).json({ ok: false, error: 'No MIDI file uploaded.' });
            return;
        }

        const originalName = String(req.file.originalname || '').trim();
        const lowerName = originalName.toLowerCase();

        if (!(lowerName.endsWith('.mid') || lowerName.endsWith('.midi'))) {
            res.status(400).json({ ok: false, error: 'Only .mid or .midi files are supported.' });
            return;
        }

        const nameOverride = req.body?.name ? String(req.body.name).trim() : undefined;

        const result = importMidiBufferToSequence(req.file.buffer, {
            filename: originalName,
            nameOverride
        });

        if (!result.ok) {
            res.status(400).json({
                ok: false,
                error: result.error,
                warnings: result.warnings || []
            });
            return;
        }

        const savedId = upsertSequence(result.sequence);
        const item = getSequence(savedId);

        res.json({
            ok: true,
            message: 'MIDI imported successfully.',
            item,
            meta: result.meta,
            warnings: result.warnings || []
        });
    } catch (e) {
        res.status(500).json({ ok: false, error: e.message });
    }
});


// ============ DB API (SOFTWARE SEQUENCE LIBRARY) ============

app.get('/api/db/sequences', (req, res) => {
  try {
    res.json({ ok: true, items: listSequences(), dbPath: DB_PATH });
  } catch (e) {
    res.status(500).json({ ok: false, error: e.message });
  }
});

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

// Creates/updates a sequence in SQLite.
// Accepts both new format (keys/colors/duration) and legacy format (k/c/d).
app.post('/api/db/sequences', (req, res) => {
  try {
    const body = req.body || {};
    const id = body.id !== undefined ? String(body.id).trim() : undefined;
    const name = String(body.name || '').trim();
    const description = body.description ? String(body.description) : '';
    const steps = body.steps;

    if (!name) {
      res.status(400).json({ ok: false, error: 'Missing name' });
      return;
    }
    if (!Array.isArray(steps)) {
      res.status(400).json({ ok: false, error: 'steps must be an array' });
      return;
    }

    const seq = {
      id,
      name,
      description,
      data: { steps },
      uploadLines: []
    };

    const savedId = upsertSequence(seq);

    res.json({ ok: true, message: 'Saved sequence', item: getSequence(savedId || id) });
  } catch (e) {
    res.status(500).json({ ok: false, error: e.message });
  }
});

// DELETE a sequence
app.delete('/api/db/sequences/:id', (req, res) => {
  try {
    const id = String(req.params.id || '').trim();
    if (!id) {
      res.status(400).json({ ok: false, error: 'Missing id' });
      return;
    }

    const deleted = deleteSequence(id);
    if (!deleted) {
      res.status(404).json({ ok: false, error: 'Sequence not found' });
      return;
    }

    res.json({ ok: true, message: 'Deleted sequence', id: Number(id) });
  } catch (e) {
    res.status(500).json({ ok: false, error: e.message });
  }
});

// Seeds the demo preset sequences (updated to new format).
app.post('/api/db/sequences/seed', (req, res) => {
  try {
    function colorForFinger(f) {
      const key = String(f || '').toLowerCase();
      return COLORS.fingerColors[key] || COLORS.fallbackColor;
    }

    function colorFor3KeyIndex(k) {
      const finger = COLORS.keyToFinger3Key[String(k)];
      return colorForFinger(finger);
    }

    function colorFor12KeyIndex(k) {
      const finger = COLORS.keyToFinger[String(k)];
      return colorForFinger(finger);
    }

    // Helper: convert old {k, c, d} to new {keys, colors, duration}
    function step(k, c, d) {
      return { keys: [k], colors: [c], duration: d };
    }

    // Helper: chord step with multiple keys
    function chord(keys, colors, d) {
      return { keys, colors, duration: d };
    }

    const presets = [
      {
        id: '0',
        name: 'Ping Pong',
        description: 'Sequence 0: alternating pattern across keys 0-2.',
        data: {
          steps: [
            step(0, colorFor3KeyIndex(0), 500),
            step(1, colorFor3KeyIndex(1), 500),
            step(2, colorFor3KeyIndex(2), 500),
            step(1, colorFor3KeyIndex(1), 500),
            step(0, colorFor3KeyIndex(0), 500)
          ]
        },
        uploadLines: []
      },
      {
        id: '1',
        name: 'Up & Down',
        description: 'Sequence 1: three-key ascending/descending.',
        data: {
          steps: [
            step(0, colorFor3KeyIndex(0), 400),
            step(1, colorFor3KeyIndex(1), 400),
            step(1, colorFor3KeyIndex(1), 400),
            step(0, colorFor3KeyIndex(0), 400),
            step(2, colorFor3KeyIndex(2), 400),
            step(0, colorFor3KeyIndex(0), 400)
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
            step(1, colorFor3KeyIndex(1), 500),
            step(1, colorFor3KeyIndex(1), 500),
            step(0, colorFor3KeyIndex(0), 500),
            step(2, colorFor3KeyIndex(2), 500),
            step(2, colorFor3KeyIndex(2), 500),
            step(1, colorFor3KeyIndex(1), 500)
          ]
        },
        uploadLines: []
      },
      {
        id: '3',
        name: 'Sweep',
        description: 'Sequence 3: slow-fast-slow arc across RGB keys.',
        data: {
          steps: [
            step(0, colorFor3KeyIndex(0), 600),
            step(1, colorFor3KeyIndex(1), 500),
            step(2, colorFor3KeyIndex(2), 400),
            step(0, colorFor3KeyIndex(0), 300),
            step(1, colorFor3KeyIndex(1), 300),
            step(2, colorFor3KeyIndex(2), 300),
            step(0, colorFor3KeyIndex(0), 300),
            step(1, colorFor3KeyIndex(1), 300),
            step(2, colorFor3KeyIndex(2), 300),
            step(0, colorFor3KeyIndex(0), 300),
            step(1, colorFor3KeyIndex(1), 400),
            step(2, colorFor3KeyIndex(2), 500),
            step(0, colorFor3KeyIndex(0), 600),
            step(1, colorFor3KeyIndex(1), 700)
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
            step(0, colorFor3KeyIndex(0), 300),
            step(2, colorFor3KeyIndex(2), 300),
            step(1, colorFor3KeyIndex(1), 600),
            step(0, colorFor3KeyIndex(0), 300),
            step(2, colorFor3KeyIndex(2), 300),
            step(1, colorFor3KeyIndex(1), 300),
            step(0, colorFor3KeyIndex(0), 300),
            step(1, colorFor3KeyIndex(1), 300),
            step(2, colorFor3KeyIndex(2), 300),
            step(0, colorFor3KeyIndex(0), 500),
            step(1, colorFor3KeyIndex(1), 300),
            step(2, colorFor3KeyIndex(2), 300),
            step(0, colorFor3KeyIndex(0), 400),
            step(1, colorFor3KeyIndex(1), 300),
            step(2, colorFor3KeyIndex(2), 300),
            step(0, colorFor3KeyIndex(0), 700)
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
            step(1, colorFor3KeyIndex(1), 400),
            step(1, colorFor3KeyIndex(1), 400),
            step(0, colorFor3KeyIndex(0), 400),
            step(0, colorFor3KeyIndex(0), 400),
            step(0, colorFor3KeyIndex(0), 400),
            step(0, colorFor3KeyIndex(0), 400),
            step(1, colorFor3KeyIndex(1), 400),
            step(1, colorFor3KeyIndex(1), 400),
            step(2, colorFor3KeyIndex(2), 400),
            step(2, colorFor3KeyIndex(2), 400),
            step(1, colorFor3KeyIndex(1), 400),
            step(1, colorFor3KeyIndex(1), 400),
            step(1, colorFor3KeyIndex(1), 600),
            step(2, colorFor3KeyIndex(2), 400),
            step(2, colorFor3KeyIndex(2), 800)
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
            step(2, colorFor3KeyIndex(2), 500),
            step(1, colorFor3KeyIndex(1), 500),
            step(0, colorFor3KeyIndex(0), 500),
            step(1, colorFor3KeyIndex(1), 500),
            step(2, colorFor3KeyIndex(2), 500),
            step(1, colorFor3KeyIndex(1), 500),
            step(0, colorFor3KeyIndex(0), 700),
            step(0, colorFor3KeyIndex(0), 300),
            step(1, colorFor3KeyIndex(1), 500),
            step(2, colorFor3KeyIndex(2), 500),
            step(1, colorFor3KeyIndex(1), 400),
            step(0, colorFor3KeyIndex(0), 600),
            step(1, colorFor3KeyIndex(1), 500),
            step(2, colorFor3KeyIndex(2), 900)
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
            step(1, colorFor3KeyIndex(1), 400),
            step(2, colorFor3KeyIndex(2), 400),
            step(2, colorFor3KeyIndex(2), 400),
            step(2, colorFor3KeyIndex(2), 400),
            step(1, colorFor3KeyIndex(1), 400),
            step(1, colorFor3KeyIndex(1), 400),
            step(1, colorFor3KeyIndex(1), 800),
            step(2, colorFor3KeyIndex(2), 400),
            step(2, colorFor3KeyIndex(2), 400),
            step(2, colorFor3KeyIndex(2), 800),
            step(1, colorFor3KeyIndex(1), 400),
            step(0, colorFor3KeyIndex(0), 400),
            step(0, colorFor3KeyIndex(0), 800)
          ]
        },
        uploadLines: []
      },
      {
        id: '8',
        name: 'Mary Had a Little Lamb (12-key)',
        description: 'Right-hand only. Slow pace for guided/teaching tests.',
        data: {
          steps: [
            step(4, colorFor12KeyIndex(4), 700),
            step(2, colorFor12KeyIndex(2), 700),
            step(0, colorFor12KeyIndex(0), 700),
            step(2, colorFor12KeyIndex(2), 700),
            step(4, colorFor12KeyIndex(4), 700),
            step(4, colorFor12KeyIndex(4), 700),
            step(4, colorFor12KeyIndex(4), 1000),
            step(2, colorFor12KeyIndex(2), 700),
            step(2, colorFor12KeyIndex(2), 700),
            step(2, colorFor12KeyIndex(2), 1000),
            step(4, colorFor12KeyIndex(4), 700),
            step(7, colorFor12KeyIndex(7), 700),
            step(7, colorFor12KeyIndex(7), 1000),
            step(4, colorFor12KeyIndex(4), 700),
            step(2, colorFor12KeyIndex(2), 700),
            step(0, colorFor12KeyIndex(0), 700),
            step(2, colorFor12KeyIndex(2), 700),
            step(4, colorFor12KeyIndex(4), 700),
            step(4, colorFor12KeyIndex(4), 700),
            step(4, colorFor12KeyIndex(4), 1000),
            step(4, colorFor12KeyIndex(4), 700),
            step(2, colorFor12KeyIndex(2), 700),
            step(2, colorFor12KeyIndex(2), 700),
            step(4, colorFor12KeyIndex(4), 700),
            step(2, colorFor12KeyIndex(2), 700),
            step(0, colorFor12KeyIndex(0), 1200)
          ]
        },
        uploadLines: []
      },
      // === NEW MULTI-MODULE SEQUENCES ===
      {
        id: '9',
        name: 'Two_Octave_Scale',
        description: 'C major scale across 2 octaves, ascending and descending.',
        data: {
          steps: [
            // Ascending: C4 D4 E4 F4 G4 A4 B4 C5 D5 E5 F5 G5 A5 B5
            step(0, colorFor12KeyIndex(0), 400),
            step(2, colorFor12KeyIndex(2), 400),
            step(4, colorFor12KeyIndex(4), 400),
            step(5, colorFor12KeyIndex(5), 400),
            step(7, colorFor12KeyIndex(7), 400),
            step(9, colorFor12KeyIndex(9), 400),
            step(11, colorFor12KeyIndex(11), 400),
            step(12, colorFor12KeyIndex(0), 400),
            step(14, colorFor12KeyIndex(2), 400),
            step(16, colorFor12KeyIndex(4), 400),
            step(17, colorFor12KeyIndex(5), 400),
            step(19, colorFor12KeyIndex(7), 400),
            step(21, colorFor12KeyIndex(9), 400),
            step(23, colorFor12KeyIndex(11), 600),
            // Descending: B5 A5 G5 F5 E5 D5 C5 B4 A4 G4 F4 E4 D4 C4
            step(23, colorFor12KeyIndex(11), 400),
            step(21, colorFor12KeyIndex(9), 400),
            step(19, colorFor12KeyIndex(7), 400),
            step(17, colorFor12KeyIndex(5), 400),
            step(16, colorFor12KeyIndex(4), 400),
            step(14, colorFor12KeyIndex(2), 400),
            step(12, colorFor12KeyIndex(0), 400),
            step(11, colorFor12KeyIndex(11), 400),
            step(9, colorFor12KeyIndex(9), 400),
            step(7, colorFor12KeyIndex(7), 400),
            step(5, colorFor12KeyIndex(5), 400),
            step(4, colorFor12KeyIndex(4), 400),
            step(2, colorFor12KeyIndex(2), 400),
            step(0, colorFor12KeyIndex(0), 600)
          ]
        },
        uploadLines: []
      },
      {
        id: '10',
        name: 'Cross_Octave_Chords',
        description: 'Chords that span both octaves (C4+C5, E4+E5, G4+G5, etc.).',
        data: {
          steps: [
            chord([0, 12], [colorFor12KeyIndex(0), colorFor12KeyIndex(0)], 800),
            chord([4, 16], [colorFor12KeyIndex(4), colorFor12KeyIndex(4)], 800),
            chord([7, 19], [colorFor12KeyIndex(7), colorFor12KeyIndex(7)], 800),
            chord([0, 4, 7], [colorFor12KeyIndex(0), colorFor12KeyIndex(4), colorFor12KeyIndex(7)], 1000),
            chord([12, 16, 19], [colorFor12KeyIndex(0), colorFor12KeyIndex(4), colorFor12KeyIndex(7)], 1000),
            chord([0, 7, 12, 19], [colorFor12KeyIndex(0), colorFor12KeyIndex(7), colorFor12KeyIndex(0), colorFor12KeyIndex(7)], 1200),
            chord([5, 9], [colorFor12KeyIndex(5), colorFor12KeyIndex(9)], 800),
            chord([17, 21], [colorFor12KeyIndex(5), colorFor12KeyIndex(9)], 800),
            chord([5, 9, 12], [colorFor12KeyIndex(5), colorFor12KeyIndex(9), colorFor12KeyIndex(0)], 1000),
            chord([0, 12], [colorFor12KeyIndex(0), colorFor12KeyIndex(0)], 1200)
          ]
        },
        uploadLines: []
      },
      {
        id: '11',
        name: 'Octave_Jump',
        description: 'Alternating notes between octave 1 and octave 2.',
        data: {
          steps: [
            step(0, colorFor12KeyIndex(0), 400),
            step(12, colorFor12KeyIndex(0), 400),
            step(2, colorFor12KeyIndex(2), 400),
            step(14, colorFor12KeyIndex(2), 400),
            step(4, colorFor12KeyIndex(4), 400),
            step(16, colorFor12KeyIndex(4), 400),
            step(5, colorFor12KeyIndex(5), 400),
            step(17, colorFor12KeyIndex(5), 400),
            step(7, colorFor12KeyIndex(7), 400),
            step(19, colorFor12KeyIndex(7), 400),
            step(9, colorFor12KeyIndex(9), 400),
            step(21, colorFor12KeyIndex(9), 400),
            step(11, colorFor12KeyIndex(11), 400),
            step(23, colorFor12KeyIndex(11), 600),
            step(23, colorFor12KeyIndex(11), 400),
            step(11, colorFor12KeyIndex(11), 400),
            step(21, colorFor12KeyIndex(9), 400),
            step(9, colorFor12KeyIndex(9), 400),
            step(19, colorFor12KeyIndex(7), 400),
            step(7, colorFor12KeyIndex(7), 400),
            step(17, colorFor12KeyIndex(5), 400),
            step(5, colorFor12KeyIndex(5), 400),
            step(16, colorFor12KeyIndex(4), 400),
            step(4, colorFor12KeyIndex(4), 400),
            step(14, colorFor12KeyIndex(2), 400),
            step(2, colorFor12KeyIndex(2), 400),
            step(12, colorFor12KeyIndex(0), 400),
            step(0, colorFor12KeyIndex(0), 600)
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

// Upload one DB sequence to a specific module or via serial fallback.
app.post('/api/db/sequences/:id/upload', async (req, res) => {
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

    const colorMode = String(req.query.colorMode || 'default');
    const gen = generateUploadLinesFromData(seq.id, seq.name, seq.data, colorMode);
    if (!gen.ok) {
      res.status(400).json({ ok: false, error: gen.error });
      return;
    }
    const lines = gen.lines;

    // Determine target: specific module IP from query, or broadcast/serial
    const moduleIp = req.query.module;

    if (moduleIp && moduleIp !== 'all') {
      // Send to specific module
      const result = uploadLinesToModule(moduleIp, lines);
      if (!result.ok) {
        res.status(500).json({ ok: false, error: result.error, sent: result.sent });
        return;
      }
      markModuleSequence(moduleIp, seq);
      res.json({ ok: true, uploaded: id, sentCount: result.sentCount, sent: result.sent });
      return;
    }

    if (moduleIp === 'all') {
      // Broadcast to all modules
      const moduleResults = uploadLinesToAllModules(lines);
      markAllModulesSequence(seq);
      res.json({ ok: true, uploaded: id, broadcast: true, modules: moduleResults });
      return;
    }

    // Default: try connected modules first, then serial fallback
    const connectedModules = [...modules.values()].filter(e => e.connected && e.ws);
    if (connectedModules.length > 0) {
      const moduleResults = uploadLinesToAllModules(lines);
      markAllModulesSequence(seq);
      res.json({ ok: true, uploaded: id, sentCount: lines.length, modules: moduleResults });
      return;
    }

    // Serial fallback
    if (port && port.isOpen) {
      const result = uploadLinesToSerial(lines);
      if (!result.ok) {
        res.status(500).json({ ok: false, error: result.error, sent: result.sent });
        return;
      }
      res.json({ ok: true, uploaded: id, sentCount: result.sentCount, sent: result.sent });
      return;
    }

    res.status(400).json({ ok: false, error: 'No module connected and no serial port open' });
  } catch (e) {
    res.status(500).json({ ok: false, error: e.message });
  }
});

// ============ WEBSOCKET SERVER ============

const httpServer = app.listen(PORT, '0.0.0.0', () => {
    console.log(`\nOPEN OCTAVE CONTROLLER`);
    console.log(`HTTP server running at: http://localhost:${PORT}`);
    console.log(`WebSocket server listening on port ${WS_PORT}`);
    console.log(`Modules connect automatically via WebSocket\n`);
});

// const wss = new WebSocket.Server({ port: Number(WS_PORT) }, () => {
//     console.log(`[WS] WebSocket server listening on 0.0.0.0:${WS_PORT}`);
// });

// laptop hosting
const wss = new WebSocket.Server({ host: '0.0.0.0', port: WS_PORT }, () => {
    console.log(`[WS] WebSocket server listening on 0.0.0.0:${WS_PORT}`);
});

wss.on('connection', (socket, req) => {
    const clientIp = req.socket.remoteAddress?.replace('::ffff:', '') || 'unknown';
    console.log(`[WS] Incoming connection from ${clientIp}`);

    const entry = registerModule(clientIp, socket);
    socket.isAlive = true;

    socket.on('pong', () => {
        socket.isAlive = true;
        touchModule(clientIp);
    });

    socket.on('message', (data) => {
        const msg = data.toString().trim();
        if (msg.length === 0) return;

        socket.isAlive = true;
        touchModule(clientIp);
        console.log(`[ESP32 ${clientIp}] ${msg}`);
        pushLog(`ESP32:${clientIp}`, msg);
        ingestEsp32Line(msg, clientIp);
    });

    socket.on('close', () => {
        unregisterModule(clientIp);
    });

    socket.on('error', (err) => {
        console.error(`[WS] Error from ${clientIp}: ${err.message}`);
        unregisterModule(clientIp);
    });
});

const staleCleanupTimer = setInterval(() => {
    cleanupStaleModules();

    for (const entry of modules.values()) {
        if (!entry.connected || !entry.ws || entry.ws.readyState !== WebSocket.OPEN) continue;
        try {
            entry.ws.ping();
        } catch (_) {}
    }
}, MODULE_CLEANUP_INTERVAL_MS);

wss.on('close', () => {
    clearInterval(staleCleanupTimer);
});
