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
// replaces spaces with hyphens to match firmware contract
function sanitizeNameForFirmware(name) {
  return String(name || '').trim().replace(/\s+/g, '-');
}

function generateUploadLinesFromData(id, name, data) {
  const cleanId = String(id || '').trim();
  const cleanName = sanitizeNameForFirmware(name);

  if (!cleanId) {
    return { error: 'Missing sequence id' };
  }

  const steps = Array.isArray(data?.steps) ? data.steps : null;
  if (!steps) {
    return { error: 'Sequence data.steps must be an array' };
  }

  // builds line "U i={id} n={name} s={stepCount}" followed by lines for each step and ending with "E i={id}"
  const lines = [];
  lines.push(`U i=${cleanId} n=${cleanName} s=${steps.length}`);

  for (const step of steps) {
    const { k, c, d } = step || {};

    if (k === undefined) return { error: 'Missing step.k' };
    if (!c) return { error: 'Missing step.c' };
    if (d === undefined) return { error: 'Missing step.d' };

    lines.push(`S i=${cleanId} k=${k} c=${String(c).trim()} d=${d}`);
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