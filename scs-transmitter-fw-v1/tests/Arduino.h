#pragma once

#include <cstdint>
#include <deque>
#include <vector>
#include <utility>

using uint8_t = std::uint8_t;
using uint16_t = std::uint16_t;

constexpr int LOW = 0, HIGH = 1, OUTPUT = 1, INPUT = 0;
constexpr int MOSI = 11, MISO = 12, SCK = 13;

extern unsigned long clockMs;
extern std::deque<std::pair<unsigned long, int>> input;
extern std::vector<std::pair<int, int>> pins;
extern bool misoBusy;
extern bool gdo;
extern int forcedGdo;

inline unsigned long millis() {
  static unsigned calls = 0;
  if (++calls % 16 == 0) ++clockMs;
  return clockMs;
}
inline void delay(unsigned long ms) { clockMs += ms; }
inline void delayMicroseconds(unsigned int) {}

inline void pinMode(int, int) {}
void spiChipSelect(int value);
inline void digitalWrite(int pin, int value) {
  pins.emplace_back(pin, value);
  if (pin == 10) spiChipSelect(value);
}

inline int digitalRead(int pin) {
  if (pin == MISO) return misoBusy;
  if (forcedGdo >= 0) return forcedGdo;

  bool result = gdo;
  gdo = !gdo;
  return result;
}

struct SerialMock {
  void begin(int) {}
  int available() { return !input.empty() && input.front().first <= clockMs; }
  int read() { int value = input.front().second; input.pop_front(); return value; }
};

extern SerialMock Serial;
