require('dotenv').config();
const express = require('express');
const cors = require('cors');
const fetch = require('node-fetch');

const app = express();
const PORT = process.env.APP_PORT || 3000;
const ESP_HOST = process.env.ESP32_HOST;

app.use(cors());
app.use(express.json());

// Helper for forwarding requests to ESP32
async function forwardToEsp(endpoint, method = 'GET', queryParams = {}) {
    try {
        // Construct query string
        const urlParams = new URLSearchParams(queryParams);
        const url = `${ESP_HOST}${endpoint}?${urlParams.toString()}`;
        
        console.log(`[PROXY] ${method} ${url}`);
        
        const options = {
            method: method,
            headers: { 'Content-Type': 'application/json' },
            timeout: 5000 // 5s timeout
        };

        const response = await fetch(url, options);
        
        if (!response.ok) {
            const errorText = await response.text();
            throw new Error(`ESP32 Error ${response.status}: ${errorText}`);
        }
        
        return await response.json();
    } catch (error) {
        console.error(`[ERROR] Failed to contact ESP32: ${error.message}`);
        throw error;
    }
}

// ============ API ROUTES ============

// 1. MODE CONTROL
// POST /api/mode?type=manual
app.post('/api/modes', async (req, res) => {
    try {
        const mode = req.query.mode;
        if (!mode) return res.status(400).json({ error: "Missing 'mode' query parameter" });
        
        const data = await forwardToEsp('/api/modes', 'POST', { mode: mode });
        res.json(data);
    } catch (error) {
        res.status(502).json({ error: "Device Unreachable", details: error.message });
    }
});

// 2. SEQUENCE TRANSPORT
// POST /api/seq/control?cmd=start
app.post('/api/seq/control', async (req, res) => {
    try {
        const cmd = req.query.cmd;
        if (!cmd) return res.status(400).json({ error: "Missing 'cmd' query parameter" });
        
        const data = await forwardToEsp('/api/seq/control', 'POST', { cmd });
        res.json(data);
    } catch (error) {
        res.status(502).json({ error: "Device Unreachable", details: error.message });
    }
});

// 3. SEQUENCE SELECTION
// POST /api/seq/select?id=2
app.post('/api/seq/select', async (req, res) => {
    try {
        const id = req.query.id;
        if (id === undefined) return res.status(400).json({ error: "Missing 'id' query parameter" });
        
        const data = await forwardToEsp('/api/seq/select', 'POST', { id });
        res.json(data);
    } catch (error) {
        res.status(502).json({ error: "Device Unreachable", details: error.message });
    }
});

// 4. LIST SEQUENCES
// GET /api/seq/list
app.get('/api/seq/list', async (req, res) => {
    try {
        const data = await forwardToEsp('/api/seq/list', 'GET');
        res.json(data);
    } catch (error) {
        res.status(502).json({ error: "Device Unreachable", details: error.message });
    }
});

// 5. STATUS
// GET /api/status
app.get('/api/status', async (req, res) => {
    try {
        const data = await forwardToEsp('/api/status', 'GET');
        res.json(data);
    } catch (error) {
        // Return structured error so frontend knows device is offline
        res.status(503).json({ 
            online: false, 
            mode: "offline", 
            error: "Device Unreachable" 
        });
    }
});

// 6. TESTING
// POST /api/test?target=leds
app.post('/api/test', async (req, res) => {
    try {
        const target = req.query.target;
        if (!target) return res.status(400).json({ error: "Missing 'target' query parameter" });
        
        const data = await forwardToEsp('/api/test', 'POST', { target });
        res.json(data);
    } catch (error) {
        res.status(502).json({ error: "Device Unreachable", details: error.message });
    }
});

app.listen(PORT, () => {
    console.log(`\nOPEN OCTAVE CONTROLLER`);
    console.log(`Server running at: http://localhost:${PORT}`);
    console.log(`Proxying to ESP32: ${ESP_HOST}\n`);
});
