// Mock ESP32 WebSocket CLIENT for testing controller's WS server.
// Connects to the controller as a WebSocket client (like a real ESP32 would).
//
// Usage:
//   node mock_esp32_ws.js                    # default: connect to ws://127.0.0.1:81, chainLength=1
//   MOCK_CHAIN=2 node mock_esp32_ws.js       # simulate a 2-module chain
//   MOCK_WS_URL=ws://192.168.4.2:81 node mock_esp32_ws.js  # custom controller URL

const WebSocket = require('ws');

const WS_URL = process.env.MOCK_WS_URL || 'ws://127.0.0.1:81';
const CHAIN_LENGTH = process.env.MOCK_CHAIN ? Number(process.env.MOCK_CHAIN) : 1;

let reconnectDelay = 1000;
const MAX_RECONNECT_DELAY = 10000;

function connect() {
  console.log(`[MOCK-ESP32] Connecting to ${WS_URL} (chain=${CHAIN_LENGTH})...`);

  const ws = new WebSocket(WS_URL);

  ws.on('open', () => {
    console.log(`[MOCK-ESP32] Connected to controller`);
    reconnectDelay = 1000;

    // Send HELLO message (registration protocol)
    const hello = `HELLO modules=${CHAIN_LENGTH}`;
    console.log(`[MOCK-ESP32] Sending: ${hello}`);
    ws.send(hello);
  });

  ws.on('message', (data) => {
    const msg = data.toString().trim();
    console.log(`[MOCK-ESP32] Received: '${msg}'`);

    // Respond to single-char commands with mock ACK
    if (msg.length === 1) {
      const ack = `ACK cmd=${msg}`;
      console.log(`[MOCK-ESP32] Sending: ${ack}`);
      ws.send(ack);

      // If start command, also send STATUS
      if (msg === 'g' || msg === 't') {
        const mode = msg === 'g' ? 'GUIDED' : 'TEACHING';
        setTimeout(() => {
          const status = `STATUS running=1 seq=1 step=0 mode=${mode}`;
          console.log(`[MOCK-ESP32] Sending: ${status}`);
          ws.send(status);
        }, 200);
      }

      if (msg === 'x') {
        setTimeout(() => {
          const status = `STATUS running=0 seq=1 step=-1 mode=N/A`;
          console.log(`[MOCK-ESP32] Sending: ${status}`);
          ws.send(status);
        }, 200);
      }
      return;
    }

    // Handle upload protocol
    if (msg.startsWith('U ')) {
      const ack = `ACK upload=start`;
      console.log(`[MOCK-ESP32] Sending: ${ack}`);
      ws.send(ack);
      return;
    }

    if (msg.startsWith('S ')) {
      // Step received — silent ACK (firmware doesn't ACK individual steps)
      return;
    }

    if (msg.startsWith('E ')) {
      const ack = `ACK upload=complete`;
      console.log(`[MOCK-ESP32] Sending: ${ack}`);
      ws.send(ack);
      return;
    }

    // Generic ACK for anything else
    ws.send(`ACK cmd=${msg.substring(0, 20)}`);
  });

  ws.on('close', () => {
    console.log(`[MOCK-ESP32] Disconnected. Reconnecting in ${reconnectDelay / 1000}s...`);
    setTimeout(connect, reconnectDelay);
    reconnectDelay = Math.min(reconnectDelay * 2, MAX_RECONNECT_DELAY);
  });

  ws.on('error', (err) => {
    if (err.code !== 'ECONNREFUSED') {
      console.error(`[MOCK-ESP32] Error: ${err.message}`);
    }
  });
}

connect();
