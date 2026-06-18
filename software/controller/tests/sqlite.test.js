const fs = require('fs');
const os = require('os');
const path = require('path');
const Database = require('better-sqlite3');

let sqlite;
let tempDir;
let dbPath;
let rawDb;

beforeAll(() => {
  // temp db for tests
  tempDir = fs.mkdtempSync(path.join(os.tmpdir(), 'open-octave-sqlite-'));
  dbPath = path.join(tempDir, 'test.sqlite3');

  process.env.SQLITE_PATH = dbPath;

  // import after env set
  sqlite = require('../database/sqlite');
  rawDb = new Database(dbPath);
});

beforeEach(() => {
  // keep each test isolated
  rawDb.prepare('DELETE FROM sequences').run();
});

afterAll(() => {
  if (rawDb) rawDb.close();
  if (fs.existsSync(dbPath)) fs.unlinkSync(dbPath);
  if (fs.existsSync(tempDir)) fs.rmSync(tempDir, { recursive: true, force: true });
  delete process.env.SQLITE_PATH;
});

describe('computeMaxKey', () => {
  it('returns -1 for missing data', () => {
    expect(sqlite.computeMaxKey(null)).toBe(-1);
    expect(sqlite.computeMaxKey({})).toBe(-1);
    expect(sqlite.computeMaxKey({ steps: 'bad' })).toBe(-1);
  });

  it('returns max key from new format steps', () => {
    const data = {
      steps: [
        { keys: [0], colors: ['0000FF'], duration: 400 },
        { keys: [3, 7], colors: ['00FF00', 'FFFF00'], duration: 500 },
        { keys: [2], colors: ['FF8000'], duration: 300 }
      ]
    };

    expect(sqlite.computeMaxKey(data)).toBe(7);
  });

  it('returns max key from old format steps', () => {
    const data = {
      steps: [
        { k: 1, c: '#00FFFF', d: 300 },
        { k: 5, c: '#00FF00', d: 300 },
        { k: 2, c: '#FFFF00', d: 300 }
      ]
    };

    expect(sqlite.computeMaxKey(data)).toBe(5);
  });

  it('ignores non-number keys', () => {
    const data = {
      steps: [
        { keys: ['x', 4], colors: ['0000FF', '00FF00'], duration: 300 },
        { keys: [2], colors: ['FFFF00'], duration: 300 }
      ]
    };

    expect(sqlite.computeMaxKey(data)).toBe(4);
  });
});

describe('generateUploadLinesFromData', () => {
  it('builds upload lines for a simple valid sequence', () => {
    const result = sqlite.generateUploadLinesFromData(
      12,
      'Ode To Joy',
      {
        steps: [
          { keys: [0], colors: ['0000FF'], duration: 400 },
          { keys: [1], colors: ['00FF00'], duration: 500 }
        ]
      }
    );

    expect(result.ok).toBe(true);
    expect(result.lines).toEqual([
      'U i=12 n=Ode-To-Joy s=2',
      'S k=0 c=0000FF d=400',
      'S k=1 c=00FF00 d=500',
      'E i=12'
    ]);
  });

  it('builds upload lines for a chord step', () => {
    const result = sqlite.generateUploadLinesFromData(
      7,
      'Chord Test',
      {
        steps: [
          {
            keys: [0, 4, 7],
            colors: ['0000FF', '00FF00', 'FFFF00'],
            duration: 600
          }
        ]
      }
    );

    expect(result.ok).toBe(true);
    expect(result.lines).toEqual([
      'U i=7 n=Chord-Test s=1',
      'S k=0.4.7 c=0000FF.00FF00.FFFF00 d=600',
      'E i=7'
    ]);
  });

  it('supports old format step data', () => {
    const result = sqlite.generateUploadLinesFromData(
      3,
      'Legacy Song',
      {
        steps: [
          { k: 2, c: '0000FF', d: 250 }
        ]
      }
    );

    expect(result.ok).toBe(true);
    expect(result.lines).toEqual([
      'U i=3 n=Legacy-Song s=1',
      'S k=2 c=0000FF d=250',
      'E i=3'
    ]);
  });

  it('sanitises long and messy names', () => {
    const result = sqlite.generateUploadLinesFromData(
      9,
      '  My Very !!! Strange___Sequence Name That Is Definitely Too Long  ',
      {
        steps: [
          { keys: [1], colors: ['0000FF'], duration: 300 }
        ]
      }
    );

    expect(result.ok).toBe(true);
    expect(result.lines[0]).toBe('U i=9 n=My-Very-Strange_Sequence-Name-T s=1');
  });

  it('falls back to seq when name becomes empty', () => {
    const result = sqlite.generateUploadLinesFromData(
      4,
      '!!!',
      {
        steps: [
          { keys: [1], colors: ['0000FF'], duration: 300 }
        ]
      }
    );

    expect(result.ok).toBe(true);
    expect(result.lines[0]).toBe('U i=4 n=seq s=1');
  });

  it('rejects non-numeric sequence id', () => {
    const result = sqlite.generateUploadLinesFromData(
      'abc',
      'Bad Id',
      {
        steps: [
          { keys: [0], colors: ['0000FF'], duration: 300 }
        ]
      }
    );

    expect(result.ok).toBeUndefined();
    expect(result.error).toMatch(/numeric sequence ids/i);
  });

  it('rejects missing steps array', () => {
    const result = sqlite.generateUploadLinesFromData(1, 'No Steps', {});

    expect(result.ok).toBeUndefined();
    expect(result.error).toBe('Sequence data.steps must be an array');
  });

  it('rejects step with missing keys', () => {
    const result = sqlite.generateUploadLinesFromData(
      1,
      'Bad Step',
      {
        steps: [
          { colors: ['0000FF'], duration: 300 }
        ]
      }
    );

    expect(result.error).toBe('Step 0: missing keys');
  });

  it('rejects step with missing colors', () => {
    const result = sqlite.generateUploadLinesFromData(
      1,
      'Bad Step',
      {
        steps: [
          { keys: [0], duration: 300 }
        ]
      }
    );

    expect(result.error).toBe('Step 0: missing colors');
  });

  it('rejects step with missing duration', () => {
    const result = sqlite.generateUploadLinesFromData(
      1,
      'Bad Step',
      {
        steps: [
          { keys: [0], colors: ['0000FF'] }
        ]
      }
    );

    expect(result.error).toBe('Step 0: missing duration');
  });

  it('rejects mismatched key and color lengths', () => {
    const result = sqlite.generateUploadLinesFromData(
      1,
      'Mismatch',
      {
        steps: [
          {
            keys: [0, 1],
            colors: ['0000FF'],
            duration: 300
          }
        ]
      }
    );

    expect(result.error).toBe('Step 0: keys and colors arrays must have same length');
  });

  it('rejects invalid colors', () => {
    const result = sqlite.generateUploadLinesFromData(
      1,
      'Bad Color',
      {
        steps: [
          {
            keys: [0],
            colors: ['#123456'],
            duration: 300
          }
        ]
      }
    );

    expect(result.ok).toBeUndefined();
    expect(result.error).toMatch(/not allowed/i);
  });

  it('remaps colors in colorblind mode', () => {
    const result = sqlite.generateUploadLinesFromData(
      5,
      'CB Mode',
      {
        steps: [
          {
            keys: [0, 1],
            colors: ['0000FF', '00FF00'],
            duration: 450
          }
        ]
      },
      'colorblind'
    );

    expect(result.ok).toBe(true);
    expect(result.lines[1]).not.toContain('0000FF.00FF00');
    expect(result.lines[1]).toMatch(/^S k=0\.1 c=/);
  });

  it('rejects sequences longer than firmware max', () => {
    const steps = Array.from({ length: 129 }, () => ({
      keys: [0],
      colors: ['0000FF'],
      duration: 100
    }));

    const result = sqlite.generateUploadLinesFromData(2, 'Too Long', { steps });

    expect(result.ok).toBeUndefined();
    expect(result.error).toMatch(/only accepts up to 128 steps/i);
  });
});

describe('sequence CRUD', () => {
  it('inserts and fetches a sequence', () => {
    const data = {
      steps: [
        { keys: [0], colors: ['0000FF'], duration: 300 },
        { keys: [2], colors: ['00FF00'], duration: 400 }
      ]
    };

    const upload = sqlite.generateUploadLinesFromData(1, 'Test Song', data);

    const id = sqlite.upsertSequence({
      name: 'Test Song',
      description: 'Simple test',
      data,
      uploadLines: upload
    });

    const saved = sqlite.getSequence(id);

    expect(Number(id)).toBeGreaterThan(0);
    expect(saved).not.toBeNull();
    expect(saved.name).toBe('Test Song');
    expect(saved.description).toBe('Simple test');
    expect(saved.data).toEqual(data);
    expect(saved.maxKey).toBe(2);
    expect(saved.uploadLines).toEqual(upload);
    expect(saved.createdAt).toBeTruthy();
    expect(saved.updatedAt).toBeTruthy();
  });

  it('updates an existing sequence when numeric id exists', () => {
    const firstData = {
      steps: [
        { keys: [0], colors: ['0000FF'], duration: 300 }
      ]
    };

    const firstUpload = sqlite.generateUploadLinesFromData(1, 'Original', firstData);

    const id = sqlite.upsertSequence({
      name: 'Original',
      description: 'Before update',
      data: firstData,
      uploadLines: firstUpload
    });

    const updatedData = {
      steps: [
        { keys: [5], colors: ['FFFF00'], duration: 800 }
      ]
    };

    const updatedUpload = sqlite.generateUploadLinesFromData(id, 'Updated Song', updatedData);

    const returnedId = sqlite.upsertSequence({
      id,
      name: 'Updated Song',
      description: 'After update',
      data: updatedData,
      uploadLines: updatedUpload
    });

    const saved = sqlite.getSequence(id);
    const countRow = rawDb.prepare('SELECT COUNT(*) AS count FROM sequences').get();

    expect(Number(returnedId)).toBe(Number(id));
    expect(countRow.count).toBe(1);
    expect(saved.name).toBe('Updated Song');
    expect(saved.description).toBe('After update');
    expect(saved.data).toEqual(updatedData);
    expect(saved.maxKey).toBe(5);
  });

  it('inserts a new row when id is non-numeric', () => {
    const id = sqlite.upsertSequence({
      id: 'abc',
      name: 'Non Numeric Id',
      description: '',
      data: { steps: [] },
      uploadLines: []
    });

    const saved = sqlite.getSequence(id);

    expect(Number(id)).toBeGreaterThan(0);
    expect(saved.name).toBe('Non Numeric Id');
  });

  it('lists sequences in ascending id order with summary fields', () => {
    const id1 = sqlite.upsertSequence({
      name: 'First',
      description: 'one',
      data: {
        steps: [
          { keys: [1], colors: ['0000FF'], duration: 200 }
        ]
      },
      uploadLines: ['U i=1 n=First s=1', 'S k=1 c=0000FF d=200', 'E i=1']
    });

    const id2 = sqlite.upsertSequence({
      name: 'Second',
      description: 'two',
      data: {
        steps: [
          { keys: [4, 6], colors: ['00FF00', 'FFFF00'], duration: 500 }
        ]
      },
      uploadLines: ['U i=2 n=Second s=1', 'S k=4.6 c=00FF00.FFFF00 d=500', 'E i=2']
    });

    const list = sqlite.listSequences();

    expect(list).toHaveLength(2);
    expect(list[0].id).toBe(Number(id1));
    expect(list[0].name).toBe('First');
    expect(list[0].stepCount).toBe(1);
    expect(list[0].maxKey).toBe(1);

    expect(list[1].id).toBe(Number(id2));
    expect(list[1].name).toBe('Second');
    expect(list[1].stepCount).toBe(1);
    expect(list[1].maxKey).toBe(6);
    expect(list[1].updatedAt).toBeTruthy();
  });

  it('returns null for missing sequence', () => {
    expect(sqlite.getSequence(99999)).toBeNull();
  });

  it('deletes an existing sequence', () => {
    const id = sqlite.upsertSequence({
      name: 'Delete Me',
      description: '',
      data: {
        steps: [
          { keys: [2], colors: ['0000FF'], duration: 200 }
        ]
      },
      uploadLines: ['U i=1 n=Delete-Me s=1', 'S k=2 c=0000FF d=200', 'E i=1']
    });

    const deleted = sqlite.deleteSequence(id);
    const saved = sqlite.getSequence(id);

    expect(deleted).toBe(true);
    expect(saved).toBeNull();
  });

  it('returns false when deleting missing sequence', () => {
    expect(sqlite.deleteSequence(123456)).toBe(false);
  });
});