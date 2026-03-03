// Mock ESP32 WebSocket server for testing controller/server.js without hardware.
// It prints received commands and optionally sends back simple replies.

const WebSocket = require('ws');

const PORT = process.env.MOCK_WS_PORT ? Number(process.env.MOCK_WS_PORT) : 8081;

const wss = new WebSocket.Server({ port: PORT }, () => {
  console.log(`[MOCK-ESP32] WebSocket server listening on ws://127.0.0.1:${PORT}`);
});

wss.on('connection', (socket, req) => {
  console.log(`[MOCK-ESP32] Client connected from ${req.socket.remoteAddress}`);

  // Optional: greet the client (helps confirm connection visually)
  socket.send('[MOCK-ESP32] hello');

  socket.on('message', (data) => {
    const msg = data.toString();
    console.log(`[MOCK-ESP32] Received: '${msg}'`);

    // Optional: send back something that looks like firmware logs
    // so you can confirm the controller receives messages.
    socket.send(`[MOCK-ESP32] ACK cmd=${msg}`);
  });

  socket.on('close', () => {
    console.log('[MOCK-ESP32] Client disconnected');
  });

  socket.on('error', (err) => {
    console.error('[MOCK-ESP32] Socket error:', err.message);
  });
});