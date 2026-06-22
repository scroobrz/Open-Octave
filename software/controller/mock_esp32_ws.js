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

let octaveOffset = 0;
let sequenceRunning = false;
let currentMode = 'GUIDED';
let currentSeq = -1;
let currentStep = -1;

function sendStatus() {
  const status = `STATUS mode=${sequenceRunning ? currentMode : 'N/A'} running=${sequenceRunning ? 1 : 0} seq=${currentSeq} step=${currentStep} octaveOffset=${octaveOffset}`;
  console.log(`[MOCK ESP32] -> ${status}`);
  ws.send(status);
}

console.log(`[MOCK ESP32] Connecting to ${url} ...`);

const ws = new WebSocket(url);

ws.on('open', () => {
  console.log(`[MOCK ESP32] Connected`);

  // octave offset
  // Reset mock state on every fresh connection so the UI always starts from default.
  octaveOffset = 0;
  sequenceRunning = false;
  currentMode = 'GUIDED';
  currentSeq = -1;
  currentStep = -1;

  const hello = `HELLO modules=${MODULES}`;
  console.log(`[MOCK ESP32] -> ${hello}`);
  ws.send(hello);

  sendStatus();
});

ws.on('message', (data) => {
  const msg = data.toString().trim();
  console.log(`[MOCK ESP32] <- ${msg}`);

  if (msg === 'g') {
    sequenceRunning = true;
    currentMode = 'GUIDED';
    currentStep = 0;
    ws.send('ACK cmd=g ok=1');
    sendStatus();
    return;
  }

  if (msg === 't') {
    sequenceRunning = true;
    currentMode = 'TEACHING';
    currentStep = 0;
    ws.send('ACK cmd=t ok=1');
    sendStatus();
    return;
  }

  if (msg === 'x') {
    sequenceRunning = false;
    currentStep = -1;
    ws.send('ACK cmd=x ok=1');
    sendStatus();
    return;
  }

  // octave offset
  if (msg.startsWith('O ')) {
    const match = msg.match(/\bv=(\d+)\b/);
    if (!match) {
      ws.send('ERR octave_offset=invalid');
      sendStatus();
      return;
    }

    const nextOffset = Number(match[1]);
    if (!Number.isInteger(nextOffset) || nextOffset < 0 || nextOffset > 2) {
      ws.send('ERR octave_offset=invalid range=0-2');
      sendStatus();
      return;
    }

    octaveOffset = nextOffset;
    ws.send(`ACK octave_offset=${octaveOffset} ok=1`);
    sendStatus();
    return;
  }

  if (msg.startsWith('U ')) {
    const match = msg.match(/\bi=(\d+)\b/);
    currentSeq = match ? Number(match[1]) : currentSeq;
    ws.send(`ACK upload=begin i=${currentSeq} ok=1`);
    return;
  }

  if (msg.startsWith('E ')) {
    ws.send(`ACK upload=end i=${currentSeq} ok=1`);
    sendStatus();
    return;
  }
});

ws.on('close', () => {
  console.log(`[MOCK ESP32] Disconnected`);
});

ws.on('error', (err) => {
  console.error(`[MOCK ESP32] Error: ${err.message}`);
});