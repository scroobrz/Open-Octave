const WebSocket = require('ws');

// laptop hosting
// Fake ESP32 module for testing the Open Octave backend without hardware.
// Usage examples:
//   node mock_esp32_ws.js
//   MODULES=2 node mock_esp32_ws.js
//   HOST=192.168.1.10 WS_PORT=8081 MODULES=1 node mock_esp32_ws.js

const HOST = process.env.HOST || '127.0.0.1';
const WS_PORT = Number(process.env.WS_PORT || 8081);
const MODULES = Number(process.env.MODULES || 1);

const url = `ws://${HOST}:${WS_PORT}`;

console.log(`[MOCK ESP32] Connecting to ${url} ...`);

const ws = new WebSocket(url);

ws.on('open', () => {
  console.log(`[MOCK ESP32] Connected`);
  const hello = `HELLO modules=${MODULES}`;
  console.log(`[MOCK ESP32] -> ${hello}`);
  ws.send(hello);

  const status = `STATUS mode=GUIDED running=0 seq=-1 step=-1`;
  console.log(`[MOCK ESP32] -> ${status}`);
  ws.send(status);
});

ws.on('message', (data) => {
  const msg = data.toString().trim();
  console.log(`[MOCK ESP32] <- ${msg}`);
});

ws.on('close', () => {
  console.log(`[MOCK ESP32] Disconnected`);
});

ws.on('error', (err) => {
  console.error(`[MOCK ESP32] Error: ${err.message}`);
});