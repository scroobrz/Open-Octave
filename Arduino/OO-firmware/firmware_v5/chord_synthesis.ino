// Chord synthesis.ino -- this 
 
#define SINE_TABLE_SIZE 1024
float sineTable[SINE_TABLE_SIZE];

// Call once in noteSetup()
void buildSineTable() {
  for (int i = 0; i < SINE_TABLE_SIZE; i++) {
    sineTable[i] = sinf(2.0f * M_PI * i / SINE_TABLE_SIZE);
  }
}

void noteSetup() {
   
  // Configure I2S Driver
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, // Stereo output
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 16,
    .dma_buf_len = 256,
    .use_apll = false
  };
   
  // Configure I2S Pins
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE // Not using input here
  };
 
  // Install and Start
  i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM, &pin_config);

  buildSineTable();
  // Flush garbage from DMA buffers
  i2s_zero_dma_buffer(I2S_NUM);

}

// --- Phase accumulators (one per key) ---
// Keeps track of where we are in each sine wave across loop calls,
// preventing clicks/pops at buffer boundaries
float phaseAccumulators[MAX_TOTAL_KEYS] = {0};

void playPressedKeys() {
  // --- 1. Collect pressed keys ---
  int pressedIndices[MAX_TOTAL_KEYS];
  int pressedCount = 0;

  for (int i = 0; i < NUM_KEYS; i++) {
    if (keys[i].isPressed && keys[i].noteFreq > 0) {
      pressedIndices[pressedCount++] = i;
    }
  }

  // Nothing pressed — write silence and return
  if (pressedCount == 0) {
    int16_t silenceBuf[DMA_BUF_LEN * 2];
    memset(silenceBuf, 0, sizeof(silenceBuf));  // Explicit zero-fill
    size_t bytesWritten;
    i2s_write(I2S_NUM, silenceBuf, sizeof(silenceBuf), &bytesWritten, portMAX_DELAY);
    return;
  }

  // --- 2. Generate one buffer of mixed audio ---
  int16_t sampleBuf[DMA_BUF_LEN * 2]; // *2 for stereo (L + R interleaved)

  // Scale volume down by number of voices to prevent clipping
  float voiceVolume = VOLUME / pressedCount;

  for (int s = 0; s < DMA_BUF_LEN; s++) {
    float mixedSample = 0.0f;

    for (int v = 0; v < pressedCount; v++) {
      if (v == 0){ 
        continue;
      }
      int keyIdx = pressedIndices[v];
      float freq  = (float)keys[keyIdx].noteFreq;

      // Advance phase and generate sine sample
      // Then in playPressedKeys(), replace the phase accumulator logic with:
      phaseAccumulators[keyIdx] += (float)SINE_TABLE_SIZE * freq / SAMPLE_RATE;
      if (phaseAccumulators[keyIdx] >= SINE_TABLE_SIZE)
        phaseAccumulators[keyIdx] -= SINE_TABLE_SIZE;

      mixedSample += sineTable[(int)phaseAccumulators[keyIdx]];
    }

    // Scale to 16-bit integer range
    int16_t pcmSample = (int16_t)(mixedSample * voiceVolume * 32767.0f);

    // Write same sample to both L and R channels (stereo interleaved)
    sampleBuf[s * 2]     = pcmSample; // Left
    sampleBuf[s * 2 + 1] = pcmSample; // Right
  }

  // --- 3. Send buffer to I2S DMA ---
  size_t bytesWritten;
  i2s_write(I2S_NUM, sampleBuf, sizeof(sampleBuf), &bytesWritten, portMAX_DELAY);
}
