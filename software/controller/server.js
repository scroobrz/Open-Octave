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

function registerModule(ip, ws, transport = 'wifi') {
    const existing = modules.get(ip);
    const now = new Date().toISOString();
    const entry = {
        ip,
        ws,
        connected: true,
        transport,
        connectedAt: now,
        lastSeenAt: now,
        chainLength: existing?.chainLength || 1,
        totalKeys: existing?.totalKeys || 12,
        currentSequenceId: existing?.currentSequenceId || null,
        currentSequenceName: existing?.currentSequenceName || null,
        lastStatus: existing?.lastStatus || null,
        lastAck: existing?.lastAck || null,
        lastError: existing?.lastError || null,
        label: getModuleLabel(ip)
    };
    modules.set(ip, entry);
    pushLog('CTRL', `Module registered: ${entry.label} (${ip}) [${transport}], chain=${entry.chainLength}`);
    console.log(`[${transport.toUpperCase()}] Module registered: ${entry.label} (${ip})`);
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
        transport: entry.transport || 'wifi',
        connectedAt: entry.connectedAt,
        lastSeenAt: entry.lastSeenAt,
        chainLength: entry.chainLength,
        totalKeys: entry.totalKeys,
        currentSequenceId: entry.currentSequenceId,
        currentSequenceName: entry.currentSequenceName,
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

    // Handle BYE protocol — module has demoted to slave
    if (normalized === 'BYE') {
        const entry = modules.get(moduleIp);
        if (entry) {
            pushLog('CTRL', `${entry.label} (${moduleIp}): BYE — demoted to slave, pausing communication`);
            console.log(`[SERIAL] ${entry.label}: BYE — module demoted to slave`);
            entry.connected = false;
            // Keep the entry in the map so it can re-register with the same label
        }
        return;
    }

    // Handle HELLO protocol
    if (normalized.startsWith('HELLO ')) {
        const fields = parseKeyValuePairs(normalized);
        const chainLength = parseInt(fields.modules, 10) || 1;
        let entry = modules.get(moduleIp);
        if (entry) {
            // Re-activate a module that was previously BYE'd (promoted back to master)
            entry.connected = true;
            entry.chainLength = chainLength;
            entry.totalKeys = chainLength * 12;
            entry.lastSeenAt = new Date().toISOString();
            const transport = entry.transport || 'wifi';
            pushLog('CTRL', `${entry.label} (${moduleIp}): HELLO modules=${chainLength} [${transport}]`);
            console.log(`[${transport.toUpperCase()}] ${entry.label}: HELLO modules=${chainLength}, totalKeys=${entry.totalKeys}`);
        } else {
            // Entry was deleted (e.g., stale cleanup after BYE).
            // Look up the serial port to re-create the WS shim and re-register.
            let ws = null;
            let transport = 'wifi';
            for (const [portPath, sp] of serialPorts) {
                if (sp.moduleKey === moduleIp) {
                    ws = createSerialWsShim(sp.port, portPath);
                    transport = 'serial';
                    break;
                }
            }
            entry = registerModule(moduleIp, ws, transport);
            entry.chainLength = chainLength;
            entry.totalKeys = chainLength * 12;
            pushLog('CTRL', `${entry.label} (${moduleIp}): HELLO modules=${chainLength} [${transport}] (re-registered after BYE)`);
            console.log(`[${transport.toUpperCase()}] ${entry.label}: HELLO modules=${chainLength} (re-registered)`);
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
        // Remove fully disconnected entries that have been inactive for a while
        // (e.g. old serial modules that sent BYE and never came back).
        if (!entry.connected) {
            const lastSeenMs = entry.lastSeenAt ? Date.parse(entry.lastSeenAt) : NaN;
            if (!Number.isFinite(lastSeenMs) || (now - lastSeenMs > MODULE_STALE_TIMEOUT_MS)) {
                modules.delete(ip);
            }
            continue;
        }

        // Serial modules are managed by the serial port close handler, not stale cleanup
        if (entry.transport === 'serial') continue;

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
    if (!entry || !entry.connected || !entry.ws || entry.ws.readyState !== WebSocket.OPEN) {
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

// ============ MULTI-SERIAL SETUP ============
// Each serial port gets a unique module key (serial:0, serial:1, ...) and is
// registered in the modules Map with a WebSocket-like shim so that sendToModule()
// and broadcastToAll() work uniformly across transports.

// Map<portPath, { port: SerialPort, parser: ReadlineParser, moduleKey: string }>
const serialPorts = new Map();
let serialDiscoveryTimer = null;
let isDiscovering = false;

function startSerialAutoDiscovery() {
    if (serialDiscoveryTimer) return;
    
    // Run an initial discovery immediately
    discoverSerialPorts();
    
    serialDiscoveryTimer = setInterval(discoverSerialPorts, 3000);
}

async function discoverSerialPorts() {
    if (isDiscovering) return;
    isDiscovering = true;
    try {
        const ports = await SerialPort.list();
        for (const p of ports) {
            const path = p.path.toLowerCase();
            if (path.includes('usb') || path.includes('acm')) {
                if (!serialPorts.has(p.path)) {
                    const result = await connectSerial(p.path);
                    if (result.success) {
                        console.log(`[SERIAL] Auto-discovered ${p.path} -> ${result.moduleKey}`);
                        pushLog('CTRL', `Auto-discovered serial port ${p.path}`);
                    }
                }
            }
        }
    } catch (err) {
        console.error(`[SERIAL] Auto-discovery error: ${err.message}`);
    } finally {
        isDiscovering = false;
    }
}

function createSerialWsShim(serialPortObj, portPath) {
    return {
        readyState: WebSocket.OPEN,
        send: (payload) => {
            // sendToModule() pre-frames with frameForWebSocket() which adds \n
            // for multi-char commands and strips trailing newlines for single-char.
            // Serial needs a trailing \n, so re-frame for serial.
            if (!serialPortObj.isOpen) return;
            const framed = frameForSerial(payload);
            serialPortObj.write(framed);
        },
        ping: () => {},
        close: () => {},
        terminate: () => {}
    };
}

function closeSerialPortAsync(portPath) {
    return new Promise((resolve) => {
        const entry = serialPorts.get(portPath);
        if (!entry || !entry.port || !entry.port.isOpen) {
            resolve({ closed: true, alreadyClosed: true });
            return;
        }
        console.log(`[SERIAL] Closing ${portPath}...`);
        entry.port.close((err) => {
            if (err) {
                console.error(`[SERIAL] Close error on ${portPath}:`, err.message);
                resolve({ closed: false, error: err.message });
                return;
            }
            resolve({ closed: true });
        });
    });
}

async function connectSerial(portPath) {
    if (!portPath) {
        console.error('[SERIAL] No port path provided');
        return { error: 'No serial port path provided' };
    }

    const existing = serialPorts.get(portPath);
    if (existing && existing.port && existing.port.isOpen) {
        return { success: true, mode: 'serial', alreadyOpen: true, port: portPath, moduleKey: existing.moduleKey };
    }

    console.log(`[INIT] Opening serial port ${portPath}...`);

    try {
        const serialPortObj = new SerialPort({ path: portPath, baudRate: BAUD_RATE });
        const parser = serialPortObj.pipe(new ReadlineParser({ delimiter: '\n' }));

        // Use a deterministic key derived from the port path so that
        // reconnecting the same USB cable reuses the same module entry
        // instead of creating ghost entries (serial:0, serial:1, ...).
        const moduleKey = `serial:${portPath}`;

        await new Promise((resolve, reject) => {
            serialPortObj.on('open', () => {
                console.log(`[SERIAL] Port open: ${portPath} -> ${moduleKey}`);
                resolve();
            });
            serialPortObj.on('error', (err) => {
                console.error(`[SERIAL] Error on ${portPath}: ${err.message}`);
                reject(err);
            });
        });

        serialPorts.set(portPath, { port: serialPortObj, parser, moduleKey });

        // Create WS shim and register in modules Map
        const wsShim = createSerialWsShim(serialPortObj, portPath);
        registerModule(moduleKey, wsShim, 'serial');

        // Request initial state from the module (give it a moment to settle)
        setTimeout(() => {
            if (serialPortObj.isOpen) {
                serialPortObj.write('i\n');
            }
        }, 1000);

        // Ingest data from this serial port using the module key
        parser.on('data', (data) => {
            const msg = data.toString().trim();
            if (msg.length === 0) return;
            console.log(`[ESP32 ${moduleKey}] ${msg}`);
            pushLog(`ESP32:${moduleKey}`, msg);
            ingestEsp32Line(msg, moduleKey);
        });

        // Handle port disconnection (USB cable unplugged)
        serialPortObj.on('close', () => {
            console.log(`[SERIAL] Port closed: ${portPath} (${moduleKey})`);
            wsShim.readyState = WebSocket.CLOSED;
            unregisterModule(moduleKey);
            serialPorts.delete(portPath);
        });

        return { success: true, mode: 'serial', port: portPath, moduleKey };
    } catch (err) {
        console.error(`[FATAL] Could not open serial port ${portPath}: ${err.message}`);
        return { error: `Could not open serial port ${portPath}: ${err.message}` };
    }
}

async function disconnectSerial(portPath) {
    if (portPath) {
        const entry = serialPorts.get(portPath);
        if (!entry) return { success: true, mode: 'serial', alreadyClosed: true };
        await closeSerialPortAsync(portPath);
        unregisterModule(entry.moduleKey);
        serialPorts.delete(portPath);
        return { success: true, mode: 'serial', closed: true, port: portPath };
    }

    // Disconnect all serial ports
    const results = [];
    for (const [path, entry] of serialPorts) {
        await closeSerialPortAsync(path);
        unregisterModule(entry.moduleKey);
        results.push({ port: path, closed: true });
    }
    serialPorts.clear();
    return { success: true, mode: 'serial', closed: true, ports: results };
}

function sendSerialCommand(cmd, portPath) {
    if (portPath) {
        const entry = serialPorts.get(portPath);
        if (!entry || !entry.port || !entry.port.isOpen) {
            console.error(`[SERIAL] Port ${portPath} closed`);
            return { error: `Serial port ${portPath} closed` };
        }
        const serialPayload = frameForSerial(cmd);
        console.log(`[SERIAL] Sending to ${portPath}: '${cmd}'`);
        pushLog('CTRL', `SERIAL ${portPath} send: ${cmd}`);
        entry.port.write(serialPayload);
        return { success: true, mode: 'serial', cmd, port: portPath };
    }

    // Send to first open serial port (backward compat)
    for (const [path, entry] of serialPorts) {
        if (entry.port && entry.port.isOpen) {
            return sendSerialCommand(cmd, path);
        }
    }
    console.error('[SERIAL] No open serial ports');
    return { error: 'No open serial ports' };
}

// Helper: send a raw line to a specific module or via serial fallback
function sendRawLine(line, moduleIp) {
    if (typeof line !== 'string' || line.trim().length === 0) {
        return { error: 'Empty upload line' };
    }

    if (moduleIp) {
        return sendToModule(moduleIp, line);
    }

    return { error: 'No transport available' };
}

// Sends all upload lines to a single module. Returns { ok, sent, error }.
async function uploadLinesToModule(ip, lines) {
    const entry = modules.get(ip);
    if (entry) {
        entry.lastAck = null;
        entry.lastError = null;
    }

    const sent = [];
    for (const line of lines) {
        const r = sendToModule(ip, String(line));
        sent.push({ line, result: r });
        if (r && r.error) {
            return { ok: false, error: r.error, sent };
        }
        // Delay to prevent hardware serial buffer overflow (ESP32/Arduino)
        await new Promise(resolve => setTimeout(resolve, 20));

        // Early check for errors during upload stream
        if (entry && entry.lastError && entry.lastError.raw.includes('upload=')) {
            return { ok: false, error: entry.lastError.raw, sent };
        }
    }

    // Wait for final acknowledgment
    if (entry) {
        let timeout = 2500;
        while (timeout > 0) {
            if (entry.lastError && entry.lastError.raw.includes('upload=')) {
                return { ok: false, error: entry.lastError.raw, sent };
            }
            if (entry.lastAck && entry.lastAck.raw.includes('upload=end')) {
                return { ok: true, sent, sentCount: sent.length };
            }
            await new Promise(resolve => setTimeout(resolve, 50));
            timeout -= 50;
        }
        return { ok: false, error: 'ERR upload_timeout: No acknowledgment received from module', sent };
    }

    return { ok: true, sent, sentCount: sent.length };
}

// Broadcasts all upload lines to every connected module in parallel. Returns per-module results.
async function uploadLinesToAllModules(lines) {
    const promises = [];
    for (const [ip, entry] of modules) {
        if (!entry.connected || !entry.ws || entry.ws.readyState !== WebSocket.OPEN) continue;
        promises.push(
            uploadLinesToModule(ip, lines).then(result => ({
                module: ip,
                sentCount: result.sentCount,
                failed: !result.ok,
                error: result.error
            }))
        );
    }
    return Promise.all(promises);
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
            if (query.mode === 'broadcast') return 'b';
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

// Connect serial ports
app.post('/api/connect', async (req, res) => {
    try {
        const body = req.body || {};
        const transport = String(body.transport || '').toUpperCase();

        if (transport === 'SERIAL') {
            // Support single port or array of ports
            const ports = body.serialPorts || (body.serialPort ? [String(body.serialPort)] : SERIAL_PATHS);
            const results = [];
            for (const p of ports) {
                results.push(await connectSerial(p));
            }
            res.json({ success: true, result: { transport: 'SERIAL', serial: results } });
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

app.post('/api/disconnect', async (req, res) => {
    try {
        const body = req.body || {};
        const moduleIp = body.module;

        if (moduleIp) {
            // Disconnect a specific module
            const entry = modules.get(moduleIp);
            if (entry && entry.ws) {
                if (entry.transport === 'serial') {
                    // Find and close the serial port for this module
                    for (const [path, se] of serialPorts) {
                        if (se.moduleKey === moduleIp) {
                            await disconnectSerial(path);
                            break;
                        }
                    }
                } else {
                    entry.ws.close();
                }
            }
            res.json({ success: true, disconnected: moduleIp });
            return;
        }

        // Disconnect all
        for (const [ip, entry] of modules) {
            if (entry.ws && entry.transport !== 'serial') {
                try { entry.ws.close(); } catch (_) {}
            }
        }
        const serialResult = await disconnectSerial();
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
    const serialStatus = Array.from(serialPorts.entries()).map(([path, s]) => ({
        path,
        open: s.port.isOpen,
        moduleKey: s.moduleKey
    }));

    res.json({
        ok: true,
        wsServerPort: WS_PORT,
        appPort: PORT,
        connectedModules: connectedModules.length,
        modules: connectedModules,
        serial: {
            ports: serialStatus
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
    const serialStatus = Array.from(serialPorts.entries()).map(([path, s]) => ({
        path,
        open: s.port.isOpen,
        moduleKey: s.moduleKey
    }));

    res.json({
        ok: true,
        state: {
            wsServerPort: WS_PORT,
            connectedModules: connectedModules.length,
            modules: connectedModules,
            serial: {
                ports: serialStatus
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
app.post('/api/modules/all/upload', async (req, res) => {
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

        const moduleResults = await uploadLinesToAllModules(gen.lines);
        
        const failures = moduleResults.filter(mr => mr.failed);
        if (failures.length > 0) {
            const errMsgs = failures.map(f => `${f.module}: ${f.error}`).join('; ');
            res.status(400).json({ ok: false, error: `Upload failed on some modules: ${errMsgs}` });
            return;
        }

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

        let serialCmd;
        if (cmd === 'start') {
            serialCmd = mode === 'teaching' ? 't' : 'g';
        } else if (cmd === 'stop') {
            serialCmd = 'x';
        } else if (cmd === 'led_test') {
            serialCmd = 'l';
        } else if (cmd === 'servo_test') {
            serialCmd = 's';
        } else {
            res.status(400).json({ ok: false, error: 'cmd must be start, stop, led_test, or servo_test' });
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
app.post('/api/modules/:ip/upload', async (req, res) => {
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

        const result = await uploadLinesToModule(ip, gen.lines);
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

        let serialCmd;
        if (cmd === 'start') {
            if (mode === 'teaching') serialCmd = 't';
            else if (mode === 'broadcast') serialCmd = 'b';
            else serialCmd = 'g';
        } else if (cmd === 'stop') {
            serialCmd = 'x';
        } else if (cmd === 'led_test') {
            serialCmd = 'l';
        } else if (cmd === 'servo_test') {
            serialCmd = 's';
        } else {
            res.status(400).json({ ok: false, error: 'cmd must be start, stop, led_test, or servo_test' });
            return;
        }

        const result = sendToModule(ip, serialCmd);
        res.json({ ok: !result.error, ...result });
    } catch (e) {
        res.status(500).json({ ok: false, error: e.message });
    }
});

// POST /api/modules/:ip/configure-octave — set chain base octave (master only)
app.post('/api/modules/:ip/configure-octave', (req, res) => {
    try {
        const ip = req.params.ip;
        const body = req.body || {};
        const targetOctave = Number(body.targetOctave);

        if (!Number.isInteger(targetOctave)) {
            res.status(400).json({ ok: false, error: 'targetOctave must be an integer' });
            return;
        }

        if (targetOctave < 1 || targetOctave > 7) {
            res.status(400).json({ ok: false, error: 'targetOctave must be between 1 and 7' });
            return;
        }

        const result = sendToModule(ip, `p${targetOctave}`);
        if (result.error) {
            res.status(400).json({ ok: false, error: result.error });
            return;
        }

        res.json({ ok: true, module: ip, targetOctave, result });
    } catch (e) {
        res.status(500).json({ ok: false, error: e.message });
    }
});

// midi import
// POST /api/midi/import — upload a MIDI file and convert it into an Open Octave
// sequence. Returns the parsed result for review; the frontend saves it via
// POST /api/db/sequences when the user accepts.
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

        res.json({
            ok: true,
            message: 'MIDI parsed successfully.',
            sequence: result.sequence,
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
    if (steps.length > 128) {
      res.status(400).json({ ok: false, error: `Too many steps (${steps.length}). Firmware maximum is 128.` });
      return;
    }
    for (let i = 0; i < steps.length; i++) {
      const s = steps[i];
      const keys = Array.isArray(s.keys) ? s.keys : [];
      const dur = s.duration !== undefined ? s.duration : s.d;
      if (keys.length === 0) {
        res.status(400).json({ ok: false, error: `Step ${i}: missing keys` });
        return;
      }
      if (keys.length > 4) {
        res.status(400).json({ ok: false, error: `Step ${i}: max 4 keys per step` });
        return;
      }
      if (dur === undefined || dur < 300 || dur > 10000) {
        res.status(400).json({ ok: false, error: `Step ${i}: duration must be 300-10000ms` });
        return;
      }
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

    // ── Key index reference ──────────────────────────────────
    // 0=C4  1=C#4  2=D4  3=D#4  4=E4   5=F4
    // 6=F#4 7=G4   8=G#4 9=A4  10=A#4 11=B4
    // 12=C5 13=C#5 14=D5 15=D#5 16=E5  17=F5
    // 18=F#5 19=G5 20=G#5 21=A5 22=A#5 23=B5
    // colorFor12KeyIndex uses key%12 to assign finger colours.

    const C4=0, D4=2, E4=4, F4=5, G4=7, A4=9, B4=11;
    const C5=12, D5=14, E5=16, F5=17, G5=19, A5=21, B5=23;
    // Sharps/flats
    const Cs4=1, Eb4=3, Fs4=6, Gs4=8, Bb4=10;
    const Cs5=13, Eb5=15, Fs5=18, Gs5=20, Bb5=22;

    // Shorthand: colour from 12-key index (wraps via modulo internally)
    const c = (k) => colorFor12KeyIndex(k % 12);

    const presets = [
      // ═══════════ 1-OCTAVE MELODIES (keys 0-11) ═══════════
      {
        id: '0', name: 'Mary Had a Little Lamb',
        description: 'Classic nursery rhyme. Keys: C4-G4.',
        data: { steps: [
          step(E4,c(E4),500), step(D4,c(D4),500), step(C4,c(C4),500), step(D4,c(D4),500),
          step(E4,c(E4),500), step(E4,c(E4),500), step(E4,c(E4),1000),
          step(D4,c(D4),500), step(D4,c(D4),500), step(D4,c(D4),1000),
          step(E4,c(E4),500), step(G4,c(G4),500), step(G4,c(G4),1000),
          step(E4,c(E4),500), step(D4,c(D4),500), step(C4,c(C4),500), step(D4,c(D4),500),
          step(E4,c(E4),500), step(E4,c(E4),500), step(E4,c(E4),500), step(E4,c(E4),500),
          step(D4,c(D4),500), step(D4,c(D4),500), step(E4,c(E4),500), step(D4,c(D4),500),
          step(C4,c(C4),1200)
        ]}, uploadLines: []
      },
      {
        id: '1', name: 'Twinkle Twinkle',
        description: 'Twinkle Twinkle Little Star. Keys: C4-A4.',
        data: { steps: [
          step(C4,c(C4),500), step(C4,c(C4),500), step(G4,c(G4),500), step(G4,c(G4),500),
          step(A4,c(A4),500), step(A4,c(A4),500), step(G4,c(G4),1000),
          step(F4,c(F4),500), step(F4,c(F4),500), step(E4,c(E4),500), step(E4,c(E4),500),
          step(D4,c(D4),500), step(D4,c(D4),500), step(C4,c(C4),1000),
          step(G4,c(G4),500), step(G4,c(G4),500), step(F4,c(F4),500), step(F4,c(F4),500),
          step(E4,c(E4),500), step(E4,c(E4),500), step(D4,c(D4),1000),
          step(G4,c(G4),500), step(G4,c(G4),500), step(F4,c(F4),500), step(F4,c(F4),500),
          step(E4,c(E4),500), step(E4,c(E4),500), step(D4,c(D4),1000),
          step(C4,c(C4),500), step(C4,c(C4),500), step(G4,c(G4),500), step(G4,c(G4),500),
          step(A4,c(A4),500), step(A4,c(A4),500), step(G4,c(G4),1000),
          step(F4,c(F4),500), step(F4,c(F4),500), step(E4,c(E4),500), step(E4,c(E4),500),
          step(D4,c(D4),500), step(D4,c(D4),500), step(C4,c(C4),1200)
        ]}, uploadLines: []
      },
      {
        id: '2', name: 'Happy Birthday',
        description: 'Happy Birthday To You. Keys: C4-C5.',
        data: { steps: [
          step(C4,c(C4),300), step(C4,c(C4),300), step(D4,c(D4),600), step(C4,c(C4),600),
          step(F4,c(F4),600), step(E4,c(E4),1200),
          step(C4,c(C4),300), step(C4,c(C4),300), step(D4,c(D4),600), step(C4,c(C4),600),
          step(G4,c(G4),600), step(F4,c(F4),1200),
          step(C4,c(C4),300), step(C4,c(C4),300), step(C5,c(C5),600), step(A4,c(A4),600),
          step(F4,c(F4),600), step(E4,c(E4),600), step(D4,c(D4),1200),
          step(B4,c(B4),300), step(B4,c(B4),300), step(A4,c(A4),600), step(F4,c(F4),600),
          step(G4,c(G4),600), step(F4,c(F4),1200)
        ]}, uploadLines: []
      },
      {
        id: '3', name: 'Ode to Joy',
        description: 'Beethoven - Ode to Joy theme. Keys: C4-G4.',
        data: { steps: [
          step(E4,c(E4),500), step(E4,c(E4),500), step(F4,c(F4),500), step(G4,c(G4),500),
          step(G4,c(G4),500), step(F4,c(F4),500), step(E4,c(E4),500), step(D4,c(D4),500),
          step(C4,c(C4),500), step(C4,c(C4),500), step(D4,c(D4),500), step(E4,c(E4),500),
          step(E4,c(E4),700), step(D4,c(D4),300), step(D4,c(D4),1000),
          step(E4,c(E4),500), step(E4,c(E4),500), step(F4,c(F4),500), step(G4,c(G4),500),
          step(G4,c(G4),500), step(F4,c(F4),500), step(E4,c(E4),500), step(D4,c(D4),500),
          step(C4,c(C4),500), step(C4,c(C4),500), step(D4,c(D4),500), step(E4,c(E4),500),
          step(D4,c(D4),700), step(C4,c(C4),300), step(C4,c(C4),1200)
        ]}, uploadLines: []
      },
      {
        id: '4', name: 'Jingle Bells',
        description: 'Jingle Bells chorus. Keys: C4-A4.',
        data: { steps: [
          step(E4,c(E4),400), step(E4,c(E4),400), step(E4,c(E4),800),
          step(E4,c(E4),400), step(E4,c(E4),400), step(E4,c(E4),800),
          step(E4,c(E4),400), step(G4,c(G4),400), step(C4,c(C4),400), step(D4,c(D4),400),
          step(E4,c(E4),1200),
          step(F4,c(F4),400), step(F4,c(F4),400), step(F4,c(F4),400), step(F4,c(F4),400),
          step(F4,c(F4),400), step(E4,c(E4),400), step(E4,c(E4),400), step(E4,c(E4),400),
          step(E4,c(E4),400), step(D4,c(D4),400), step(D4,c(D4),400), step(E4,c(E4),400),
          step(D4,c(D4),800), step(G4,c(G4),800)
        ]}, uploadLines: []
      },
      {
        id: '5', name: 'Hot Cross Buns',
        description: 'Simple beginner melody. Keys: C4-E4.',
        data: { steps: [
          step(E4,c(E4),600), step(D4,c(D4),600), step(C4,c(C4),1200),
          step(E4,c(E4),600), step(D4,c(D4),600), step(C4,c(C4),1200),
          step(C4,c(C4),300), step(C4,c(C4),300), step(C4,c(C4),300), step(C4,c(C4),300),
          step(D4,c(D4),300), step(D4,c(D4),300), step(D4,c(D4),300), step(D4,c(D4),300),
          step(E4,c(E4),600), step(D4,c(D4),600), step(C4,c(C4),1200)
        ]}, uploadLines: []
      },
      {
        id: '6', name: 'When The Saints',
        description: 'When The Saints Go Marching In. Keys: C4-A4.',
        data: { steps: [
          step(C4,c(C4),400), step(E4,c(E4),400), step(F4,c(F4),400), step(G4,c(G4),1200),
          step(C4,c(C4),400), step(E4,c(E4),400), step(F4,c(F4),400), step(G4,c(G4),1200),
          step(C4,c(C4),400), step(E4,c(E4),400), step(F4,c(F4),400), step(G4,c(G4),800),
          step(E4,c(E4),800), step(C4,c(C4),800), step(E4,c(E4),800), step(D4,c(D4),1200),
          step(E4,c(E4),400), step(E4,c(E4),400), step(D4,c(D4),400), step(C4,c(C4),800),
          step(C4,c(C4),400), step(E4,c(E4),800), step(G4,c(G4),800),
          step(G4,c(G4),400), step(F4,c(F4),1200), step(E4,c(E4),400), step(F4,c(F4),400),
          step(G4,c(G4),800), step(E4,c(E4),800), step(D4,c(D4),800), step(C4,c(C4),1200)
        ]}, uploadLines: []
      },
      {
        id: '7', name: 'C Major Scale',
        description: 'C major scale, ascending and descending within one octave.',
        data: { steps: [
          step(C4,c(C4),500), step(D4,c(D4),500), step(E4,c(E4),500), step(F4,c(F4),500),
          step(G4,c(G4),500), step(A4,c(A4),500), step(B4,c(B4),500), step(C5,c(C5),800),
          step(B4,c(B4),500), step(A4,c(A4),500), step(G4,c(G4),500), step(F4,c(F4),500),
          step(E4,c(E4),500), step(D4,c(D4),500), step(C4,c(C4),800)
        ]}, uploadLines: []
      },

      // ═══════════ 2-OCTAVE MELODIES (keys 0-23) ═══════════
      {
        id: '8', name: 'Two Octave Scale',
        description: 'C major scale across 2 octaves, ascending and descending.',
        data: { steps: [
          step(C4,c(C4),400), step(D4,c(D4),400), step(E4,c(E4),400), step(F4,c(F4),400),
          step(G4,c(G4),400), step(A4,c(A4),400), step(B4,c(B4),400),
          step(C5,c(C5),400), step(D5,c(D5),400), step(E5,c(E5),400), step(F5,c(F5),400),
          step(G5,c(G5),400), step(A5,c(A5),400), step(B5,c(B5),600),
          step(B5,c(B5),400), step(A5,c(A5),400), step(G5,c(G5),400), step(F5,c(F5),400),
          step(E5,c(E5),400), step(D5,c(D5),400), step(C5,c(C5),400),
          step(B4,c(B4),400), step(A4,c(A4),400), step(G4,c(G4),400), step(F4,c(F4),400),
          step(E4,c(E4),400), step(D4,c(D4),400), step(C4,c(C4),600)
        ]}, uploadLines: []
      },
      {
        id: '9', name: 'Fur Elise',
        description: 'Beethoven - Fur Elise opening theme across two octaves.',
        data: { steps: [
          step(E5,c(E5),300), step(Eb5,c(Eb5),300), step(E5,c(E5),300), step(Eb5,c(Eb5),300),
          step(E5,c(E5),300), step(B4,c(B4),300), step(D5,c(D5),300), step(C5,c(C5),300),
          step(A4,c(A4),600),
          step(C4,c(C4),300), step(E4,c(E4),300), step(A4,c(A4),300), step(B4,c(B4),600),
          step(E4,c(E4),300), step(Gs4,c(Gs4),300), step(B4,c(B4),300), step(C5,c(C5),600),
          step(E4,c(E4),300),
          step(E5,c(E5),300), step(Eb5,c(Eb5),300), step(E5,c(E5),300), step(Eb5,c(Eb5),300),
          step(E5,c(E5),300), step(B4,c(B4),300), step(D5,c(D5),300), step(C5,c(C5),300),
          step(A4,c(A4),600),
          step(C4,c(C4),300), step(E4,c(E4),300), step(A4,c(A4),300), step(B4,c(B4),600),
          step(E4,c(E4),300), step(C5,c(C5),300), step(B4,c(B4),300), step(A4,c(A4),900)
        ]}, uploadLines: []
      },
      {
        id: '10', name: 'Canon in D',
        description: 'Pachelbel - Canon in D simplified melody across two octaves.',
        data: { steps: [
          step(Fs5,c(Fs5),500), step(E5,c(E5),500), step(D5,c(D5),500), step(Cs5,c(Cs5),500),
          step(B4,c(B4),500), step(A4,c(A4),500), step(B4,c(B4),500), step(Cs5,c(Cs5),500),
          step(D5,c(D5),500), step(Cs5,c(Cs5),500), step(B4,c(B4),500), step(A4,c(A4),500),
          step(G4,c(G4),500), step(Fs4,c(Fs4),500), step(G4,c(G4),500), step(A4,c(A4),500),
          step(D4,c(D4),500), step(Fs4,c(Fs4),500), step(A4,c(A4),500), step(G4,c(G4),500),
          step(Fs4,c(Fs4),500), step(D4,c(D4),500), step(Fs4,c(Fs4),500), step(E4,c(E4),500),
          step(D4,c(D4),500), step(B4,c(B4),500), step(A4,c(A4),500), step(G4,c(G4),500),
          step(A4,c(A4),500), step(Fs4,c(Fs4),500), step(D4,c(D4),500), step(D5,c(D5),1000)
        ]}, uploadLines: []
      },
      {
        id: '11', name: 'Greensleeves',
        description: 'Traditional English melody (What Child Is This). Two octaves.',
        data: { steps: [
          step(A4,c(A4),600),
          step(C5,c(C5),900), step(D5,c(D5),300), step(E5,c(E5),600),
          step(F5,c(F5),300), step(E5,c(E5),600), step(D5,c(D5),900),
          step(B4,c(B4),600), step(G4,c(G4),300), step(B4,c(B4),600),
          step(C5,c(C5),900), step(A4,c(A4),600),
          step(A4,c(A4),300), step(Gs4,c(Gs4),600), step(A4,c(A4),900),
          step(B4,c(B4),600), step(Gs4,c(Gs4),300), step(E4,c(E4),900),
          step(A4,c(A4),600),
          step(C5,c(C5),900), step(D5,c(D5),300), step(E5,c(E5),600),
          step(F5,c(F5),300), step(E5,c(E5),600), step(D5,c(D5),900),
          step(B4,c(B4),600), step(G4,c(G4),300), step(B4,c(B4),600),
          step(C5,c(C5),300), step(B4,c(B4),600),
          step(A4,c(A4),300), step(Gs4,c(Gs4),600), step(A4,c(A4),1200)
        ]}, uploadLines: []
      },
      {
        id: '12', name: 'Amazing Grace',
        description: 'Traditional hymn arranged across two octaves.',
        data: { steps: [
          step(G4,c(G4),600),
          step(C5,c(C5),900), step(E5,c(E5),300), step(C5,c(C5),600),
          step(E5,c(E5),900), step(D5,c(D5),600),
          step(C5,c(C5),900), step(A4,c(A4),600),
          step(G4,c(G4),1200), step(G4,c(G4),600),
          step(C5,c(C5),900), step(E5,c(E5),300), step(C5,c(C5),600),
          step(E5,c(E5),900), step(D5,c(D5),600),
          step(G5,c(G5),1800),
          step(E5,c(E5),600),
          step(G5,c(G5),300), step(E5,c(E5),600), step(G5,c(G5),300), step(E5,c(E5),600),
          step(C5,c(C5),900), step(A4,c(A4),600),
          step(G4,c(G4),1200), step(G4,c(G4),600),
          step(C5,c(C5),900), step(E5,c(E5),300), step(C5,c(C5),600),
          step(E5,c(E5),900), step(D5,c(D5),600),
          step(C5,c(C5),1800)
        ]}, uploadLines: []
      },
      {
        id: '13', name: 'Twinkle Twinkle (2 Oct)',
        description: 'Twinkle Twinkle Little Star arranged across two octaves with harmony.',
        data: { steps: [
          step(C4,c(C4),500), step(C4,c(C4),500), step(G4,c(G4),500), step(G4,c(G4),500),
          step(A4,c(A4),500), step(A4,c(A4),500), step(G4,c(G4),1000),
          step(F4,c(F4),500), step(F4,c(F4),500), step(E4,c(E4),500), step(E4,c(E4),500),
          step(D4,c(D4),500), step(D4,c(D4),500), step(C4,c(C4),1000),
          // Second verse up an octave
          step(C5,c(C5),500), step(C5,c(C5),500), step(G5,c(G5),500), step(G5,c(G5),500),
          step(A5,c(A5),500), step(A5,c(A5),500), step(G5,c(G5),1000),
          step(F5,c(F5),500), step(F5,c(F5),500), step(E5,c(E5),500), step(E5,c(E5),500),
          step(D5,c(D5),500), step(D5,c(D5),500), step(C5,c(C5),1000),
          // Back down to octave 1 for ending
          step(C4,c(C4),500), step(C4,c(C4),500), step(G4,c(G4),500), step(G4,c(G4),500),
          step(A4,c(A4),500), step(A4,c(A4),500), step(G4,c(G4),1000),
          step(F4,c(F4),500), step(F4,c(F4),500), step(E4,c(E4),500), step(E4,c(E4),500),
          step(D4,c(D4),500), step(D4,c(D4),500), step(C4,c(C4),1200)
        ]}, uploadLines: []
      },
      {
        id: '14', name: 'Ode to Joy (2 Oct)',
        description: 'Beethoven - Ode to Joy with two-octave run. Keys span C4-G5.',
        data: { steps: [
          step(E4,c(E4),500), step(E4,c(E4),500), step(F4,c(F4),500), step(G4,c(G4),500),
          step(G4,c(G4),500), step(F4,c(F4),500), step(E4,c(E4),500), step(D4,c(D4),500),
          step(C4,c(C4),500), step(C4,c(C4),500), step(D4,c(D4),500), step(E4,c(E4),500),
          step(E4,c(E4),700), step(D4,c(D4),300), step(D4,c(D4),1000),
          // Repeat up an octave
          step(E5,c(E5),500), step(E5,c(E5),500), step(F5,c(F5),500), step(G5,c(G5),500),
          step(G5,c(G5),500), step(F5,c(F5),500), step(E5,c(E5),500), step(D5,c(D5),500),
          step(C5,c(C5),500), step(C5,c(C5),500), step(D5,c(D5),500), step(E5,c(E5),500),
          step(D5,c(D5),700), step(C5,c(C5),300), step(C5,c(C5),1200)
        ]}, uploadLines: []
      },
      {
        id: '15', name: 'Octave Jump Drill',
        description: 'Alternating notes between octave 1 and octave 2 for practice.',
        data: { steps: [
          step(C4,c(C4),400), step(C5,c(C5),400), step(D4,c(D4),400), step(D5,c(D5),400),
          step(E4,c(E4),400), step(E5,c(E5),400), step(F4,c(F4),400), step(F5,c(F5),400),
          step(G4,c(G4),400), step(G5,c(G5),400), step(A4,c(A4),400), step(A5,c(A5),400),
          step(B4,c(B4),400), step(B5,c(B5),600),
          step(B5,c(B5),400), step(B4,c(B4),400), step(A5,c(A5),400), step(A4,c(A4),400),
          step(G5,c(G5),400), step(G4,c(G4),400), step(F5,c(F5),400), step(F4,c(F4),400),
          step(E5,c(E5),400), step(E4,c(E4),400), step(D5,c(D5),400), step(D4,c(D4),400),
          step(C5,c(C5),400), step(C4,c(C4),600)
        ]}, uploadLines: []
      },
      {
        id: '16', name: 'Cross Octave Chords',
        description: 'Chords spanning both octaves for multi-module practice.',
        data: { steps: [
          chord([C4,C5], [c(C4),c(C5)], 800),
          chord([E4,E5], [c(E4),c(E5)], 800),
          chord([G4,G5], [c(G4),c(G5)], 800),
          chord([C4,E4,G4],[c(C4),c(E4),c(G4)], 1000),
          chord([C5,E5,G5],[c(C5),c(E5),c(G5)], 1000),
          chord([C4,G4,C5,G5],[c(C4),c(G4),c(C5),c(G5)], 1200),
          chord([F4,A4], [c(F4),c(A4)], 800),
          chord([F5,A5], [c(F5),c(A5)], 800),
          chord([F4,A4,C5],[c(F4),c(A4),c(C5)], 1000),
          chord([C4,C5], [c(C4),c(C5)], 1200)
        ]}, uploadLines: []
      },
      {
        id: '17', name: 'Flower of Scotland',
        description: 'The national anthem of Scotland.',
        data: { steps: [
          step(A4,c(A4),400), step(A4,c(A4),400), step(G4,c(G4),400), step(F4,c(F4),800), step(C4,c(C4),800), step(C4,c(C4),400),
          step(F4,c(F4),600), step(A4,c(A4),400), step(G4,c(G4),1200), step(G4,c(G4),400), step(F4,c(F4),400), step(G4,c(G4),400),
          step(A4,c(A4),1600), step(A4,c(A4),400), step(Bb4,c(Bb4),600), step(A4,c(A4),400), step(Bb4,c(Bb4),400),
          step(C5,c(C5),800), step(F4,c(F4),1200), step(F4,c(F4),400), step(G4,c(G4),600), step(G4,c(G4),400), step(G4,c(G4),400), step(F4,c(F4),400), step(G4,c(G4),400),
          step(A4,c(A4),400), step(Bb4,c(Bb4),400), step(A4,c(A4),400), step(G4,c(G4),400), step(F4,c(F4),800), step(C4,c(C4),1200), step(A4,c(A4),400),
          step(Bb4,c(Bb4),600), step(A4,c(A4),400), step(Bb4,c(Bb4),400), step(C5,c(C5),800), step(F4,c(F4),1600), step(A4,c(A4),400),
          step(Bb4,c(Bb4),600), step(A4,c(A4),400), step(G4,c(G4),400), step(A4,c(A4),600), step(G4,c(G4),400), step(F4,c(F4),400), step(F4,c(F4),1200), step(F4,c(F4),400),
          step(Eb4,c(Eb4),400), step(G4,c(G4),400), step(F4,c(F4),1600)
        ]}, uploadLines: []
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
      const result = await uploadLinesToModule(moduleIp, lines);
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
      const moduleResults = await uploadLinesToAllModules(lines);
      markAllModulesSequence(seq);
      res.json({ ok: true, uploaded: id, broadcast: true, modules: moduleResults });
      return;
    }

    // Default: broadcast to all connected modules (serial modules are now in the Map too)
    const connectedModules = [...modules.values()].filter(e => e.connected && e.ws);
    if (connectedModules.length > 0) {
      const moduleResults = await uploadLinesToAllModules(lines);
      markAllModulesSequence(seq);
      res.json({ ok: true, uploaded: id, sentCount: lines.length, modules: moduleResults });
      return;
    }

    res.status(400).json({ ok: false, error: 'No module connected' });
  } catch (e) {
    res.status(500).json({ ok: false, error: e.message });
  }
});

let httpServer = null;
let wss = null;
let staleCleanupTimer = null;

async function startServers() {
    if (httpServer && wss) return;

    httpServer = app.listen(PORT, '0.0.0.0', async () => {
        console.log(`\nOPEN OCTAVE CONTROLLER`);
        console.log(`HTTP server running at: http://localhost:${PORT}`);
        console.log(`WebSocket server listening on port ${WS_PORT}`);

        // Start auto-discovery for USB serial modules
        console.log(`[SERIAL] Starting USB serial auto-discovery...`);
        startSerialAutoDiscovery();
        console.log();
    });

    wss = new WebSocket.Server({ host: '0.0.0.0', port: WS_PORT }, () => {
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

    staleCleanupTimer = setInterval(() => {
        cleanupStaleModules();

        for (const entry of modules.values()) {
            if (!entry.connected || !entry.ws || entry.transport === 'serial') continue;
            if (entry.ws.readyState !== WebSocket.OPEN) continue;
            try {
                entry.ws.ping();
            } catch (_) {}
        }
    }, MODULE_CLEANUP_INTERVAL_MS);

    wss.on('close', () => {
        if (staleCleanupTimer) clearInterval(staleCleanupTimer);
        if (serialDiscoveryTimer) {
            clearInterval(serialDiscoveryTimer);
            serialDiscoveryTimer = null;
        }
    });
}

function stopServers() {
    if (staleCleanupTimer) {
        clearInterval(staleCleanupTimer);
        staleCleanupTimer = null;
    }
    if (serialDiscoveryTimer) {
        clearInterval(serialDiscoveryTimer);
        serialDiscoveryTimer = null;
    }

    for (const entry of modules.values()) {
        if (entry.ws) {
            try {
                if (entry.transport !== 'serial') entry.ws.terminate();
            } catch (_) {}
        }
        entry.ws = null;
        entry.connected = false;
    }

    if (wss) {
        try { wss.close(); } catch (_) {}
        wss = null;
    }

    if (httpServer) {
        try { httpServer.close(); } catch (_) {}
        httpServer = null;
    }
}

if (require.main === module) {
    startServers();
}

module.exports = {
    app,
    startServers,
    stopServers
};
