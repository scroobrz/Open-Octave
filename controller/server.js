require('dotenv').config();
const express = require('express');
const cors = require('cors');
const fetch = require('node-fetch');
const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');

const app = express();
const PORT = process.env.APP_PORT || 3000;

const COMM_MODE = process.env.COMM_MODE || 'WIFI';
const ESP_HOST = process.env.ESP32_HOST;
const SERIAL_PATH = process.env.SERIAL_PORT || '/dev/tty.usbmodem1101';
const BAUD_RATE = 115200;

app.use(cors());
app.use(express.json());

let port;
let parser;

if (COMM_MODE === 'SERIAL') {
    console.log(`[INIT] Starting in SERIAL mode on ${SERIAL_PATH}...`);
    try {
        port = new SerialPort({ path: SERIAL_PATH, baudRate: BAUD_RATE });
        parser = port.pipe(new ReadlineParser({ delimiter: '\n' }));
        
        port.on('open', () => console.log('[SERIAL] Port Open'));
        port.on('error', (err) => console.error('[SERIAL] Error: ', err.message));
        
        // Simple listener for debug
        parser.on('data', (data) => {
            console.log(`[ESP32] ${data.toString().trim()}`);
        });
        
    } catch (err) {
        console.error(`[FATAL] Could not open serial port: ${err.message}`);
    }
} else {
    console.log(`[INIT] Starting in WIFI mode targeting ${ESP_HOST}...`);
}

function translateToSerialCmd(endpoint, query) {
    if (endpoint === '/api/modes') {
        if (query.mode === 'manual') return 'm';
        if (query.mode === 'auto_leds') return 'a';
        if (query.mode === 'full_auto') return 'f';
    }
    
    if (endpoint === '/api/seq/control') {
        if (query.cmd === 'start') return 's';
        if (query.cmd === 'stop') return 'x';
        if (query.cmd === 'next') return 'n';
        if (query.cmd === 'prev') return 'p';
    }

    if (endpoint === '/api/seq/select') {
        if (query.id !== undefined) return query.id.toString(); 
    }

    if (endpoint === '/api/test') {
        if (query.target === 'leds') return 't';
        if (query.target === 'servos') return 'u';
    }
    return null;
}

async function sendCommand(endpoint, method, query = {}) {
    
    if (COMM_MODE === 'WIFI') {
        return await sendWifiCommand(endpoint, method, query);
    }

    if (COMM_MODE === 'SERIAL') {
        return await sendSerialCommand(endpoint, query);
    }
}

async function sendWifiCommand(endpoint, method, query) {
    if (!ESP_HOST) {
        console.error("ESP32_HOST is missing!");
        return { error: "Configuration Error" };
    }

    const urlParams = new URLSearchParams(query);
    const url = `${ESP_HOST}${endpoint}?${urlParams.toString()}`;
    
    console.log(`[WIFI] ${method} ${url}`);
    
    try {
        const response = await fetch(url, {
            method: method,
            headers: { 'Content-Type': 'application/json' },
            timeout: 5000
        });
        
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        return await response.json();
    } catch (e) {
        console.error(`[WIFI ERROR] ${e.message}`);
        throw e;
    }
}

async function sendSerialCommand(endpoint, query) {
    if (!port || !port.isOpen) {
            console.error("Serial Port Closed");
            return { error: "Serial Port Closed" };
    }
    
    const cmd = translateToSerialCmd(endpoint, query);
    
    if (!cmd) {
        console.warn(`[SERIAL] No mapping for ${endpoint} ${JSON.stringify(query)}`);
        return { error: "Command not supported in Serial Mode" };
    }

    console.log(`[SERIAL] Sending: '${cmd}'`);
    port.write(cmd); 
    
    return { success: true, mode: "serial", cmd: cmd };
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

app.post('/api/test', async (req, res) => {
    try {
        const result = await sendCommand('/api/test', 'POST', req.query);
        res.json(result);
    } catch (e) { res.status(500).json({ error: e.message }); }
});

app.get('/api/status', async (req, res) => {
    if (COMM_MODE === 'SERIAL') {
        res.json({ online: true, mode: 'serial_assumed' });
    } else {
        try {
            const result = await sendCommand('/api/status', 'GET');
            res.json(result);
        } catch (e) { res.status(503).json({ online: false }); }
    }
});


app.listen(PORT, () => {
    console.log(`\nOPEN OCTAVE CONTROLLER`);
    console.log(`Server running at: http://localhost:${PORT}`);
    console.log(`Mode: ${COMM_MODE}\n`);
});
