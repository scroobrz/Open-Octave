// ==============================
// Open Octave - SQLite Layer
// ==============================
// Firmware v5 stores only:
//   - 1 default sequence
//   - 1 uploaded sequence
// Software stores many sequences in SQLite and uploads one at a time.
//
// This module isolates all DB logic so server.js remains readable.

const path = require('path');
const Database = require('better-sqlite3');

// Shared colour definitions (single source of truth for controller + frontend)
const COLORS = require('../../shared/colors.json');

// Legacy branded hex values (before the solid-colour palette update).
// Sequences saved with these colours are still valid and get normalised
// to the current defaults at upload time.
const LEGACY_FINGER_COLORS = {
  thumb: '00B4D8', index: '4ECB71', middle: 'FFD700', ring: 'FF6B35', pinky: 'E8368F'
};
const LEGACY_CB_COLORS = {
  thumb: '0072B2', index: '009E73', middle: 'F0E442', ring: 'D55E00', pinky: 'CC79A7'
};

// Map legacy hex → current default hex.
const LEGACY_COLOR_REMAP = {};
for (const finger of COLORS.fingerOrder) {
  const def = COLORS.fingerColors[finger].toUpperCase();
  const legacy = LEGACY_FINGER_COLORS[finger]?.toUpperCase();
  const legacyCb = LEGACY_CB_COLORS[finger]?.toUpperCase();
  if (legacy && legacy !== def) LEGACY_COLOR_REMAP[legacy] = def;
  if (legacyCb && legacyCb !== def) LEGACY_COLOR_REMAP[legacyCb] = def;
}

// Build the set of allowed hex colours (uppercase) from the shared definitions.
// Include current palettes and legacy colours.
const ALLOWED_COLORS = new Set([
  ...Object.values(COLORS.fingerColors).map(c => c.toUpperCase()),
  ...(COLORS.alternativePalettes?.colorblind
    ? Object.values(COLORS.alternativePalettes.colorblind.fingerColors).map(c => c.toUpperCase())
    : []),
  ...Object.values(LEGACY_FINGER_COLORS).map(c => c.toUpperCase()),
  ...Object.values(LEGACY_CB_COLORS).map(c => c.toUpperCase())
]);

// Build a remap table: default hex → CB hex (for colourblind upload mode).
const CB_COLOR_REMAP = {};
if (COLORS.alternativePalettes?.colorblind) {
  for (const finger of COLORS.fingerOrder) {
    const original = COLORS.fingerColors[finger].toUpperCase();
    const cbColor = COLORS.alternativePalettes.colorblind.fingerColors[finger].toUpperCase();
    CB_COLOR_REMAP[original] = cbColor;
  }
}

// Firmware v5 hard-limits uploads to MAX_SEQUENCE_LENGTH (see firmware_V5_config.h).
const FIRMWARE_MAX_SEQUENCE_NOTES = 512;

// Firmware v5 parses `i=` (sequence id) using atoi(), so it must be numeric.
const FIRMWARE_SEQUENCE_ID_REGEX = /^\d+$/;

// Allow override via env
const DB_PATH =
  process.env.SQLITE_PATH ||
  path.join(__dirname, 'open-octave.sqlite3');

const db = new Database(DB_PATH);
db.pragma('journal_mode = WAL'); // use write-ahead logging mode

// ------------------------------
// Schema
// ------------------------------
db.exec(`
  CREATE TABLE IF NOT EXISTS sequences (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
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
// Data Format Migration
// ------------------------------
// Converts old format { k, c, d } steps to new format { keys: [], colors: [], duration }.
// Runs once on startup.
function migrateOldFormatSteps() {
  const rows = db.prepare('SELECT id, data_json FROM sequences').all();
  let migrated = 0;

  for (const row of rows) {
    const data = safeJsonParse(row.data_json, null);
    if (!data || !Array.isArray(data.steps) || data.steps.length === 0) continue;

    // Check if already in new format (first step has 'keys' array)
    const firstStep = data.steps[0];
    if (Array.isArray(firstStep.keys)) continue; // Already migrated

    // Check if it's old format (has 'k' property)
    if (firstStep.k === undefined && firstStep.keys === undefined) continue;

    // Migrate
    const newSteps = data.steps.map(s => ({
      keys: [s.k],
      colors: [s.c],
      duration: s.d
    }));

    const newData = { steps: newSteps };
    db.prepare('UPDATE sequences SET data_json = ?, updated_at = ? WHERE id = ?')
      .run(JSON.stringify(newData), nowIso(), row.id);
    migrated++;
  }

  if (migrated > 0) {
    console.log(`[DB] Migrated ${migrated} sequence(s) from old format to new format`);
  }
}

// Run migration on module load
migrateOldFormatSteps();

// ------------------------------
// Helpers
// ------------------------------
// Compute maxKey: highest key index used in any step of a sequence.
function computeMaxKey(data) {
  if (!data || !Array.isArray(data.steps)) return -1;
  let max = -1;
  for (const step of data.steps) {
    const keys = Array.isArray(step.keys) ? step.keys : (step.k !== undefined ? [step.k] : []);
    for (const k of keys) {
      if (typeof k === 'number' && k > max) max = k;
    }
  }
  return max;
}

// ------------------------------
// CRUD
// ------------------------------
function listSequences() {
  const rows = db
    .prepare(`
      SELECT id, name, description, data_json, updated_at
      FROM sequences
      ORDER BY id ASC
    `)
    .all();

  return rows.map((r) => {
    const data = safeJsonParse(r.data_json, null);
    const stepCount = Array.isArray(data?.steps) ? data.steps.length : null;
    const maxKey = computeMaxKey(data);

    return {
      id: Number(r.id),
      name: r.name,
      description: r.description || '',
      stepCount,
      maxKey,
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
    .get(Number(id));

  if (!row) return null;

  const data = safeJsonParse(row.data_json, {});

  return {
    id: Number(row.id),
    name: row.name,
    description: row.description || '',
    data,
    maxKey: computeMaxKey(data),
    uploadLines: safeJsonParse(row.upload_lines_json, []),
    createdAt: row.created_at,
    updatedAt: row.updated_at
  };
}

function upsertSequence(seq) {
  const ts = nowIso();

  const hasNumericId =
    seq.id !== undefined &&
    seq.id !== null &&
    FIRMWARE_SEQUENCE_ID_REGEX.test(String(seq.id));

  if (hasNumericId) {
    const existing = db
      .prepare(`SELECT created_at FROM sequences WHERE id = ?`)
      .get(seq.id);

    if (existing) {
      db.prepare(`
        UPDATE sequences
        SET
          name = @name,
          description = @description,
          data_json = @data_json,
          upload_lines_json = @upload_lines_json,
          updated_at = @updated_at
        WHERE id = @id
      `).run({
        id: Number(seq.id),
        name: seq.name,
        description: seq.description || '',
        data_json: JSON.stringify(seq.data || {}),
        upload_lines_json: JSON.stringify(seq.uploadLines || []),
        updated_at: ts
      });

      return Number(seq.id);
    }
  }

  // Insert new
  if (hasNumericId) {
    db.prepare(`
      INSERT INTO sequences
      (id, name, description, data_json, upload_lines_json, created_at, updated_at)
      VALUES (@id, @name, @description, @data_json, @upload_lines_json, @created_at, @updated_at)
    `).run({
      id: Number(seq.id),
      name: seq.name,
      description: seq.description || '',
      data_json: JSON.stringify(seq.data || {}),
      upload_lines_json: JSON.stringify(seq.uploadLines || []),
      created_at: ts,
      updated_at: ts
    });
    return Number(seq.id);
  }

  const result = db.prepare(`
    INSERT INTO sequences
    (name, description, data_json, upload_lines_json, created_at, updated_at)
    VALUES (@name, @description, @data_json, @upload_lines_json, @created_at, @updated_at)
  `).run({
    name: seq.name,
    description: seq.description || '',
    data_json: JSON.stringify(seq.data || {}),
    upload_lines_json: JSON.stringify(seq.uploadLines || []),
    created_at: ts,
    updated_at: ts
  });

  return result.lastInsertRowid;
}

function deleteSequence(id) {
  const result = db.prepare('DELETE FROM sequences WHERE id = ?').run(Number(id));
  return result.changes > 0;
}

// ------------------------------
// Upload Line Generator
// ------------------------------
function sanitizeNameForFirmware(name) {
  const raw = String(name || '').trim();

  let s = raw
    .replace(/\s+/g, '-')
    .replace(/[^A-Za-z0-9_-]/g, '-')
    .replace(/-+/g, '-')
    .replace(/_+/g, '_')
    .replace(/^[-_]+|[-_]+$/g, '');

  const MAX_NAME_LEN = 31;
  if (s.length > MAX_NAME_LEN) s = s.slice(0, MAX_NAME_LEN);

  return s || 'seq';
}

// Updated for firmware v5 protocol: supports multi-key steps with dot-separated values.
function generateUploadLinesFromData(id, name, data, colorMode = 'default') {
  const cleanId = String(id !== undefined && id !== null ? id : '').trim();
  const cleanName = sanitizeNameForFirmware(name);

  if (!FIRMWARE_SEQUENCE_ID_REGEX.test(cleanId)) {
    return {
      error:
        'Firmware v5 requires numeric sequence ids for uploads (U/E i=...). ' +
        `Got id="${cleanId}". Use a numeric id.`
    };
  }

  const steps = Array.isArray(data?.steps) ? data.steps : null;
  if (!steps) {
    return { error: 'Sequence data.steps must be an array' };
  }

  let tempNotes = [];
  let currentStartTime = 0;
  for (let stepIndex = 0; stepIndex < steps.length; stepIndex++) {
    const step = steps[stepIndex];
    const keys = Array.isArray(step.keys) ? step.keys : (step.k !== undefined ? [step.k] : undefined);
    const colors = Array.isArray(step.colors) ? step.colors : (step.c !== undefined ? [step.c] : undefined);
    let duration = step.duration !== undefined ? step.duration : step.d;

    if (!keys || keys.length === 0) return { error: `Step ${stepIndex}: missing keys` };
    if (!colors || colors.length === 0) return { error: `Step ${stepIndex}: missing colors` };
    if (duration === undefined) return { error: `Step ${stepIndex}: missing duration` };
    if (keys.length !== colors.length) return { error: `Step ${stepIndex}: keys and colors arrays must have same length` };

    if (duration < 300) duration = 300;
    if (duration > 10000) duration = 10000;

    for (let ci = 0; ci < colors.length; ci++) {
      const colorHex = String(colors[ci]).trim().toUpperCase();
      if (!ALLOWED_COLORS.has(colorHex)) {
        const allowed = [...ALLOWED_COLORS].join(', ');
        return {
          error: `Step ${stepIndex}: colour "${colors[ci]}" is not allowed. Allowed: ${allowed}`
        };
      }

      const normColor = LEGACY_COLOR_REMAP[colorHex] || colorHex;
      const finalColor = (colorMode === 'colorblind' && CB_COLOR_REMAP[normColor])
        ? CB_COLOR_REMAP[normColor]
        : normColor;
      
      tempNotes.push({ key: keys[ci], color: finalColor, t: currentStartTime, d: duration });
    }
    
    currentStartTime += duration;
  }

  if (tempNotes.length > FIRMWARE_MAX_SEQUENCE_NOTES) {
    return {
      error:
        `Sequence has ${tempNotes.length} notes but firmware v5 only accepts up to ${FIRMWARE_MAX_SEQUENCE_NOTES} notes per upload.`
    };
  }

  const lines = [];
  lines.push(`U i=${cleanId} n=${cleanName} s=${tempNotes.length}`);

  for (const n of tempNotes) {
    lines.push(`S k=${n.key} c=${n.color} t=${n.t} d=${n.d}`);
  }

  lines.push(`E i=${cleanId}`);
  return { ok: true, lines };
}

module.exports = {
  DB_PATH,
  listSequences,
  getSequence,
  upsertSequence,
  deleteSequence,
  generateUploadLinesFromData,
  computeMaxKey
};
