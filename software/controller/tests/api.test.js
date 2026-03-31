// CI-tolerant serial state checks
const fs = require('fs');
const os = require('os');
const path = require('path');
const request = require('supertest');

vi.mock('serialport', () => {
  class FakeSerialPort {
    constructor() {
      this.isOpen = false;
      this.path = 'TEST_PORT';
      this.handlers = {};
    }

    pipe() {
      return {
        on() {}
      };
    }

    on(event, handler) {
      this.handlers[event] = handler;
    }

    write() {}

    close(cb) {
      this.isOpen = false;
      if (cb) cb(null);
    }
  }

  return {
    SerialPort: FakeSerialPort
  };
});

vi.mock('@serialport/parser-readline', () => {
  class FakeReadlineParser {
    on() {}
  }

  return {
    ReadlineParser: FakeReadlineParser
  };
});

vi.mock('ws', () => {
  class FakeWebSocketServer {
    constructor(options, callback) {
      this.options = options;
      this.handlers = {};
      if (typeof callback === 'function') {
        callback();
      }
    }

    on(event, handler) {
      this.handlers[event] = handler;
    }

    close() {}
  }

  return {
    OPEN: 1,
    Server: FakeWebSocketServer
  };
});

let app;
let sqlite;
let serverModule;
let tempDir;
let dbPath;

beforeAll(() => {
  // isolated db for api suite
  tempDir = fs.mkdtempSync(path.join(os.tmpdir(), 'open-octave-api-'));
  dbPath = path.join(tempDir, 'api-test.sqlite3');

  process.env.SQLITE_PATH = dbPath;
  process.env.APP_PORT = '3999';
  process.env.WS_PORT = '8099';

  sqlite = require('../database/sqlite');
  serverModule = require('../server');
  app = serverModule.app;
});

beforeEach(() => {
  const items = sqlite.listSequences();
  for (const item of items) {
    sqlite.deleteSequence(item.id);
  }
});

afterAll(() => {
  if (serverModule && serverModule.stopServers) {
    serverModule.stopServers();
  }

  if (fs.existsSync(dbPath)) fs.unlinkSync(dbPath);
  if (fs.existsSync(tempDir)) fs.rmSync(tempDir, { recursive: true, force: true });

  delete process.env.SQLITE_PATH;
  delete process.env.APP_PORT;
  delete process.env.WS_PORT;
});

describe('controller api smoke tests', () => {
  it('GET /api/health returns controller health info', async () => {
    const res = await request(app).get('/api/health');

    expect(res.status).toBe(200);
    expect(res.body.ok).toBe(true);
    expect(res.body.wsServerPort).toBe(8099);
    expect(Number(res.body.appPort)).toBe(3999);
    expect(res.body.connectedModules).toBe(0);
    expect(Array.isArray(res.body.modules)).toBe(true);
    expect(res.body.modules).toHaveLength(0);
    expect(res.body.serial).toBeTruthy();
    expect(res.body.serial.open).toBe(false);
    expect([null, '/dev/cu.usbserial-XXXX']).toContain(res.body.serial.port);
  });

  it('GET /api/colors returns shared colour config', async () => {
    const res = await request(app).get('/api/colors');

    expect(res.status).toBe(200);
    expect(res.body.ok).toBe(true);
    expect(res.body.fingerColors).toBeTruthy();
    expect(typeof res.body.fingerColors).toBe('object');
    expect(res.body.fallbackColor).toBeTruthy();
  });

  it('GET /api/state returns controller state', async () => {
    const res = await request(app).get('/api/state');

    expect(res.status).toBe(200);
    expect(res.body.ok).toBe(true);
    expect(res.body.state).toBeTruthy();
    expect(res.body.state.wsServerPort).toBe(8099);
    expect(res.body.state.connectedModules).toBe(0);
    expect(Array.isArray(res.body.state.modules)).toBe(true);
    expect(res.body.state.serial).toBeTruthy();
    expect(res.body.state.serial.open).toBe(false);
    expect([null, '/dev/cu.usbserial-XXXX']).toContain(res.body.state.serial.port);
  });

  it('GET /api/logs returns bounded log output', async () => {
    const res = await request(app).get('/api/logs?tail=10');

    expect(res.status).toBe(200);
    expect(res.body.ok).toBe(true);
    expect(res.body.max).toBe(500);
    expect(Array.isArray(res.body.items)).toBe(true);
    expect(res.body.returned).toBeLessThanOrEqual(10);
  });

  it('GET /api/modules returns an empty module list initially', async () => {
    const res = await request(app).get('/api/modules');

    expect(res.status).toBe(200);
    expect(res.body.ok).toBe(true);
    expect(Array.isArray(res.body.modules)).toBe(true);
    expect(res.body.modules).toHaveLength(0);
  });

  it('GET /api/db/sequences returns an empty list initially', async () => {
    const res = await request(app).get('/api/db/sequences');

    expect(res.status).toBe(200);
    expect(res.body.ok).toBe(true);
    expect(Array.isArray(res.body.items)).toBe(true);
    expect(res.body.items).toHaveLength(0);
    expect(typeof res.body.dbPath).toBe('string');
  });

  it('POST /api/db/sequences rejects missing name', async () => {
    const res = await request(app)
      .post('/api/db/sequences')
      .send({
        steps: []
      });

    expect(res.status).toBe(400);
    expect(res.body.ok).toBe(false);
    expect(res.body.error).toBe('Missing name');
  });

  it('POST /api/db/sequences rejects non-array steps', async () => {
    const res = await request(app)
      .post('/api/db/sequences')
      .send({
        name: 'Bad Payload',
        steps: 'not-an-array'
      });

    expect(res.status).toBe(400);
    expect(res.body.ok).toBe(false);
    expect(res.body.error).toBe('steps must be an array');
  });

  it('POST /api/db/sequences saves a valid sequence', async () => {
    const res = await request(app)
      .post('/api/db/sequences')
      .send({
        name: 'API Saved Sequence',
        description: 'created by api test',
        steps: [
          { keys: [0], colors: ['00B4D8'], duration: 300 },
          { keys: [4], colors: ['FFD700'], duration: 400 }
        ]
      });

    expect(res.status).toBe(200);
    expect(res.body.ok).toBe(true);
    expect(res.body.message).toBe('Saved sequence');
    expect(res.body.item).toBeTruthy();
    expect(res.body.item.name).toBe('API Saved Sequence');
    expect(res.body.item.description).toBe('created by api test');
    expect(res.body.item.maxKey).toBe(4);
    expect(res.body.item.data.steps).toHaveLength(2);
  });

  it('GET /api/db/sequences/:id returns 404 for a missing sequence', async () => {
    const res = await request(app).get('/api/db/sequences/99999');

    expect(res.status).toBe(404);
    expect(res.body.ok).toBe(false);
    expect(res.body.error).toBe('Sequence not found');
  });

  it('GET /api/db/sequences/:id returns a saved sequence', async () => {
    const savedId = sqlite.upsertSequence({
      name: 'Lookup Sequence',
      description: 'for get by id',
      data: {
        steps: [
          { keys: [2], colors: ['4ECB71'], duration: 250 }
        ]
      },
      uploadLines: []
    });

    const res = await request(app).get(`/api/db/sequences/${savedId}`);

    expect(res.status).toBe(200);
    expect(res.body.ok).toBe(true);
    expect(res.body.item).toBeTruthy();
    expect(res.body.item.id).toBe(Number(savedId));
    expect(res.body.item.name).toBe('Lookup Sequence');
    expect(res.body.item.maxKey).toBe(2);
  });

  it('DELETE /api/db/sequences/:id returns 404 for a missing sequence', async () => {
    const res = await request(app).delete('/api/db/sequences/99999');

    expect(res.status).toBe(404);
    expect(res.body.ok).toBe(false);
    expect(res.body.error).toBe('Sequence not found');
  });

  it('DELETE /api/db/sequences/:id deletes an existing sequence', async () => {
    const savedId = sqlite.upsertSequence({
      name: 'Delete Through API',
      description: '',
      data: {
        steps: [
          { keys: [1], colors: ['00B4D8'], duration: 200 }
        ]
      },
      uploadLines: []
    });

    const res = await request(app).delete(`/api/db/sequences/${savedId}`);

    expect(res.status).toBe(200);
    expect(res.body.ok).toBe(true);
    expect(res.body.message).toBe('Deleted sequence');
    expect(res.body.id).toBe(Number(savedId));
    expect(sqlite.getSequence(savedId)).toBeNull();
  });

  it('POST /api/db/sequences/seed creates preset sequences', async () => {
    const res = await request(app).post('/api/db/sequences/seed');

    expect(res.status).toBe(200);
    expect(res.body.ok).toBe(true);
    expect(res.body.message).toBe('Seeded preset sequences');
    expect(Array.isArray(res.body.items)).toBe(true);
    expect(res.body.items.length).toBeGreaterThan(0);
  });

  it('POST /api/modules/all/control rejects an invalid command', async () => {
    const res = await request(app)
      .post('/api/modules/all/control')
      .send({ cmd: 'bad_command' });

    expect(res.status).toBe(400);
    expect(res.body.ok).toBe(false);
    expect(res.body.error).toBe('cmd must be start, stop, led_test, or servo_test');
  });

  it('POST /api/modules/:ip/control rejects an invalid command', async () => {
    const res = await request(app)
      .post('/api/modules/192.168.1.77/control')
      .send({ cmd: 'bad_command' });

    expect(res.status).toBe(400);
    expect(res.body.ok).toBe(false);
    expect(res.body.error).toBe('cmd must be start, stop, led_test, or servo_test');
  });

  it('POST /api/modules/:ip/control returns module not connected for a valid command', async () => {
    const res = await request(app)
      .post('/api/modules/192.168.1.77/control')
      .send({ cmd: 'stop' });

    expect(res.status).toBe(200);
    expect(res.body.ok).toBe(false);
    expect(res.body.error).toMatch(/not connected/i);
  });

  it('POST /api/modules/:ip/upload returns 404 when module is not connected', async () => {
    const savedId = sqlite.upsertSequence({
      name: 'Upload Target',
      description: '',
      data: {
        steps: [
          { keys: [0], colors: ['00B4D8'], duration: 300 }
        ]
      },
      uploadLines: []
    });

    const res = await request(app)
      .post('/api/modules/192.168.1.50/upload')
      .send({ sequenceId: savedId });

    expect(res.status).toBe(404);
    expect(res.body.ok).toBe(false);
    expect(res.body.error).toMatch(/not connected/i);
  });

  it('POST /api/modules/all/upload rejects missing sequenceId', async () => {
    const res = await request(app)
      .post('/api/modules/all/upload')
      .send({});

    expect(res.status).toBe(400);
    expect(res.body.ok).toBe(false);
    expect(res.body.error).toBe('Missing sequenceId in body');
  });

  it('POST /api/midi/import rejects when no file is uploaded', async () => {
    const res = await request(app).post('/api/midi/import');

    expect(res.status).toBe(400);
    expect(res.body.ok).toBe(false);
    expect(res.body.error).toBe('No MIDI file uploaded.');
  });

  it('POST /api/midi/import rejects a non-midi file extension', async () => {
    const res = await request(app)
      .post('/api/midi/import')
      .attach('file', Buffer.from('not a midi'), 'notes.txt');

    expect(res.status).toBe(400);
    expect(res.body.ok).toBe(false);
    expect(res.body.error).toBe('Only .mid or .midi files are supported.');
  });

  it('POST /api/db/sequences/:id/upload returns 400 when no transport is available', async () => {
    const savedId = sqlite.upsertSequence({
      name: 'DB Upload Sequence',
      description: '',
      data: {
        steps: [
          { keys: [0], colors: ['00B4D8'], duration: 300 }
        ]
      },
      uploadLines: []
    });

    const res = await request(app).post(`/api/db/sequences/${savedId}/upload`);

    expect(res.status).toBe(400);
    expect(res.body.ok).toBe(false);
    expect(res.body.error).toBe('No module connected and no serial port open');
  });
});