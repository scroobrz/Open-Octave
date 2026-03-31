#ifndef MOCK_WIRE_H
#define MOCK_WIRE_H

#include <cstdint>

class TwoWire {
public:
    void begin(uint8_t sda, uint8_t scl);
    void begin();
    void beginTransmission(uint8_t address);
    uint8_t endTransmission(bool sendStop = true);
    uint8_t endTransmission(void);
    size_t requestFrom(uint8_t address, size_t len, bool stop = true);
    size_t requestFrom(uint8_t address, size_t len);

    void write(uint8_t data);
    void write(uint8_t *data, size_t quantity);
    void write(const char *str);

    uint8_t available(void);
    uint8_t read(void);
    uint8_t readBytes(uint8_t *buffer, size_t length);
    void setClock(uint32_t freq);
    void onReceive(void (*function)(int));
    void onRequest(void (*function)(void));

    void beginTransmission(int address);
    uint8_t endTransmission(int sendStop);
};

extern TwoWire Wire;
extern TwoWire Wire1;
extern TwoWire Wire2;

#endif // MOCK_WIRE_H
