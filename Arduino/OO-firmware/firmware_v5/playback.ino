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

void handlePlayback(){
  // This looks at the global is_pressed array then calls streamToAmp() for active keys
  playChord();
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