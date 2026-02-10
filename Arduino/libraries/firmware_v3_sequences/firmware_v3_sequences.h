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
  {0, COLOR_RED, 500},
  {1, COLOR_GREEN, 500},
  {2, COLOR_BLUE, 500},
  {1, COLOR_GREEN, 500},
  {0, COLOR_RED, 500},
};

// Sequence 1: Three-key ascending/descending scale
const SequenceStep seq1_steps[] = {
  {0, COLOR_RED, 400},
  {1, COLOR_GREEN, 400},
  {1, COLOR_GREEN, 400},
  {0, COLOR_RED, 400},
  {2, COLOR_BLUE, 400},
  {0, COLOR_RED, 400}
};

// Sequence 2: Fast repeating pattern
const SequenceStep seq2_steps[] = {
  {1, COLOR_GREEN, 500},
  {1, COLOR_GREEN, 500},
  {0, COLOR_RED, 500},
  {2, COLOR_BLUE, 500},
  {2, COLOR_BLUE, 500},
  {1, COLOR_GREEN, 500}
};

// Sequence 3: Sweep — cycles through RGB keys with a slow-fast-slow arc
const SequenceStep seq3_steps[] = {
  {0, COLOR_RED,     600},
  {1, COLOR_GREEN,  500},
  {2, COLOR_BLUE,  400},
  {0, COLOR_RED,   300},
  {1, COLOR_GREEN,    300},
  {2, COLOR_BLUE,  300},
  {0, COLOR_RED,  300},
  {1, COLOR_GREEN,     300},
  {2, COLOR_BLUE,  300},
  {0, COLOR_RED,  300},
  {1, COLOR_GREEN,   400},
  {2, COLOR_BLUE,    500},
  {0, COLOR_RED,  600},
  {1, COLOR_GREEN,  700},
};

// Sequence 4: Syncopated — irregular rhythm across all keys
const SequenceStep seq4_steps[] = {
  {0, COLOR_RED,  300},
  {2, COLOR_BLUE,  300},
  {1, COLOR_GREEN,    600},
  {0, COLOR_RED,  300},
  {2, COLOR_BLUE,  300},
  {1, COLOR_GREEN,     300},
  {0, COLOR_RED,   300},
  {1, COLOR_GREEN,  300},
  {2, COLOR_BLUE,     300},
  {0, COLOR_RED,  500},
  {1, COLOR_GREEN,  300},
  {2, COLOR_BLUE,  300},
  {0, COLOR_RED,    400},
  {1, COLOR_GREEN,   300},
  {2, COLOR_BLUE,  300},
  {0, COLOR_RED,     700},
};

// Sequence 5: "Ode to Joy" by Beethoven (adapted for C-E-G)
//   Original melody: E E F G | G F E D | C C D E | E D D
//   Adapted (no D or F, nearest substitution):
//     E E G G | G G E E | C C E E | E C C
//   Notes: Key0=G4(392), Key1=E4(330), Key2=C4(262)
const SequenceStep seq5_steps[] = {
  // Phrase 1: E E G G | G G E E
  {1, COLOR_YELLOW,  400},   // E
  {1, COLOR_YELLOW,  400},   // E
  {0, COLOR_ORANGE,  400},   // G (sub for F)
  {0, COLOR_RED,     400},   // G
  {0, COLOR_RED,     400},   // G
  {0, COLOR_ORANGE,  400},   // G (sub for F)
  {1, COLOR_YELLOW,  400},   // E
  {1, COLOR_GREEN,   400},   // E (sub for D)
  // Phrase 2: C C E E | E C C
  {2, COLOR_BLUE,    400},   // C
  {2, COLOR_BLUE,    400},   // C
  {1, COLOR_GREEN,   400},   // E (sub for D)
  {1, COLOR_YELLOW,  400},   // E
  {1, COLOR_YELLOW,  600},   // E (held longer — resolution)
  {2, COLOR_INDIGO,  400},   // C (sub for D)
  {2, COLOR_VIOLET,  800},   // C (final, held long)
};

// Sequence 6: Lullaby — gentle C major arpeggio, ascending and descending
//   A soothing, dreamy pattern that sounds like a music box
const SequenceStep seq6_steps[] = {
  {2, COLOR_BLUE,    500},   // C  — root
  {1, COLOR_GREEN,   500},   // E  — third
  {0, COLOR_RED,     500},   // G  — fifth
  {1, COLOR_YELLOW,  500},   // E  — descend
  {2, COLOR_BLUE,    500},   // C  — root
  {1, COLOR_GREEN,   500},   // E  — third
  {0, COLOR_RED,     700},   // G  — linger on top
  {0, COLOR_ORANGE,  300},   // G  — gentle repeat
  {1, COLOR_GREEN,   500},   // E  — come back down
  {2, COLOR_INDIGO,  500},   // C  — root
  {1, COLOR_YELLOW,  400},   // E  — one more rise
  {0, COLOR_RED,     600},   // G  — peak
  {1, COLOR_GREEN,   500},   // E  — descend
  {2, COLOR_VIOLET,  900},   // C  — resolve and rest
};

// Sequence 7: "Mary Had a Little Lamb" (adapted for C-E-G)
//   Original: E D C D | E E E | D D D | E G G
//   Adapted:  E C C C | E E E | C C C | E G G  (D → C substitution)
//   Notes: Key0=G4(392), Key1=E4(330), Key2=C4(262)
const SequenceStep seq7_steps[] = {
  // "Ma-ry had a"
  {1, COLOR_YELLOW,  400},   // E  — Ma
  {2, COLOR_BLUE,    400},   // C  — ry  (sub for D)
  {2, COLOR_INDIGO,  400},   // C  — had
  {2, COLOR_BLUE,    400},   // C  — a   (sub for D)
  // "lit-tle lamb"
  {1, COLOR_GREEN,   400},   // E  — lit
  {1, COLOR_YELLOW,  400},   // E  — tle
  {1, COLOR_GREEN,   800},   // E  — lamb (held)
  // "lit-tle lamb"
  {2, COLOR_BLUE,    400},   // C  — lit  (sub for D)
  {2, COLOR_INDIGO,  400},   // C  — tle  (sub for D)
  {2, COLOR_VIOLET,  800},   // C  — lamb (held, sub for D)
  // "lit-tle lamb"
  {1, COLOR_YELLOW,  400},   // E  — lit
  {0, COLOR_RED,     400},   // G  — tle
  {0, COLOR_ORANGE,  800},   // G  — lamb (held)
};

// ============ SEQUENCE ARRAY ============
// This array holds all available sequences.
// Format: {pointer_to_steps, number_of_steps, "display_name"}

const Sequence sequences[NUM_SEQUENCES] = {
  {seq0_steps, sizeof(seq0_steps) / sizeof(SequenceStep), "Ping Pong"},
  {seq1_steps, sizeof(seq1_steps) / sizeof(SequenceStep), "Up & Down"},
  {seq2_steps, sizeof(seq2_steps) / sizeof(SequenceStep), "Quick Repeat"},
  {seq3_steps, sizeof(seq3_steps) / sizeof(SequenceStep), "Sweep"},
  {seq4_steps, sizeof(seq4_steps) / sizeof(SequenceStep), "Syncopated"},
  {seq5_steps, sizeof(seq5_steps) / sizeof(SequenceStep), "Ode to Joy"},
  {seq6_steps, sizeof(seq6_steps) / sizeof(SequenceStep), "Lullaby"},
  {seq7_steps, sizeof(seq7_steps) / sizeof(SequenceStep), "Mary Had a Lamb"}
};

#endif
