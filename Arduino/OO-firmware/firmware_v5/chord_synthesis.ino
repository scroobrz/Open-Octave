/**
 * chord_synth.ino
 *
 * Decodes multiple MP3 files (e.g. c4.mp3, d4.mp3, e4.mp3), superimposes
 * them into a mixed PCM stream, and feeds that stream directly to an I2S
 * amplifier module (e.g. MAX98357A, UDA1334A, PCM5102).
 *
 * No intermediate file is written. synthChord() returns a WavStream struct
 * whose samples pointer and sampleCount describe the heap buffer, which
 * loop() drains into AudioOutputI2S. Call freeWavStream() when done.
 *
 * Hardware assumed:
 *   - ESP32
 *   - I2S amplifier wired to the pins defined in I2S_* below
 *   - SD card module for source MP3 files
 *
 * I2S amplifier wiring (MAX98357A example):
 *   ESP32 GPIO25  →  BCLK  (bit clock)
 *   ESP32 GPIO26  →  LRC   (word select / left-right clock)
 *   ESP32 GPIO22  →  DIN   (serial data)
 *   3.3 V         →  VIN
 *   GND           →  GND
 *   SD pin        →  leave floating or tie HIGH to enable output
 *
 * Dependencies (Arduino Library Manager):
 *   - ESP8266Audio  by  earlephilhower
 *   - ESP32 board package
 */

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

#include "AudioFileSourceSD.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"
#include "AudioOutputNull.h"

// ── Pin / hardware configuration ─────────────────────────────────────────────
#define SD_CS_PIN       5    // SD card chip-select
#define I2S_BCLK_PIN   25    // I2S bit clock   → amp BCLK
#define I2S_LRCLK_PIN  26    // I2S word select  → amp LRC / WSEL
#define I2S_DOUT_PIN   22    // I2S data out     → amp DIN / SD

// ── Audio configuration ───────────────────────────────────────────────────────
#define MIX_SAMPLE_RATE  44100
#define MIX_CHANNELS         1   // 1 = mono, 2 = stereo
#define FRAME_SAMPLES     1024   // samples pushed to I2S per loop() call

// ── WavStream ─────────────────────────────────────────────────────────────────
/**
 * WavStream
 *
 * Returned by synthChord(). On success:
 *   samples     → heap-allocated int16_t PCM buffer (caller must free via freeWavStream)
 *   sampleCount → total number of int16 samples
 *   sampleRate  → playback rate in Hz
 *   channels    → 1 (mono) or 2 (stereo)
 *   errorCode   → 0
 *
 * On failure:
 *   samples     → nullptr
 *   errorCode   → non-zero
 *   errorMsg    → human-readable description
 */
struct WavStream {
  int16_t*    samples;
  size_t      sampleCount;
  uint32_t    sampleRate;
  uint8_t     channels;
  int         errorCode;
  const char* errorMsg;
};

// ── Module-level playback state ───────────────────────────────────────────────
static AudioOutputI2S* i2sOut   = nullptr;
static WavStream        g_stream = {};
static size_t           g_cursor = 0;   // current playback position (samples)

// ── Forward declarations ──────────────────────────────────────────────────────
WavStream   synthChord(const char** mp3Paths, uint8_t fileCount);
void        freeWavStream(WavStream& ws);
static bool decodeMp3ToPcm(const char* path, int16_t** outBuf, size_t* outLen);
static void mixBuffers(int16_t** bufs, size_t* lens,
                       uint8_t count, int16_t* mixBuf, size_t mixLen);

// ── setup() ──────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("[ERROR] SD init failed");
    return;
  }
  Serial.println("[INFO]  SD ready");

  // Initialise I2S output to amplifier
  i2sOut = new AudioOutputI2S();
  i2sOut->SetPinout(I2S_BCLK_PIN, I2S_LRCLK_PIN, I2S_DOUT_PIN);
  i2sOut->SetRate(MIX_SAMPLE_RATE);
  i2sOut->SetChannels(MIX_CHANNELS);
  i2sOut->SetBitsPerSample(16);
  i2sOut->begin();

  // ── Synthesise a C-major chord (C4 + E4 + G4) ────────────────────────────
  const char* notes[]   = { "/c4.mp3", "/e4.mp3", "/g4.mp3" };
  uint8_t     noteCount = sizeof(notes) / sizeof(notes[0]);

  g_stream = synthChord(notes, noteCount);
  g_cursor = 0;

  if (g_stream.errorCode != 0) {
    Serial.print("[ERROR] ");
    Serial.print(g_stream.errorCode);
    Serial.print(" — ");
    Serial.println(g_stream.errorMsg);
  } else {
    Serial.print("[OK]    Streaming ");
    Serial.print(g_stream.sampleCount);
    Serial.println(" samples → I2S amplifier");
  }
}

// ── loop() ───────────────────────────────────────────────────────────────────
/**
 * Drains the WavStream into the I2S peripheral FRAME_SAMPLES at a time.
 *
 * For mono streams the same sample is copied to both the left and right
 * I2S slots (AudioOutputI2S always expects a stereo L/R pair per call).
 * For stereo streams the buffer is already interleaved [L0,R0,L1,R1,...].
 *
 * To loop playback instead of stopping, reset g_cursor = 0 rather than
 * calling freeWavStream().
 */
void loop() {
  if (!i2sOut || g_stream.samples == nullptr) {
    delay(100);
    return;
  }

  if (g_cursor >= g_stream.sampleCount) {
    freeWavStream(g_stream);
    i2sOut->stop();
    Serial.println("[INFO]  Playback complete");
    delay(1000);
    return;
  }

  size_t remaining = g_stream.sampleCount - g_cursor;
  size_t chunkLen  = min((size_t)FRAME_SAMPLES, remaining);

  for (size_t i = 0; i < chunkLen; ) {
    if (g_stream.channels == 2) {
      // Stereo: buffer is interleaved [L, R, L, R, ...]
      int16_t lr[2] = { g_stream.samples[g_cursor + i],
                        g_stream.samples[g_cursor + i + 1] };
      i2sOut->ConsumeSample(lr);
      i += 2;
    } else {
      // Mono: duplicate sample to both channels
      int16_t s = g_stream.samples[g_cursor + i];
      int16_t lr[2] = { s, s };
      i2sOut->ConsumeSample(lr);
      i += 1;
    }
  }

  g_cursor += chunkLen;
}

// ── synthChord() ─────────────────────────────────────────────────────────────
/**
 * synthChord()
 *
 * @param mp3Paths   Array of null-terminated SD paths (e.g. "/c4.mp3")
 * @param fileCount  Number of entries in mp3Paths
 * @returns          WavStream with heap-allocated mixed PCM on success.
 *
 * Pipeline:
 *   1. Decode each MP3 → individual int16 heap buffer via AudioGeneratorMP3
 *   2. Find the longest decoded length (shorter files → silence padding)
 *   3. Average-mix all buffers into one output buffer (clamps to int16 range)
 *   4. Free intermediate per-file buffers
 *   5. Return WavStream — the caller owns the buffer and must call freeWavStream()
 */
WavStream synthChord(const char** mp3Paths, uint8_t fileCount) {
  WavStream ws = { nullptr, 0, MIX_SAMPLE_RATE, MIX_CHANNELS, 0, nullptr };

  if (!mp3Paths || fileCount == 0) {
    ws.errorCode = 1;
    ws.errorMsg  = "No input files provided";
    return ws;
  }

  // ── 1. Decode ─────────────────────────────────────────────────────────────
  int16_t** pcmBufs = (int16_t**)calloc(fileCount, sizeof(int16_t*));
  size_t*   pcmLens = (size_t*)  calloc(fileCount, sizeof(size_t));

  if (!pcmBufs || !pcmLens) {
    free(pcmBufs); free(pcmLens);
    ws.errorCode = 2;
    ws.errorMsg  = "Out of memory (decode array)";
    return ws;
  }

  size_t maxLen = 0;
  for (uint8_t i = 0; i < fileCount; i++) {
    Serial.print("[INFO]  Decoding ");
    Serial.println(mp3Paths[i]);

    if (!decodeMp3ToPcm(mp3Paths[i], &pcmBufs[i], &pcmLens[i])) {
      Serial.print("[WARN]  Skipping (decode failed): ");
      Serial.println(mp3Paths[i]);
    }
    if (pcmLens[i] > maxLen) maxLen = pcmLens[i];
  }

  if (maxLen == 0) {
    for (uint8_t i = 0; i < fileCount; i++) free(pcmBufs[i]);
    free(pcmBufs); free(pcmLens);
    ws.errorCode = 3;
    ws.errorMsg  = "All files failed to decode";
    return ws;
  }

  // ── 2. Allocate mix buffer ────────────────────────────────────────────────
  int16_t* mixBuf = (int16_t*)calloc(maxLen, sizeof(int16_t));
  if (!mixBuf) {
    for (uint8_t i = 0; i < fileCount; i++) free(pcmBufs[i]);
    free(pcmBufs); free(pcmLens);
    ws.errorCode = 4;
    ws.errorMsg  = "Out of memory (mix buffer)";
    return ws;
  }

  // ── 3. Mix ────────────────────────────────────────────────────────────────
  mixBuffers(pcmBufs, pcmLens, fileCount, mixBuf, maxLen);

  // ── 4. Free per-file buffers ──────────────────────────────────────────────
  for (uint8_t i = 0; i < fileCount; i++) free(pcmBufs[i]);
  free(pcmBufs);
  free(pcmLens);

  // ── 5. Hand back the stream ───────────────────────────────────────────────
  ws.samples     = mixBuf;
  ws.sampleCount = maxLen;
  return ws;
}

// ── freeWavStream() ───────────────────────────────────────────────────────────
/**
 * Releases the heap buffer owned by a WavStream and zeroes the struct.
 * Safe to call on an already-freed or error-state stream.
 */
void freeWavStream(WavStream& ws) {
  if (ws.samples) {
    free(ws.samples);
    ws.samples     = nullptr;
    ws.sampleCount = 0;
  }
}

// ── decodeMp3ToPcm() ─────────────────────────────────────────────────────────
/**
 * Decodes one SD MP3 file into a heap-allocated int16 PCM buffer.
 * AudioOutputNull is used as a silent sink so that AudioGeneratorMP3
 * drives the decode loop without touching the I2S peripheral.
 * The caller is responsible for free()-ing *outBuf.
 */
static bool decodeMp3ToPcm(const char* path, int16_t** outBuf, size_t* outLen) {
  *outBuf = nullptr;
  *outLen = 0;

  AudioFileSourceSD* src = new AudioFileSourceSD(path);
  if (!src->isOpen()) { delete src; return false; }

  AudioOutputNull* sink = new AudioOutputNull();
  sink->SetRate(MIX_SAMPLE_RATE);
  sink->SetChannels(MIX_CHANNELS);
  sink->SetBitsPerSample(16);

  AudioGeneratorMP3* mp3 = new AudioGeneratorMP3();
  mp3->begin(src, sink);

  size_t   capacity = FRAME_SAMPLES * 64;
  int16_t* buf      = (int16_t*)malloc(capacity * sizeof(int16_t));
  size_t   filled   = 0;

  if (!buf) {
    mp3->stop();
    delete mp3; delete sink; delete src;
    return false;
  }

  int16_t frameBuf[FRAME_SAMPLES * MIX_CHANNELS];

  while (mp3->isRunning()) {
    if (!mp3->loop()) break;

    int got = sink->consumeSamples(frameBuf, FRAME_SAMPLES * MIX_CHANNELS);
    if (got <= 0) continue;

    if (filled + got > capacity) {
      capacity = (filled + got) * 2;
      int16_t* tmp = (int16_t*)realloc(buf, capacity * sizeof(int16_t));
      if (!tmp) { free(buf); buf = nullptr; break; }
      buf = tmp;
    }
    memcpy(buf + filled, frameBuf, got * sizeof(int16_t));
    filled += got;
  }

  mp3->stop();
  delete mp3; delete sink; delete src;

  if (!buf || filled == 0) { free(buf); return false; }
  *outBuf = buf;
  *outLen = filled;
  return true;
}

// ── mixBuffers() ─────────────────────────────────────────────────────────────
/**
 * Equal-weight average mix into a pre-allocated output buffer of length mixLen.
 * Buffers shorter than mixLen are treated as silence beyond their end.
 * Output is clamped to int16 range to prevent wrap-around distortion.
 */
static void mixBuffers(int16_t** bufs, size_t* lens,
                        uint8_t count, int16_t* mixBuf, size_t mixLen) {
  memset(mixBuf, 0, mixLen * sizeof(int16_t));
  for (size_t s = 0; s < mixLen; s++) {
    int32_t sum = 0;
    for (uint8_t c = 0; c < count; c++) {
      sum += (s < lens[c]) ? (int32_t)bufs[c][s] : 0;
    }
    int32_t avg = sum / count;
    if      (avg >  32767) avg =  32767;
    else if (avg < -32768) avg = -32768;
    mixBuf[s] = (int16_t)avg;
  }
}
