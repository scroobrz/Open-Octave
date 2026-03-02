// ==============================
// Open Octave - SQLite Layer
// ==============================
// Firmware v4 stores only:
//   - 1 default sequence
//   - 1 uploaded sequence
// Software stores many sequences in SQLite and uploads one at a time.
//
// This module isolates all DB logic so server.js remains readable.

const path = require('path');
const Database = require('better-sqlite3');

// Firmware v4 hard-limits uploads to MAX_SEQUENCE_LENGTH (see firmware_V4_config.h).
// Keep this mirrored here so we fail fast before sending an upload that firmware will reject.
const FIRMWARE_V4_MAX_SEQUENCE_LENGTH = 16;

// Firmware v4 parses `i=` (sequence id) using atoi(), so it must be numeric.
// If we later change firmware to accept string IDs, we can relax this constraint.
const FIRMWARE_V4_SEQUENCE_ID_REGEX = /^\d+$/;

// Allow override via env
const DB_PATH =
  process.env.SQLITE_PATH ||
  path.join(__dirname, 'open-octave.sqlite3');

const db = new Database(DB_PATH);
db.pragma('journal_mode = WAL'); // use write-ahead logging mode

// ------------------------------
// Schema
// ------------------------------
// create single table called sequences
db.exec(`
  CREATE TABLE IF NOT EXISTS sequences (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    description TEXT,
    data_json TEXT NOT NULL,
    upload_lines_json TEXT NOT NULL,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
  );
`);

function nowIso() {
  return new Date().toISOString();
}

function safeJsonParse(s, fallback) {
  try {
    return JSON.parse(s);
  } catch {
    return fallback;
  }
}

// ------------------------------
// CRUD
// ------------------------------
// queries all sequences ordered by most recently updated
function listSequences() {
  const rows = db
    .prepare(`
      SELECT id, name, description, data_json, updated_at
      FROM sequences
      ORDER BY updated_at DESC
    `)
    .all();

  // parses data_json
  return rows.map((r) => {
    const data = safeJsonParse(r.data_json, null);
    const stepCount = Array.isArray(data?.steps)
      ? data.steps.length
      : null;

      // computes stepCount from data.steps.length if possible
    return {
      id: r.id,
      name: r.name,
      description: r.description || '',
      stepCount,
      updatedAt: r.updated_at
    };
  });
}

function getSequence(id) {
  const row = db
    .prepare(`
      SELECT *
      FROM sequences
      WHERE id = ?
    `)
    .get(id);

  if (!row) return null;

  return {
    id: row.id,
    name: row.name,
    description: row.description || '',
    data: safeJsonParse(row.data_json, {}),
    uploadLines: safeJsonParse(row.upload_lines_json, []),
    createdAt: row.created_at,
    updatedAt: row.updated_at
  };
}

function upsertSequence(seq) {
  const ts = nowIso();

  const existing = db
    .prepare(`SELECT created_at FROM sequences WHERE id = ?`)
    .get(seq.id);

  const createdAt = existing ? existing.created_at : ts;

  db.prepare(`
    INSERT INTO sequences
    (id, name, description, data_json, upload_lines_json, created_at, updated_at)
    VALUES (@id, @name, @description, @data_json, @upload_lines_json, @created_at, @updated_at)
    ON CONFLICT(id) DO UPDATE SET
      name = excluded.name,
      description = excluded.description,
      data_json = excluded.data_json,
      upload_lines_json = excluded.upload_lines_json,
      updated_at = excluded.updated_at
  `).run({
    id: seq.id,
    name: seq.name,
    description: seq.description || '',
    data_json: JSON.stringify(seq.data || {}),
    upload_lines_json: JSON.stringify(seq.uploadLines || []),
    created_at: createdAt,
    updated_at: ts
  });
}

// ------------------------------
// Upload Line Generator
// ------------------------------
// Firmware parses name by reading until the next space, so the name must not contain spaces.
// We also keep the charset conservative to avoid surprises on the ESP32.
// Policy: spaces -> '-', collapse repeats, trim, and clamp length.
function sanitizeNameForFirmware(name) {
  const raw = String(name || '').trim();

  // Replace whitespace with '-', then replace any other unsafe chars with '-'.
  let s = raw
    .replace(/\s+/g, '-')
    .replace(/[^A-Za-z0-9_-]/g, '-')
    .replace(/-+/g, '-')
    .replace(/_+/g, '_')
    .replace(/^[-_]+|[-_]+$/g, '');

  // Keep short to avoid overflowing firmware-side buffers (firmware-side name buffer is fixed-size).
  // If firmware later publishes the exact max length, we can set this precisely.
  const MAX_NAME_LEN = 24;
  if (s.length > MAX_NAME_LEN) s = s.slice(0, MAX_NAME_LEN);

  return s || 'seq';
}

function generateUploadLinesFromData(id, name, data) {
  const cleanId = String(id || '').trim();
  const cleanName = sanitizeNameForFirmware(name);

  // Firmware v4 expects a numeric sequence id for `U i=` and `E i=`.
  if (!FIRMWARE_V4_SEQUENCE_ID_REGEX.test(cleanId)) {
    return {
      error:
        'Firmware v4 requires numeric sequence ids for uploads (U/E i=...). ' +
        `Got id="${cleanId}". Use a numeric id (e.g., autoincrement) or add a separate firmwareUploadId field.`
    };
  }

  const steps = Array.isArray(data?.steps) ? data.steps : null;
  if (!steps) {
    return { error: 'Sequence data.steps must be an array' };
  }

  if (steps.length > FIRMWARE_V4_MAX_SEQUENCE_LENGTH) {
    return {
      error:
        `Sequence has ${steps.length} steps but firmware v4 only accepts up to ${FIRMWARE_V4_MAX_SEQUENCE_LENGTH} steps per upload.`
    };
  }

  // builds line "U i={id} n={name} s={stepCount}" followed by lines for each step and ending with "E i={id}"
  const lines = [];
  lines.push(`U i=${cleanId} n=${cleanName} s=${steps.length}`);

  // NOTE: Firmware upload protocol expects `i=` to be the *step index* (0..N-1),
  // not the sequence id. Using the sequence id here would make every step share
  // the same index (overwriting or rejecting steps).
  for (let stepIndex = 0; stepIndex < steps.length; stepIndex++) {
    const step = steps[stepIndex];
    const { k, c, d } = step || {};

    if (k === undefined) return { error: 'Missing step.k' };
    if (!c) return { error: 'Missing step.c' };
    if (d === undefined) return { error: 'Missing step.d' };

    lines.push(`S i=${stepIndex} k=${k} c=${String(c).trim()} d=${d}`);
  }

  lines.push(`E i=${cleanId}`);
  return { ok: true, lines };
}

module.exports = {
  DB_PATH,
  listSequences,
  getSequence,
  upsertSequence,
  generateUploadLinesFromData
};