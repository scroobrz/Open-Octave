// Chord synthesis.ino
// Audio runs on its own FreeRTOS task so the blocking i2s_write() call
// doesn't stall the main loop (WiFi, sequences, button scanning, etc.).

#define AUDIO_TASK_STACK_SIZE 4096
#define AUDIO_TASK_PRIORITY 2  // Above loop()'s default priority of 1
#define AUDIO_TASK_CORE 1      // Same core as loop(); higher priority preempts it

#define SINE_TABLE_SIZE 1024
#define NUM_HARMONICS 5

const float HARMONICS[NUM_HARMONICS][2] = {
  {1.0f, 1.0f},
  {2.0f, 0.5f},
  {3.0f, 0.25f},
  {4.0f, 0.125f},
  {5.0f, 0.0625f}
};

#define DECAY_PER_SAMPLE 0.9999f
#define ATTACK_PER_SAMPLE 0.001f  // Ramp up over ~1000 samples to avoid click on press

float sineTable[SINE_TABLE_SIZE];
float phaseAccumulators[MAX_TOTAL_KEYS][NUM_HARMONICS] = {0};

#define KS_MAX_DELAY 200
float ksDelayLines[MAX_TOTAL_KEYS][KS_MAX_DELAY] = {0};
int ksDelayPointers[MAX_TOTAL_KEYS] = {0};

struct Envelope {
  float value;
  bool wasPressed;  // Tracks previous state to detect fresh keypresses
};
Envelope envelopes[MAX_TOTAL_KEYS] = {0};

static int16_t sampleBuf[DMA_BUF_LEN * 2];
static int16_t silenceBuf[DMA_BUF_LEN * 2];

void buildSineTable() {
  for (int i = 0; i < SINE_TABLE_SIZE; i++) {
    sineTable[i] = sinf(2.0f * M_PI * i / SINE_TABLE_SIZE);
  }
}

void noteSetup() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 16,
    .dma_buf_len = 256,
    .use_apll = false
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM, &pin_config);

  buildSineTable();
  memset(silenceBuf, 0, sizeof(silenceBuf));
  i2s_zero_dma_buffer(I2S_NUM);

  // Initialise all envelopes to safe defaults
  for (int i = 0; i < MAX_TOTAL_KEYS; i++) {
    envelopes[i].value = 0.0f;
    envelopes[i].wasPressed = false;
  }

  // Launch the audio task on its own FreeRTOS task.
  // Priority 2 lets it preempt loop() (priority 1) to fill DMA buffers
  // on time, while loop() still gets all remaining CPU for WiFi, sequences, etc.
  xTaskCreatePinnedToCore(
    audioTask,              // task function
    "audio",                // name (for debugging)
    AUDIO_TASK_STACK_SIZE,  // stack size in bytes
    NULL,                   // parameter
    AUDIO_TASK_PRIORITY,    // priority (2 > loop's 1)
    NULL,                   // task handle (not needed)
    AUDIO_TASK_CORE         // core 1 (same as loop)
  );
}

// FreeRTOS task that continuously fills the I2S DMA buffer.
// Runs independently of loop() so WiFi/sequence/button handling
// never starves the audio, and audio never blocks those systems.
void audioTask(void *param) {
  (void)param;
  for (;;) {
    playPressedKeys();
  }
}

void playPressedKeys() {
  // --- Snapshot key states and frequencies atomically at the start ---
  // This function runs on a separate task from handleKeyPresses() and
  // configureNotes(), so we snapshot both isPressed and noteFreq to
  // avoid reading partially-updated state mid-buffer.
  bool pressedSnapshot[NUM_KEYS];
  int freqSnapshot[NUM_KEYS];
  for (int i = 0; i < NUM_KEYS; i++) {
    freqSnapshot[i] = keys[i].noteFreq;
    pressedSnapshot[i] = keys[i].isPressed && freqSnapshot[i] > 0;
  }

  // --- 1. Detect fresh keypresses ---
  for (int i = 0; i < NUM_KEYS; i++) {
    // Detect fresh keypress from snapshot — reset envelope
    if (pressedSnapshot[i] && !envelopes[i].wasPressed) {
      envelopes[i].value = 0.0f;
      
      if (currentSynthMode == SYNTH_KARPLUS_STRONG) {
        // Initialize Karplus-Strong buffer with a shaped excitation
        int delayLen = (int)(SAMPLE_RATE / (float)freqSnapshot[i]);
        if (delayLen > KS_MAX_DELAY) delayLen = KS_MAX_DELAY;
        if (delayLen < 1) delayLen = 1;
        
        float prevNoise = 0.0f;
        for(int j = 0; j < delayLen; j++) {
           float phase = (float)j / (float)delayLen; // 0.0 to 1.0
           
           // Generate a Triangle wave for acoustic warmth (fundamental body)
           float tri = (phase < 0.5f) ? (4.0f * phase - 1.0f) : (3.0f - 4.0f * phase);
           
           // Generate a Sawtooth wave for the "clavinet buzz" overtones
           float saw = 2.0f * phase - 1.0f;

           // Generate soft filtered noise for the mechanical hammer strike
           float noise = ((float)random(20000) / 10000.0f) - 1.0f;
           float hammerNoise = 0.3f * noise + 0.7f * prevNoise;
           prevNoise = hammerNoise;
           
           // Mix them together: 60% Triangle, 20% Sawtooth, 20% Hammer Noise
           ksDelayLines[i][j] = (tri * 0.6f) + (saw * 0.2f) + (hammerNoise * 0.2f);
        }
        ksDelayPointers[i] = 0;
      }
    }
    // Update wasPressed from snapshot — consistent with what we just used
    envelopes[i].wasPressed = pressedSnapshot[i];
  }

  // --- 2. Collect active keys (pressed or still releasing) ---
  int activeIndices[NUM_KEYS];
  int activeCount = 0;

  for (int i = 0; i < NUM_KEYS; i++) {
    // Include keys that are currently pressed or have a decaying envelope
    if (pressedSnapshot[i] || envelopes[i].value > 0.001f) {
      activeIndices[activeCount++] = i;
    }
  }

  // Nothing audible — write silence and return
  if (activeCount == 0) {
    size_t bytesWritten;
    i2s_write(I2S_NUM, silenceBuf, sizeof(silenceBuf), &bytesWritten, portMAX_DELAY);
    return;
  }

  // --- 3. Generate one buffer of mixed audio ---
  float voiceVolume = 0.0f;
  float maxPolyphony = 4.0f; // Typical max simultaneous keys per step
  
  if (currentSynthMode == SYNTH_ADDITIVE) {
    float totalHarmonicWeight = 0.0f;
    for (int h = 0; h < NUM_HARMONICS; h++) totalHarmonicWeight += HARMONICS[h][1];
    voiceVolume = VOLUME / (maxPolyphony * totalHarmonicWeight);
  } else {
    // We boost the volume slightly to compensate for the soft hammer, 
    // but limit the multiplier to 1.25x to ensure that 4-note chords do not 
    // exceed the maximum digital amplitude and cause hard-clipping distortion.
    voiceVolume = (VOLUME * 1.25f) / maxPolyphony;
  }

  for (int s = 0; s < DMA_BUF_LEN; s++) {
    // --- Advance envelopes per-sample ---
    for (int i = 0; i < activeCount; i++) {
      int keyIdx = activeIndices[i];
      if (pressedSnapshot[keyIdx]) {
        if (currentSynthMode == SYNTH_KARPLUS_STRONG) {
          // Fast attack (takes ~200 samples / 5ms to reach 1.0) to remove the hard click 
          // of an instant start, mimicking a felt hammer pushing the string.
          envelopes[keyIdx].value = min(1.0f, envelopes[keyIdx].value + 0.005f);
        } else {
          envelopes[keyIdx].value = min(1.0f, envelopes[keyIdx].value + ATTACK_PER_SAMPLE);
        }
      } else if (envelopes[keyIdx].value > 0.0f) {
        envelopes[keyIdx].value *= DECAY_PER_SAMPLE;
        if (envelopes[keyIdx].value < 0.001f) envelopes[keyIdx].value = 0.0f;
      }
    }

    float mixedSample = 0.0f;

    for (int v = 0; v < activeCount; v++) {
      int keyIdx = activeIndices[v];
      float freq = (float)freqSnapshot[keyIdx];
      float noteSample = 0.0f;

      if (currentSynthMode == SYNTH_ADDITIVE) {
        for (int h = 0; h < NUM_HARMONICS; h++) {
          float harmonicFreq = freq * HARMONICS[h][0];
          if (harmonicFreq >= SAMPLE_RATE / 2) continue;

          phaseAccumulators[keyIdx][h] += (float)SINE_TABLE_SIZE * harmonicFreq / SAMPLE_RATE;
          if (phaseAccumulators[keyIdx][h] >= SINE_TABLE_SIZE)
            phaseAccumulators[keyIdx][h] -= SINE_TABLE_SIZE;

          noteSample += sineTable[(int)phaseAccumulators[keyIdx][h]] * HARMONICS[h][1];
        }
      } else if (currentSynthMode == SYNTH_KARPLUS_STRONG) {
        int delayLen = (int)(SAMPLE_RATE / freq);
        if (delayLen > KS_MAX_DELAY) delayLen = KS_MAX_DELAY;
        if (delayLen < 1) delayLen = 1;
        
        int p = ksDelayPointers[keyIdx];
        float currentVal = ksDelayLines[keyIdx][p];
        int prevP = p - 1;
        if (prevP < 0) prevP = delayLen - 1;
        float prevVal = ksDelayLines[keyIdx][prevP];
        
        // Low pass filter + longer sustain for piano strings (99.8% retention instead of 99.5%)
        float newVal = 0.998f * 0.5f * (currentVal + prevVal);
        
        ksDelayLines[keyIdx][p] = newVal;
        ksDelayPointers[keyIdx] = (p + 1) % delayLen;
        
        noteSample = newVal;
      }

      noteSample *= envelopes[keyIdx].value;
      mixedSample += noteSample;
    }

    // Calculate final PCM value and hard-clip to prevent integer overflow wrapping
    float pcmFloat = mixedSample * voiceVolume * 32767.0f;
    if (pcmFloat > 32767.0f) pcmFloat = 32767.0f;
    if (pcmFloat < -32768.0f) pcmFloat = -32768.0f;
    
    int16_t pcmSample = (int16_t)pcmFloat;
    sampleBuf[s * 2]     = pcmSample;
    sampleBuf[s * 2 + 1] = pcmSample;
  }

  // --- 4. Send buffer to I2S DMA ---
  size_t bytesWritten;
  i2s_write(I2S_NUM, sampleBuf, sizeof(sampleBuf), &bytesWritten, portMAX_DELAY);
}