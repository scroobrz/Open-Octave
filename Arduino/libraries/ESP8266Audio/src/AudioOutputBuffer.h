#pragma once
#include "AudioOutput.h"

class AudioOutputBuffer : public AudioOutput {
public:
  AudioOutputBuffer()
    : _buf(nullptr), _filled(0), _capacity(0), _bps(16) {}

  ~AudioOutputBuffer() {
    free(_buf);
  }

  bool begin() override { return true; }
  bool stop()  override { return true; }

  // Called by the MP3 decoder once per stereo frame
  bool ConsumeSample(int16_t sample[2]) override {
    size_t needed = _filled + channels; // channels == 1 or 2
    if (needed > _capacity) {
      size_t newCap = (_capacity == 0) ? 4096 : _capacity * 2;
      if (newCap < needed) newCap = needed;
      int16_t* tmp = (int16_t*)realloc(_buf, newCap * sizeof(int16_t));
      if (!tmp) return false;   // signal backpressure to decoder
      _buf      = tmp;
      _capacity = newCap;
    }
    if (channels == 2) {
      _buf[_filled++] = sample[0];   // L
      _buf[_filled++] = sample[1];   // R
    } else {
      _buf[_filled++] = sample[0];   // mono
    }
    return true;
  }

  bool SetBitsPerSample(int bps) {
  this->_bps = bps;
  return true;

 }

  // Hand ownership of the buffer to the caller — buffer is no longer ours
  int16_t* take(size_t* outFilled) {
    int16_t* ret = _buf;
    *outFilled   = _filled;
    _buf     = nullptr;
    _filled  = 0;
    _capacity = 0;
    return ret;
  }

private:
  int16_t* _buf;
  size_t   _filled;
  size_t   _capacity;
  size_t   _bps;
};

