#ifndef FIRMWARE_V3_SEQUENCES_H
#define FIRMWARE_V3_SEQUENCES_H

#include "firmware_v3_config.h"

/*
 * SEQUENCE DEFINITIONS
 * 
 * This file contains all playable sequences for the Open Octave keyboard.
 * Each sequence is an array of SequenceStep structs, wrapped in a Sequence struct.
 * 
 * To add a new sequence:
 *   1. Define a new step array (e.g., seq3_steps[])
 *   2. Add it to the sequences[] array at the bottom
 *   3. Update NUM_SEQUENCES in firmware_v3_config.h
 * 
 * SequenceStep format: {keyIndex, color, duration_ms}
 *   - keyIndex: 0 to NUM_KEYS-1
 *   - color: COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_WHITE, or hex value
 *   - duration_ms: how long to hold the step in milliseconds
 */

// ============ SEQUENCE STEP ARRAYS ============

// Sequence 0: Simple alternating pattern (2 keys)
const SequenceStep seq0_steps[] = {
  {0, COLOR_BLUE, 500},
  {1, COLOR_GREEN, 500},
  {0, COLOR_BLUE, 500},
  {1, COLOR_RED, 500}
};

// Sequence 1: Three-key ascending/descending scale
const SequenceStep seq1_steps[] = {
  {0, COLOR_RED, 400},
  {1, COLOR_GREEN, 400},
  {2, COLOR_BLUE, 400},
  {2, COLOR_BLUE, 400},
  {1, COLOR_GREEN, 400},
  {0, COLOR_RED, 400}
};

// Sequence 2: Fast repeating pattern
const SequenceStep seq2_steps[] = {
  {0, COLOR_WHITE, 200},
  {1, COLOR_WHITE, 200},
  {2, COLOR_WHITE, 200},
  {0, COLOR_RED, 200},
  {1, COLOR_GREEN, 200},
  {2, COLOR_BLUE, 200}
};

// ============ SEQUENCE ARRAY ============
// This array holds all available sequences.
// Format: {pointer_to_steps, number_of_steps, "display_name"}

const Sequence sequences[NUM_SEQUENCES] = {
  {seq0_steps, sizeof(seq0_steps) / sizeof(SequenceStep), "Alternating"},
  {seq1_steps, sizeof(seq1_steps) / sizeof(SequenceStep), "Scale"},
  {seq2_steps, sizeof(seq2_steps) / sizeof(SequenceStep), "Fast"}
};

#endif
