#pragma once

#include <cstdint>
#include <vector>
#include <array>

constexpr int MSBFIRST = 1, SPI_MODE0 = 0;
struct SPISettings { SPISettings(int, int, int) {} };
extern std::vector<uint8_t> transfers;

struct SPIMock {
  std::array<uint8_t, 64> registers{};
  bool enabled = false, selected = false, header = true;
  bool reading = false, dropWrites = false;
  int readOverride = -1, corruptReadAddress = -1;
  uint8_t address = 0;
  unsigned beginCount = 0, endCount = 0;

  void chipSelect(bool active) {
    selected = active;
    header = true;
  }

  void begin() { enabled = true; ++beginCount; }
  void end() { enabled = false; ++endCount; }
  void beginTransaction(SPISettings) {}
  void endTransaction() {}

  uint8_t transfer(uint8_t value) {
    transfers.push_back(value);
    if (!selected || !enabled) return 0xff;

    if (header) {
      header = false;
      address = value & 0x3f;
      reading = value & 0x80;
      if (value == 0x30) registers.fill(0); // SRES
      return 0;
    }

    if (reading) {
      if (readOverride >= 0) return readOverride;
      return registers[address] ^ (address == corruptReadAddress ? 1 : 0);
    }

    if (!dropWrites && address < 0x3f) registers[address] = value;
    return 0;
  }
};

extern SPIMock SPI;
