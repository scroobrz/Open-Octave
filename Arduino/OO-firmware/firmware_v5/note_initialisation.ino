
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
static bool decodeMp3ToPcm(const char* path, WavStream* ws) {
  ws.samples     = nullptr;
  ws.sampleCount = 0;
  ws.cursor      = 0;
  ws.sampleRate  = MIX_SAMPLE_RATE;
  ws.channels    = MIX_CHANNELS;
  ws.errorCode   = 0;
  ws.errorMsg    = nullptr;

  AudioFileSourceSD* src = new AudioFileSourceSD(path);
  if (!src->isOpen()) {
    delete src;
    ws.errorCode = 1;
    ws.errorMsg  = "File not found";
    return false;
  }

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
    ws.errorCode = 2;
    ws.errorMsg  = "Out of memory";
    return false;
  }

  int16_t frameBuf[FRAME_SAMPLES * MIX_CHANNELS];

  while (mp3->isRunning()) {
    if (!mp3->loop()) break;

    int got = sink->ConsumeSamples(frameBuf, FRAME_SAMPLES * MIX_CHANNELS);
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

  if (!buf || filled == 0) {
    free(buf);
    ws.errorCode = 3;
    ws.errorMsg  = "Decode produced no samples";
    return false;
  }

  ws.samples     = buf;
  ws.sampleCount = filled;
  return true;
}