/**
 * playback.ino
 *
 * Playback for constructed WavStream onto DFRobot_MAX98357A amplifier module
 *
 *  DFRobot_MAX98367A amplifier wiring:
 *   ESP32 GPIO25  →  BCLK
 *   ESP32 GPIO26  →  LRC
 *   ESP32 GPIO22  →  DIN
 *   3.3V          →  VIN
 *   GND           →  GND
 */

static i2s_chan_handle_t i2s_tx_handle = NULL;

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

void playChord() {
  // ── 1. Collect pressed keys ───────────────────────────────────────────────
  WavStream** streams = (WavStream**)alloca(NUM_KEYS * sizeof(WavStream*));
  uint8_t count = 0;

  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    if (keys[i].isPressed && keys[i].pcm != nullptr) {
      streams[count++] = keys[i].pcm;
    }
  }

  if (count == 0) {
    LOGLN("[WARN]  playChord: no keys pressed or no buffers loaded");
    return;
  }

  // ── 2. Build flat pcm/len arrays for mixBuffers ───────────────────────────
  int16_t** pcmBufs = (int16_t**)alloca(count * sizeof(int16_t*));
  size_t*   pcmLens = (size_t*)  alloca(count * sizeof(size_t));
  size_t    maxLen  = 0;

  for (uint8_t i = 0; i < count; i++) {
    pcmBufs[i] = streams[i]->samples;
    pcmLens[i] = streams[i]->sampleCount;
    if (pcmLens[i] > maxLen) maxLen = pcmLens[i];
  }

  // ── 3. Allocate mix buffer ────────────────────────────────────────────────
  int16_t* mixBuf = (int16_t*)calloc(maxLen, sizeof(int16_t));
  if (!mixBuf) {
    LOGLN("[ERROR] playChord: out of memory (mix buffer)");
    return;
  }

  // ── 4. Mix ────────────────────────────────────────────────────────────────
  mixBuf(pcmBufs, pcmLens, count, mixBuf, maxLen);

  // ── 5. Wrap in a WavStream and stream to amp ──────────────────────────────
  WavStream mixed = {
    mixBuf,
    maxLen,
    0,
    MIX_SAMPLE_RATE,
    MIX_CHANNELS,
    0,
    nullptr
  };

  streamToAmp(mixed);

  // ── 6. Free the mix buffer (streamToAmp is done with it) ──────────────────
  free(mixBuf);
}

// ── ampSetup() ───────────────────────────────────────────────────────────────
void ampSetup() {
  if (!SD.begin(SD_CS_PIN)) {
    LOGLN("[ERROR] DFR0229 SD card is uninitialised");
    return;
  }

  // Configure I2S channel
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &i2s_tx_handle, NULL));

  i2s_std_config_t std_cfg = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(MIX_SAMPLE_RATE),
    .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)AMP_BCLK_PIN,
      .ws   = (gpio_num_t)AMP_LRCLK_PIN,
      .dout = (gpio_num_t)AMP_DIN_PIN,
      .din  = I2S_GPIO_UNUSED,
      .invert_flags = {
        .mclk_inv = false,
        .bclk_inv = false,
        .ws_inv   = false,
      },
    },
  };

  ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2s_tx_handle, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(i2s_tx_handle));

  LOGLN("[SETUP]  MAX98357A ready");
}

// ── streamToAmp() ────────────────────────────────────────────────────────────
void streamToAmp(WavStream& ws) {
  if (ws.samples == nullptr) return;

  if (ws.cursor >= ws.sampleCount) {
    freeWavStream(&ws);
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

  esp_err_t err = i2s_channel_write(
    i2s_tx_handle,
    (const char*)stereoBuf,
    bytesToWrite,
    &bytesWritten,
    pdMS_TO_TICKS(I2S_WRITE_TIMEOUT_MS)
  );

  if (err != ESP_OK) {
    LOG("[WARN]  i2s_channel_write error: ");
    LOGLN(esp_err_to_name(err));
  }

  ws.cursor += chunkSamples;
}

void handlePlayback(){
  // This looks at the global is_pressed array then calls streamToAmp() for active keys
  playChord();
}