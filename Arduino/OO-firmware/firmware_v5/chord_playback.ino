/**
 * chord_playback.ino
 *
 * Playback for constructed WavStream onto DFRobot_MAX98357A amplifier module
 *
 *
 *   NEED TO CHECK/ CHANGE THIS WITH ELECTRONICS TESTING
 *  DFRobot_MAX98367A amplifier wiring:
 *   ESP32 GPIO25  →  BCLK  (bit clock)
 *   ESP32 GPIO26  →  LRC   (word select / left-right clock)
 *   ESP32 GPIO22  →  DIN   (serial data)
 *   3.3 V         →  VIN
 *   GND           →  GND
 *   SD pin        →  leave floating or tie HIGH to enable output
 *
 * Dependencies (Arduino Library Manager):
 *   - ESP8266Audio  by  earlephilhower
 */


using namespace std;

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
 * Returned by synthesiseChord(). On success:
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


// Rename this to describe the function better..? Also refactor because doesn't make any sense

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
void loop(WavStream ws) {
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