#include <iostream>
#include "../scs-receiver/radio.h"

uint32_t clockMs = 0;
bool gdoHigh = false, misoHigh = false;
MockSPI SPI;

void status(uint8_t count) {
  SPI.expect(0xfb);
  SPI.expect(0, count);
}

void restart() {
  SPI.expect(0x36);
  SPI.expect(0x3a);
  SPI.expect(0x34);
}

void packet(uint8_t lqi) {
  status(5);
  SPI.expect(0x36);
  status(5);
  SPI.expect(0xff);
  SPI.expect(0, 2);
  SPI.expect(0, 'x');
  SPI.expect(0, '\n');
  SPI.expect(0, 0x40); // RSSI
  SPI.expect(0, lqi);

  restart();
}

// Golden RX profile: startup must preserve all values and verify every write.
static const uint8_t config[][2] = {
  {0x02, 0x06}, {0x03, 0x07}, {0x04, 0xd3}, {0x05, 0x91},
  {0x06, 32}, {0x07, 0x04}, {0x08, 0x05},
  {0x0a, 0x00}, {0x0b, 0x06}, {0x0c, 0x00},
  {0x0d, 0x10}, {0x0e, 0xb0}, {0x0f, 0x71},
  {0x10, 0x87}, {0x11, 0x83}, {0x12, 0x02},
  {0x13, 0x02}, {0x14, 0xf8}, {0x15, 0x47},
  {0x17, 0x30}, {0x18, 0x18}, {0x19, 0x16}, {0x1a, 0x1c},
  {0x1b, 0xc7}, {0x1c, 0x00}, {0x1d, 0xb2},
  {0x21, 0x56}, {0x22, 0x10}, {0x23, 0xe9}, {0x24, 0x2a},
  {0x25, 0x00}, {0x26, 0x1f}, {0x29, 0x59},
  {0x2c, 0x81}, {0x2d, 0x35}, {0x2e, 0x09}
};

static void expectBegin(int corruptAddress = -1, int stuckRead = -1) {
  SPI.expect(0x30);
  for (const auto &reg : config) {
    SPI.expect(reg[0]);
    SPI.expect(reg[1]);
  }

  for (const auto &reg : config) {
    uint8_t value = stuckRead >= 0 ? stuckRead :
                    reg[0] == corruptAddress ? reg[1] ^ 1 : reg[1];
    SPI.expect(reg[0] | 0x80);
    SPI.expect(0, value);
    if (value != reg[1]) return; // No SRX after failed readback.
  }

  restart();
}

static void testBegin() {
  misoHigh = false;

  // Both first initialization and retry perform the same GPIO reset.
  for (unsigned attempt = 1; attempt <= 2; ++attempt) {
    pinWrites().clear();
    microsecondDelays().clear();
    expectBegin();

    assert(Radio::begin());
    assert(SPI.transfers.empty());
    const std::vector<std::pair<uint8_t, uint8_t>> expectedPins{
      {10, HIGH}, {SCK, HIGH}, {MOSI, LOW}, {10, LOW}, {10, HIGH}, {10, LOW}
    };
    assert(pinWrites().size() >= expectedPins.size());
    assert((std::vector<std::pair<uint8_t, uint8_t>>(
      pinWrites().begin(), pinWrites().begin() + expectedPins.size()) == expectedPins));
    assert((microsecondDelays() == std::vector<unsigned int>{10, 40}));
    assert(SPI.endAt == 1 && SPI.beginAt == 6 && SPI.enabled);
    assert(SPI.beginCount == attempt && SPI.endCount == attempt);
  }

  // Corruption in any register rejects initialization, including the last one.
  for (const auto &reg : config) {
    expectBegin(reg[0]);

    assert(!Radio::begin());
    assert(SPI.transfers.empty());
    assert((pinWrites().back() == std::make_pair(uint8_t(10), HIGH)));
  }

  // MISO stuck low or a disconnected bus returning FF cannot pass readback.
  for (int stuck : {0x00, 0xff}) {
    expectBegin(-1, stuck);

    assert(!Radio::begin());
    assert(SPI.transfers.empty());
  }

  // Readiness failure is bounded and leaves CSn high without enabling SPI.
  misoHigh = true;
  const uint32_t start = clockMs;
  const unsigned attempts = SPI.beginCount;

  assert(!Radio::begin());
  assert(uint32_t(clockMs - start) < 200);
  assert(SPI.beginCount == attempts && !SPI.enabled);
  assert((pinWrites().back() == std::make_pair(uint8_t(10), HIGH)));

  uint8_t value = 0x55;

  assert(!Radio::readReg(0x04, value) && value == 0x55);
  assert(SPI.transfers.empty());

  // Recovery after a failed attempt must still enter RX normally.
  misoHigh = false;
  expectBegin();

  assert(Radio::begin() && SPI.transfers.empty());
}

int main() {
  testBegin();

  uint8_t bytes[32], length = 99;

  status(0);

  assert(Radio::poll(bytes, length) && length == 0);

  packet(0x80);

  assert(Radio::poll(bytes, length) && length == 2);
  assert(bytes[0] == 'x' && bytes[1] == '\n');

  packet(0x7f);

  assert(Radio::poll(bytes, length) && length == 0);

  // Full-size packet: read all 32 bytes and both appended status bytes.
  status(35);
  SPI.expect(0x36);
  status(35);
  SPI.expect(0xff);
  SPI.expect(0, 32);
  for (uint8_t i = 0; i < 32; ++i) SPI.expect(0, i);
  SPI.expect(0, 0x40);
  SPI.expect(0, 0x80);
  restart();

  assert(Radio::poll(bytes, length) && length == 32);
  for (uint8_t i = 0; i < 32; ++i) assert(bytes[i] == i);

  status(0x80);
  restart();

  assert(Radio::poll(bytes, length) && length == 0);

  status(3); // Incomplete/invalid FIFO, no read past its end.
  SPI.expect(0x36);
  status(3);
  restart();

  assert(Radio::poll(bytes, length) && length == 0);

  status(5);
  SPI.expect(0x36);
  status(5);
  SPI.expect(0xff);
  SPI.expect(0, 33); // Invalid length byte.
  restart();

  assert(Radio::poll(bytes, length) && length == 0);

  status(5);
  SPI.expect(0x36);
  status(5);
  SPI.expect(0xff);
  SPI.expect(0, 1); // Count/length mismatch.
  restart();

  assert(Radio::poll(bytes, length) && length == 0);

  gdoHigh = true;

  assert(Radio::poll(bytes, length) && length == 0);

  clockMs += 200;
  restart();

  assert(Radio::poll(bytes, length) && length == 0);

  gdoHigh = false;
  misoHigh = true;

  assert(!Radio::poll(bytes, length) && length == 0);
  assert(SPI.transfers.empty());

  std::cout << "radio tests passed\n";
}
