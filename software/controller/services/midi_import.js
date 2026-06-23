//midi import
// Converts an uploaded MIDI buffer into the Open Octave sequence format used by
// the existing SQLite sequence library and firmware upload generator.

const { Midi } = require('@tonejs/midi');
const COLORS = require('../../shared/colors.json');

const MIDI_C4 = 60;
const MIN_SUPPORTED_KEY = 0;
const MAX_SUPPORTED_KEY = 23; // Demo 3 current target: 2 modules max = 24 keys
const MIN_SUPPORTED_MIDI = MIDI_C4 + MIN_SUPPORTED_KEY;   // C4
const MAX_SUPPORTED_MIDI = MIDI_C4 + MAX_SUPPORTED_KEY;   // B5
const MAX_IMPORTABLE_SPAN_SEMITONES = MAX_SUPPORTED_KEY;  // 23 semitones inclusive span = 24 keys
const MAX_RAW_MIDI_SPAN_SEMITONES = 48; // midi import: reject very wide/full-arrangement MIDI early
const MAX_IMPORTABLE_NOTE_COUNT = 2000;
const MAX_SEQUENCE_LENGTH = 256; // midi import: allow longer beginner songs while staying within current firmware limits
const MAX_KEYS_PER_STEP = 8;
const MIN_NOTE_DURATION_MS = 300; // must match firmware MIN_STEP_DURATION
const MAX_NOTE_DURATION_MS = 10000;

function sanitizeSequenceName(name) {
  const raw = String(name || 'Imported MIDI').replace(/,/g, ' ').trim();
  if (!raw) return 'Imported MIDI';
  return raw.slice(0, 31);
}

function stripExtension(filename) {
  const f = String(filename || '').trim();
  if (!f) return 'Imported MIDI';
  return f.replace(/\.[^/.]+$/, '');
}

function clampDuration(ms) {
  const rounded = Math.round(Number(ms) || 0);
  if (rounded < MIN_NOTE_DURATION_MS) return MIN_NOTE_DURATION_MS;
  if (rounded > MAX_NOTE_DURATION_MS) return MAX_NOTE_DURATION_MS;
  return rounded;
}

function midiNoteToGlobalKey(midiNote) {
  return Number(midiNote) - MIDI_C4;
}

// midi import
// Shift by octaves only so imported melodies stay musically recognisable while
// fitting inside the current Demo 3 keyboard range (C4 to B5).
function chooseTransposeSemitones(minMidi, maxMidi) {
  const rawSpan = Number(maxMidi) - Number(minMidi);

  // midi import
  // Hard-stop only on clearly unsuitable files, such as full piano/orchestral MIDI
  // with a very wide pitch span. Smaller files should still be given a chance to
  // fit by whole-octave transposition.
  if (rawSpan > MAX_RAW_MIDI_SPAN_SEMITONES) {
    return {
      ok: false,
      error: `MIDI note span is too large for Open Octave import. Maximum supported raw span is ${MAX_RAW_MIDI_SPAN_SEMITONES + 1} semitones.`
    };
  }

  // Determine all octave shifts that place the whole melody inside range.
  const minShift = Math.ceil((MIN_SUPPORTED_MIDI - minMidi) / 12);
  const maxShift = Math.floor((MAX_SUPPORTED_MIDI - maxMidi) / 12);

  if (minShift > maxShift) {
    return {
      ok: false,
      error: 'MIDI file cannot be transposed by whole octaves into the supported C4-B5 range.'
    };
  }

  // Choose the smallest-magnitude octave shift so the imported melody stays as
  // close as possible to the original register.
  let bestShift = minShift;
  for (let shift = minShift; shift <= maxShift; shift++) {
    if (Math.abs(shift) < Math.abs(bestShift)) {
      bestShift = shift;
    } else if (Math.abs(shift) === Math.abs(bestShift) && shift > bestShift) {
      bestShift = shift;
    }
  }

  return {
    ok: true,
    semitones: bestShift * 12
  };
}

function colorForGlobalKey(globalKey) {
  const localKey = ((Number(globalKey) % 12) + 12) % 12;
  const finger = COLORS.keyToFinger[String(localKey)];
  const hex = COLORS.fingerColors[finger] || COLORS.fallbackColor || 'FFFFFF';
  return String(hex).replace('#', '').toUpperCase();
}

function groupNotesByStartTime(noteItems) {
  const groups = new Map();

  for (const note of noteItems) {
    const startMs = Math.round(note.time * 1000);
    if (!groups.has(startMs)) {
      groups.set(startMs, []);
    }
    groups.get(startMs).push(note);
  }

  return [...groups.entries()]
    .sort((a, b) => a[0] - b[0])
    .map(([startMs, notes]) => ({ startMs, notes }));
}

function buildPlayableCandidate(rawNotes, warnings, options = {}) {
  if (!Array.isArray(rawNotes) || rawNotes.length === 0) {
    return { ok: false, error: 'No playable note events were found in this MIDI file.', warnings };
  }

  if (rawNotes.length > MAX_IMPORTABLE_NOTE_COUNT) {
    return {
      ok: false,
      error: `MIDI file is too dense for import. Maximum supported note count is ${MAX_IMPORTABLE_NOTE_COUNT}.`,
      warnings
    };
  }

  const rawMidiValues = rawNotes.map((n) => Number(n.midi));
  const rawMinMidi = Math.min(...rawMidiValues);
  const rawMaxMidi = Math.max(...rawMidiValues);

  const transposeDecision = chooseTransposeSemitones(rawMinMidi, rawMaxMidi);
  if (!transposeDecision.ok) {
    return {
      ok: false,
      error: transposeDecision.error,
      warnings
    };
  }

  const transposeSemitones = transposeDecision.semitones;
  const collectedNotes = rawNotes.map((note) => {
    const shiftedMidi = note.midi + transposeSemitones;
    return {
      ...note,
      midi: shiftedMidi,
      globalKey: midiNoteToGlobalKey(shiftedMidi)
    };
  });

  if (transposeSemitones !== 0) {
    const direction = transposeSemitones > 0 ? 'up' : 'down';
    warnings.push(
      `Imported MIDI was transposed ${direction} by ${Math.abs(transposeSemitones)} semitones to fit the supported C4-B5 range.`
    );
  }

  const grouped = groupNotesByStartTime(collectedNotes);
  const steps = [];

  for (const group of grouped) {
    if (steps.length >= MAX_SEQUENCE_LENGTH) {
      return {
        ok: false,
        error: `Converted MIDI exceeds the maximum supported sequence length of ${MAX_SEQUENCE_LENGTH} steps.`,
        warnings
      };
    }

    const sortedNotes = [...group.notes].sort((a, b) => a.globalKey - b.globalKey);

    const uniqueByKey = new Map();
    for (const note of sortedNotes) {
      if (!uniqueByKey.has(note.globalKey)) {
        uniqueByKey.set(note.globalKey, note);
      }
    }

    let uniqueNotes = [...uniqueByKey.values()];

    if (uniqueNotes.length > MAX_KEYS_PER_STEP) {
      warnings.push(
        `Chord at ${group.startMs}ms had ${uniqueNotes.length} notes; trimmed to first ${MAX_KEYS_PER_STEP} notes.`
      );
      uniqueNotes = uniqueNotes.slice(0, MAX_KEYS_PER_STEP);
    }

    const keys = uniqueNotes.map((n) => n.globalKey);
    const colors = keys.map((k) => colorForGlobalKey(k));
    const longestDurationMs = clampDuration(
      Math.max(...uniqueNotes.map((n) => (Number(n.duration) || 0) * 1000))
    );

    steps.push({
      keys,
      colors,
      duration: longestDurationMs
    });
  }

  const allKeys = steps.flatMap((s) => s.keys);
  const minKey = Math.min(...allKeys);
  const maxKey = Math.max(...allKeys);
  const requiredModules = Math.max(1, Math.ceil((maxKey + 1) / 12));

  return {
    ok: true,
    steps,
    meta: {
      minKey,
      maxKey,
      requiredModules,
      noteCount: collectedNotes.length,
      stepCount: steps.length,
      transposeSemitones
    },
    warnings,
    score: {
      noteCount: collectedNotes.length,
      stepCount: steps.length,
      span: maxKey - minKey
    },
    trackName: options.trackName || null,
    trackIndex: options.trackIndex ?? null,
    source: options.source || 'combined'
  };
}

function compareCandidateScores(a, b) {
  if (!a) return 1;
  if (!b) return -1;

  if ((b.score?.noteCount || 0) !== (a.score?.noteCount || 0)) {
    return (b.score?.noteCount || 0) - (a.score?.noteCount || 0);
  }

  if ((b.score?.stepCount || 0) !== (a.score?.stepCount || 0)) {
    return (b.score?.stepCount || 0) - (a.score?.stepCount || 0);
  }

  return (a.score?.span || 0) - (b.score?.span || 0);
}

function chooseBestPlayableCandidate(candidates) {
  const playable = (candidates || []).filter((c) => c && c.ok);
  if (playable.length === 0) return null;

  playable.sort(compareCandidateScores);
  return playable[0];
}

function importMidiBufferToSequence(buffer, options = {}) {
  try {
    if (!buffer || !Buffer.isBuffer(buffer) || buffer.length === 0) {
      return { ok: false, error: 'Uploaded file is empty or invalid.', warnings: [] };
    }

    const midi = new Midi(buffer);
    const warnings = [];
    const trackCandidates = [];
    const combinedRawNotes = [];

    for (let trackIndex = 0; trackIndex < (midi.tracks || []).length; trackIndex++) {
      const track = midi.tracks[trackIndex];
      if (!track || !Array.isArray(track.notes) || track.notes.length === 0) continue;

      if (track.instrument && track.instrument.percussion) continue;

      const trackRawNotes = [];
      for (const note of track.notes) {
        if (typeof note.midi !== 'number') continue;

        const normalizedNote = {
          midi: note.midi,
          name: note.name,
          time: note.time,
          duration: note.duration
        };

        trackRawNotes.push(normalizedNote);
        combinedRawNotes.push(normalizedNote);
      }

      if (trackRawNotes.length > 0) {
        const trackWarnings = [];
        const trackName = String(track.name || '').trim() || `Track ${trackIndex + 1}`;
        const candidate = buildPlayableCandidate(trackRawNotes, trackWarnings, {
          trackIndex,
          trackName,
          source: 'single-track'
        });

        if (candidate.ok) {
          trackCandidates.push(candidate);
        }
      }
    }

    if (combinedRawNotes.length === 0) {
      return {
        ok: false,
        error: 'No playable note events were found in this MIDI file.',
        warnings
      };
    }

    const combinedWarnings = [];
    const combinedCandidate = buildPlayableCandidate(combinedRawNotes, combinedWarnings, {
      source: 'combined'
    });

    let chosenCandidate = null;
    if (combinedCandidate.ok) {
      chosenCandidate = combinedCandidate;
      warnings.push(...combinedWarnings);
    } else {
      chosenCandidate = chooseBestPlayableCandidate(trackCandidates);

      if (!chosenCandidate) {
        return {
          ok: false,
          error: combinedCandidate.error,
          warnings
        };
      }

      warnings.push(
        'Full multi-track MIDI could not fit the current keyboard, so import used the most playable single track instead.'
      );

      if (chosenCandidate.trackName) {
        warnings.push(`Selected MIDI track: ${chosenCandidate.trackName}.`);
      }

      warnings.push(...chosenCandidate.warnings);
    }

    const baseName = options.nameOverride
      ? sanitizeSequenceName(options.nameOverride)
      : sanitizeSequenceName(`Imported MIDI - ${stripExtension(options.filename)}`);

    const sequence = {
      name: baseName,
      description: `Imported from MIDI file${options.filename ? `: ${options.filename}` : ''}`,
      data: { steps: chosenCandidate.steps },
      uploadLines: []
    };

    return {
      ok: true,
      sequence,
      meta: {
        ...chosenCandidate.meta,
        source: chosenCandidate.source,
        trackName: chosenCandidate.trackName,
        trackIndex: chosenCandidate.trackIndex
      },
      warnings
    };
  } catch (err) {
    return {
      ok: false,
      error: `Failed to parse MIDI file: ${err.message}`,
      warnings: []
    };
  }
}

module.exports = {
  importMidiBufferToSequence
};