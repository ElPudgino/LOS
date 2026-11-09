#pragma once

#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>
#include <utility>

#define F(text) text

struct MockSerial {
  std::string data;

  void begin(unsigned long baud) { assert(baud == 115200); }
  void print(const char *text) { data += text; }
  void write(const uint8_t *bytes, size_t size) {
    data.append(reinterpret_cast<const char *>(bytes), size);
  }
};

extern MockSerial Serial;

const uint8_t LOW = 0, HIGH = 1, INPUT = 0, OUTPUT = 1;
const uint8_t MOSI = 11, MISO = 12, SCK = 13;

inline std::vector<std::pair<uint8_t, uint8_t>> &pinWrites() {
  static std::vector<std::pair<uint8_t, uint8_t>> writes;
  return writes;
}

inline std::vector<unsigned int> &microsecondDelays() {
  static std::vector<unsigned int> delays;
  return delays;
}

extern uint32_t clockMs;
extern bool gdoHigh, misoHigh;

inline uint32_t millis() { return clockMs++; }

inline int digitalRead(uint8_t pin) { return pin == MISO ? misoHigh : gdoHigh; }
inline void digitalWrite(uint8_t pin, uint8_t value) {
  pinWrites().emplace_back(pin, value);
}
inline void pinMode(uint8_t, uint8_t) {}

inline void delay(unsigned long ms) { clockMs += ms; }
inline void delayMicroseconds(unsigned int us) { microsecondDelays().push_back(us); }
