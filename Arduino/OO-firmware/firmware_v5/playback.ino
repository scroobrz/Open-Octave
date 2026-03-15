/**
 * playback.ino
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

// ── ampSetup() ──────────────────────────────────────────────────────────────────
void ampSetup() {
  // SD card (DFR0229)
  if (!initSD()){
    LOGLN("[ERROR] DFR0229 SD card cannot initialise");
    return;
  } 

  // Initialise the MAX98357A via DFRobot library (configures I2S peripheral)
  while (!amplifier.initI2S(AMP_BCLK_PIN, AMP_LRCLK_PIN, AMP_DIN_PIN)) {
    LOGLN("[ERROR] MAX98357A init failed, retrying...");
    delay(1000); 
  }
  LOGLN("[SETUP]  MAX98357A ready");

  // Re-configure I2S port to match our sample rate / bit depth.
  // The DFRobot library sets up I2S_NUM_0 internally; we reconfigure
  // it here to match MIX_SAMPLE_RATE before pushing our own PCM data.
  i2s_set_clk(I2S_PORT, MIX_SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
}

// ── streamToAmp() ───────────────────────────────────────────────────────────────────
/**
 * Drains the WavStream into the I2S peripheral FRAME_SAMPLES at a time.
 *
 * For mono streams the same sample is copied to both the left and right
 * I2S slots (AudioOutputI2S always expects a stereo L/R pair per call).
 * For stereo streams the buffer is already interleaved [L0,R0,L1,R1,...].
 *
 */
void streamToAmp(WavStream& ws) {
  if (ws.samples == nullptr) return;

  if (ws.cursor >= ws.sampleCount) {
    freeWavStream(ws);
    return;
  }

  size_t remaining    = ws.sampleCount - ws.cursor;
  size_t chunkSamples = min((size_t)FRAME_SAMPLES, remaining);

  int16_t stereoBuf[FRAME_SAMPLES * 2];
  for (size_t i = 0; i < chunkSamples; i++) {
    int16_t s = ws.samples[ws.cursor + i];
    stereoBuf[i * 2]     = s;  // Left
    stereoBuf[i * 2 + 1] = s;  // Right
  }

  size_t bytesToWrite = chunkSamples * 2 * sizeof(int16_t);
  size_t bytesWritten = 0;

  esp_err_t err = i2s_write(
    I2S_PORT,
    (const char*)stereoBuf,
    bytesToWrite,
    &bytesWritten,
    pdMS_TO_TICKS(I2S_WRITE_TIMEOUT_MS)
  );

  if (err != ESP_OK) {
    LOG("[WARN]  i2s_write error: ");
    LOGLN(esp_err_to_name(err));
  }

  ws.cursor += chunkSamples;
}