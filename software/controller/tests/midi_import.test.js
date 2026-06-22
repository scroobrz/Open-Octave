const { Midi } = require('@tonejs/midi');
const { importMidiBufferToSequence } = require('../services/midi_import');

function makeMidiBuffer(buildFn) {
  const midi = new Midi();
  buildFn(midi);
  return Buffer.from(midi.toArray());
}

describe('importMidiBufferToSequence', () => {
  it('rejects an empty or invalid buffer', () => {
    const result = importMidiBufferToSequence(null);

    expect(result.ok).toBe(false);
    expect(result.error).toMatch(/empty or invalid/i);
    expect(result.warnings).toEqual([]);
  });

  it('rejects a midi file with no playable notes', () => {
    const buffer = makeMidiBuffer(() => {
      // keep file valid but empty
    });

    const result = importMidiBufferToSequence(buffer, {
      filename: 'empty.mid'
    });

    expect(result.ok).toBe(false);
    expect(result.error).toMatch(/no playable note events/i);
  });

  it('imports a simple in-range melody into sequence steps', () => {
    const buffer = makeMidiBuffer((midi) => {
      const track = midi.addTrack();
      track.name = 'Melody';

      track.addNote({ midi: 60, time: 0, duration: 0.4 });
      track.addNote({ midi: 64, time: 0.5, duration: 0.35 });
      track.addNote({ midi: 67, time: 1.0, duration: 0.6 });
    });

    const result = importMidiBufferToSequence(buffer, {
      filename: 'simple-song.mid'
    });

    expect(result.ok).toBe(true);
    expect(result.warnings).toEqual([]);

    expect(result.sequence.name).toBe('Imported MIDI - simple-song');
    expect(result.sequence.description).toBe('Imported from MIDI file: simple-song.mid');
    expect(result.sequence.uploadLines).toEqual([]);

    expect(result.sequence.data.steps).toEqual([
      { keys: [0], colors: ['0000FF'], duration: 400 },
      { keys: [4], colors: ['FFFF00'], duration: 350 },
      { keys: [7], colors: ['FF00FF'], duration: 600 }
    ]);

    expect(result.meta.minKey).toBe(0);
    expect(result.meta.maxKey).toBe(7);
    expect(result.meta.requiredModules).toBe(1);
    expect(result.meta.noteCount).toBe(3);
    expect(result.meta.stepCount).toBe(3);
    expect(result.meta.transposeSemitones).toBe(0);
    expect(result.meta.source).toBe('combined');
    expect(result.meta.trackName).toBe(null);
  });

  it('transposes a lower melody by whole octaves when needed', () => {
    const buffer = makeMidiBuffer((midi) => {
      const track = midi.addTrack();
      track.name = 'Low Melody';

      track.addNote({ midi: 48, time: 0, duration: 0.3 });
      track.addNote({ midi: 52, time: 0.4, duration: 0.3 });
    });

    const result = importMidiBufferToSequence(buffer, {
      filename: 'low.mid'
    });

    expect(result.ok).toBe(true);
    expect(result.meta.transposeSemitones).toBe(12);
    expect(result.sequence.data.steps).toEqual([
      { keys: [0], colors: ['0000FF'], duration: 300 },
      { keys: [4], colors: ['FFFF00'], duration: 300 }
    ]);
    expect(result.warnings[0]).toMatch(/transposed up by 12 semitones/i);
  });

  it('rejects a midi file with a very large raw pitch span', () => {
    const buffer = makeMidiBuffer((midi) => {
      const track = midi.addTrack();
      track.addNote({ midi: 40, time: 0, duration: 0.5 });
      track.addNote({ midi: 100, time: 0.5, duration: 0.5 });
    });

    const result = importMidiBufferToSequence(buffer, {
      filename: 'too-wide.mid'
    });

    expect(result.ok).toBe(false);
    expect(result.error).toMatch(/note span is too large/i);
  });
});