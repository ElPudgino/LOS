#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "config.h"

class Burst {
 public:
  Burst() : size_(0), lastPacket_(0), active_(false), overflow_(false) {}

  // Only CRC-valid, nonempty packets reach this method.
  void append(const uint8_t *data, uint8_t length, uint32_t now) {
    // Validate the whole physical packet before touching group state.
    if (!length || length > 30 || length % 6) return;
    for (uint8_t i = 0; i < length; i += 6) {
      const uint8_t sensor = data[i] & 0x0f;
      const uint8_t status = data[i] >> 4;
      if (sensor < 1 || sensor > 3 || status > 4) return;
    }

    active_ = true;
    lastPacket_ = now;
    if (overflow_) return;

    if (length > sizeof(data_) - size_) {
      overflow_ = true;
      size_ = 0;
      return;
    }

    memcpy(data_ + size_, data, length);
    size_ += length;
  }

  template <typename Output>
  void flushIfExpired(uint32_t now, Output &output) {
    if (!active_ || uint32_t(now - lastPacket_) < BURST_TIMEOUT_MS) return;

    // Emit the entire frame synchronously; heartbeat cannot split it.
    output.print(F("BEGIN\n"));
    if (overflow_) {
      output.print(F("ERR BO\n"));
    } else {
      for (uint16_t i = 0; i < size_; i += 6) {
        emitRecord(data_ + i, output);
      }
    }
    output.print(F("END\n"));

    size_ = 0;
    active_ = false;
    overflow_ = false;
  }

 private:
  template <typename Output>
  static void emitRecord(const uint8_t *record, Output &output) {
    // Longest line: "3 FF 4294967.295\n". No ASCII copy of the group.
    char line[17];
    line[0] = '0' + (record[0] & 0x0f);
    line[1] = ' ';

    const uint8_t status = record[0] >> 4;
    if (status) {
      memcpy(line + 2, "ERR ", 4);
      switch (status) {
        case 1: line[6] = 'N'; line[7] = 'R'; break;
        case 2: line[6] = 'T'; line[7] = 'O'; break;
        case 3: line[6] = 'C'; line[7] = 'R'; break;
        case 4: line[6] = 'O'; line[7] = 'F'; break;
      }
      line[8] = '\n';
      output.write(reinterpret_cast<const uint8_t *>(line), 9);
      return;
    }

    const uint8_t high = record[1] >> 4, low = record[1] & 0x0f;
    line[2] = high < 10 ? '0' + high : 'A' + high - 10;
    line[3] = low < 10 ? '0' + low : 'A' + low - 10;
    line[4] = ' ';

    const uint32_t value = (uint32_t(record[2]) << 24) |
                           (uint32_t(record[3]) << 16) |
                           (uint32_t(record[4]) << 8) | record[5];
    uint32_t integer = value / 1000;
    const uint16_t fraction = value % 1000;

    uint32_t divisor = 1;
    while (divisor <= integer / 10) divisor *= 10;

    uint8_t size = 5;
    do {
      line[size++] = '0' + integer / divisor;
      integer %= divisor;
      divisor /= 10;
    } while (divisor);

    line[size++] = '.';
    line[size++] = '0' + fraction / 100;
    line[size++] = '0' + (fraction / 10) % 10;
    line[size++] = '0' + fraction % 10;
    line[size++] = '\n';

    output.write(reinterpret_cast<const uint8_t *>(line), size);
  }

  uint8_t data_[BURST_BUFFER_SIZE];
  uint16_t size_;
  uint32_t lastPacket_;
  bool active_;
  bool overflow_;
};
