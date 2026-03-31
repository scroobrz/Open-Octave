// Platform constants to make tests compile without ESP32/Arduino environment

// Math constants
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ESP32/I2S constants (stubbed)
#define I2S_NUM 0
#define portMAX_DELAY 1000

// Audio synthesis constants
#define SAMPLE_RATE 44100
#define DMA_BUF_LEN 256
#define VOLUME 0.8f
