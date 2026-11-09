#pragma once

#include "Arduino.h"
#include <assert.h>
#include <deque>
#include <stdint.h>
#include <utility>

const uint8_t MSBFIRST = 1, SPI_MODE0 = 0;

struct SPISettings {
  SPISettings(unsigned long, uint8_t, uint8_t) {}
};

struct MockSPI {
  std::deque<std::pair<uint8_t, uint8_t> > transfers;
  bool enabled = false;
  size_t beginAt = 0, endAt = 0;
  unsigned beginCount = 0, endCount = 0;

  void begin() {
    enabled = true;
    beginAt = pinWrites().size();
    ++beginCount;
  }

  void end() {
    enabled = false;
    endAt = pinWrites().size();
    ++endCount;
  }

  void beginTransaction(SPISettings) {}
  void endTransaction() {}

  uint8_t transfer(uint8_t value) {
    assert(!transfers.empty());

    const auto expected = transfers.front();
    transfers.pop_front();

    assert(value == expected.first);
    return expected.second;
  }

  void expect(uint8_t value, uint8_t result = 0) {
    transfers.push_back(std::make_pair(value, result));
  }
};

extern MockSPI SPI;
