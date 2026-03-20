
void noteSetup(){
  for (int i = 0; i < NUM_KEYS; i++) {
    if (decodeMp3ToPcm(keys[i].fpath, &keys[i].pcm)){
      LOG("[INFO] CORRECT DECODE");
    } else {
      LOG("[ERROR] CANNOT DECODE");
    }
  }
}

// ── decodeMp3ToPcm() ─────────────────────────────────────────────────────────
/**
 * Decodes one SD MP3 file into a heap-allocated int16 PCM buffer.
 * AudioOutputNull is used as a silent sink so that AudioGeneratorMP3
 * drives the decode loop without touching the I2S peripheral.
 * The caller is responsible for free()-ing *outBuf.
 */
static bool decodeMp3ToPcm(const char* path, WavStream** wsOut) {
  // Allocate the WavStream the caller's pointer will refer to
  WavStream* ws = (WavStream*)malloc(sizeof(WavStream));
  if (!ws) return false;

  ws->samples     = nullptr;
  ws->sampleCount = 0;
  ws->cursor      = 0;
  ws->sampleRate  = MIX_SAMPLE_RATE;
  ws->channels    = MIX_CHANNELS;
  ws->errorCode   = 0;
  ws->errorMsg    = nullptr;

  // Open the file
  AudioFileSourceSD* src = new AudioFileSourceSD(path);
  if (!src->isOpen()) {
    delete src;
    ws->errorCode = 1;
    ws->errorMsg  = "File not found";
    *wsOut = ws;   // still give caller the struct so it can read errorCode
    return false;
  }

  // Use our capturing sink instead of AudioOutputNull
  AudioOutputBuffer* sink = new AudioOutputBuffer();
  sink->SetRate(MIX_SAMPLE_RATE);
  sink->SetChannels(MIX_CHANNELS);
  sink->SetBitsPerSample(16);

  AudioGeneratorMP3* mp3 = new AudioGeneratorMP3();
  mp3->begin(src, sink);

  // Decode all frames — ConsumeSample inside the sink grows the buffer
  while (mp3->isRunning()) {
    if (!mp3->loop()) break;
  }

  mp3->stop();
  delete mp3;
  delete src;

  // Pull the decoded buffer out of the sink
  size_t   filled  = 0;
  int16_t* samples = sink->take(&filled);
  delete sink;

  if (!samples || filled == 0) {
    free(samples);
    ws->errorCode = 3;
    ws->errorMsg  = "Decode produced no samples";
    *wsOut = ws;
    return false;
  }

  ws->samples     = samples;
  ws->sampleCount = filled;
  *wsOut = ws;
  return true;
}

static void freeWavStream(WavStream** ws) {
  if (*ws) {
    free(ws->samples);
    free(ws);
  }
  return;
}