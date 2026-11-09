#include <cassert>
#include <string>
#include <iostream>
#include "Arduino.h"
#include "SPI.h"

unsigned long clockMs = 0;
std::deque<std::pair<unsigned long, int>> input;
std::vector<std::pair<int, int>> pins;
std::vector<uint8_t> transfers;
bool misoBusy = false, gdo = true;
int forcedGdo = -1;
SerialMock Serial;
SPIMock SPI;

void spiChipSelect(int value) { SPI.chipSelect(value == LOW); }

#include "../scs-transmitter/scs-transmitter.ino"

using Bytes = std::vector<uint8_t>;

static void feed(const Bytes &bytes, unsigned long spacing = 1) {
  unsigned long time = clockMs;
  for (uint8_t c : bytes) {
    input.emplace_back(time, c);
    time += spacing;
  }
}

static void reset() {
  clockMs = 0;
  input.clear();
  pins.clear();
  transfers.clear();

  packetCount = 0;
  transmissionOk = true;
  radioReady = true;

  misoBusy = false;
  gdo = true;
  forcedGdo = -1;
  SPI = SPIMock{};
}

static void append(Bytes &to, const Bytes &from) {
  to.insert(to.end(), from.begin(), from.end());
}

static Bytes sensorRecord(uint8_t seq, uint32_t value) {
  Bytes result{seq, uint8_t(value >> 24), uint8_t(value >> 16),
               uint8_t(value >> 8), uint8_t(value)};
  result.push_back(crc8(result.data(), result.size()));
  return result;
}

static Bytes radioRecord(uint8_t sensor, uint8_t status = 0,
                         uint8_t seq = 0, uint32_t value = 0) {
  return Bytes{uint8_t((status << 4) | sensor), seq, uint8_t(value >> 24),
               uint8_t(value >> 16), uint8_t(value >> 8), uint8_t(value)};
}

// Decode SPI transactions, skipping payload bytes even when they equal 0x7f.
static std::vector<Bytes> packets() {
  std::vector<Bytes> result;
  for (size_t i = 0; i < transfers.size(); ++i) {
    if (transfers[i] != 0x7f) continue;
    assert(i + 1 < transfers.size());
    unsigned length = transfers[++i];
    assert(length >= 6 && length <= 30 && length % 6 == 0);
    assert(i + length < transfers.size());
    result.emplace_back(transfers.begin() + i + 1, transfers.begin() + i + length + 1);
    i += length;
  }
  return result;
}

static Bytes payload() {
  Bytes result;
  for (const auto &p : packets()) append(result, p);
  return result;
}

static Bytes formatted(const Channel &ch, uint8_t number = 3) {
  sendChannel(ch, number);
  flushPacket();
  return payload();
}

int main() {
  Channel ch{};
  const uint8_t check[] = {'1','2','3','4','5','6','7','8','9'};

  assert(crc8(check, sizeof(check)) == 0xf4);
  assert(sensorRecord(0, 125) == (Bytes{0, 0, 0, 0, 0x7d, 0x74}));

  reset();
  feed(Bytes{0, 0, 0, 0, 0x7d, 0x74});

  receiveChannel(ch);

  assert(ch.status == OK && ch.count == 1);
  assert(formatted(ch) == (Bytes{3, 0, 0, 0, 0, 0x7d}));

  // Binary LF/CR/NUL/0x7f are data, and counters are not interpreted.
  reset();
  Bytes special, specialExpected;
  for (uint8_t seq : {0xff, 0x00, 0x0a, 0x0d, 0x7f}) {
    append(special, sensorRecord(seq, 0x000a0d7f));
    append(specialExpected, radioRecord(3, 0, seq, 0x000a0d7f));
  }
  feed(special);

  receiveChannel(ch);

  assert(ch.status == OK && ch.count == 5);
  assert(formatted(ch) == specialExpected);

  // Every single-bit corruption, including the CRC byte, is rejected.
  for (unsigned byte = 0; byte < 6; ++byte) {
    for (unsigned bit = 0; bit < 8; ++bit) {
      reset();
      Bytes bad = sensorRecord(0xff, 0x12345678);
      bad[byte] ^= 1 << bit;
      feed(bad);

      receiveChannel(ch);

      assert(ch.status == PROTOCOL_ERROR && ch.count == 0);
      assert(formatted(ch) == radioRecord(3, 3));
    }
  }

  Bytes batch, expected;
  for (unsigned i = 0; i < 32; ++i) {
    append(batch, sensorRecord(i, 0xffffffffUL - i));
    append(expected, radioRecord(3, 0, i, 0xffffffffUL - i));
  }

  // Boundary sizes, with valid CRCs for every complete record.
  for (unsigned length : {0, 1, 5, 6, 12, 191, 192, 193}) {
    reset();
    Bytes bytes = batch;
    bytes.push_back(0);
    bytes.resize(length);
    feed(bytes);

    receiveChannel(ch);

    Status status = length == 0 ? NO_RESPONSE : length > 192 ? OVERFLOW :
                    length % 6 ? PROTOCOL_ERROR : OK;
    assert(ch.status == status);
    assert(ch.count == (status == OK ? length / 6 : 0));
    assert(formatted(ch) == (status == OK ? Bytes(expected.begin(), expected.begin() + length)
                                           : radioRecord(3, status)));
    assert(input.empty());
  }

  // A bad first, middle, or last CRC invalidates the whole response.
  for (unsigned record : {0, 15, 31}) {
    reset();
    Bytes bad = batch;
    bad[record * 6 + 5] ^= 1;
    feed(bad);

    receiveChannel(ch);

    assert(ch.status == PROTOCOL_ERROR && ch.count == 0 && input.empty());
    assert(formatted(ch) == radioRecord(3, 3));
  }

  // First terminal error wins even if the drained tail exceeds 192 bytes.
  reset();
  Bytes bad = batch;
  bad[5] ^= 1;
  bad.resize(240, 0);
  feed(bad);

  receiveChannel(ch);

  assert(ch.status == PROTOCOL_ERROR && ch.count == 0 && input.empty());

  // A new poll resets a terminal error and starts at a known record boundary.
  reset();
  feed(sensorRecord(9, 0));

  receiveChannel(ch);

  assert(ch.status == OK && ch.count == 1);
  assert(formatted(ch) == radioRecord(3, 0, 9, 0));

  // All status headers for all channels; error fields are zero.
  for (uint8_t sensor = 1; sensor <= 3; ++sensor) {
    for (uint8_t status = 0; status <= 4; ++status) {
      reset();
      ch.status = static_cast<Status>(status);
      ch.count = status == 0 ? 1 : 0;
      Bytes record = sensorRecord(0xff, 0xffffffffUL);
      memcpy(ch.data[0], record.data(), 6);

      assert(formatted(ch, sensor) == radioRecord(sensor, status,
             status == 0 ? 0xff : 0, status == 0 ? 0xffffffffUL : 0));
    }
  }

  // Keep bytes arriving without a gap until just before the overall deadline.
  for (bool partial : {false, true}) {
    reset();
    Bytes source(batch.begin(), batch.begin() + 60);
    for (unsigned i = 0; i < source.size(); ++i)
      input.emplace_back(i * (TIMEOUT_MS - 10) / (source.size() - 1), source[i]);
    if (partial) input.emplace_back(TIMEOUT_MS - 5, 0);

    receiveChannel(ch);

    assert(ch.status == (partial ? PROTOCOL_ERROR : TIMEOUT));
    assert(ch.count == (partial ? 0 : 10));

    Bytes want;
    if (!partial) want.assign(expected.begin(), expected.begin() + 60);
    append(want, radioRecord(3, partial ? 3 : 2));

    assert(formatted(ch) == want);
  }

  // If frame gap and overall timeout coincide, timeout takes precedence.
  reset();
  for (uint8_t c : sensorRecord(0, 125))
    input.emplace_back(TIMEOUT_MS - FRAME_GAP_MS, c);

  receiveChannel(ch);

  assert(ch.status == TIMEOUT && ch.count == 1);

  // Whole-record packet boundaries: five records fit; the sixth starts anew.
  reset();
  feed(Bytes(batch.begin(), batch.begin() + 36));

  receiveChannel(ch);

  assert(formatted(ch) == Bytes(expected.begin(), expected.begin() + 36));
  assert(packets().size() == 2 && packets()[0].size() == 30 && packets()[1].size() == 6);

  // Independent channels preserve all 32 records and physical sensor order.
  reset();
  Bytes all;
  for (unsigned i = 0; i < 3; ++i) {
    feed(batch);

    receiveChannel(channels[i]);

    assert(channels[i].count == 32 && channels[i].status == OK);
    for (unsigned j = 0; j < 32; ++j)
      append(all, radioRecord(SENSOR_NUMBERS[i], 0, j, 0xffffffffUL - j));
  }

  for (unsigned i = 0; i < 3; ++i) sendChannel(channels[i], SENSOR_NUMBERS[i]);
  flushPacket();

  assert(payload() == all && all.size() == 576);

  // Maximum timeout response: 32 measurements plus TO for each sensor.
  reset();
  all.clear();
  for (unsigned i = 0; i < 3; ++i) {
    channels[i].status = TIMEOUT;
    sendChannel(channels[i], SENSOR_NUMBERS[i]);
    for (unsigned j = 0; j < 32; ++j)
      append(all, radioRecord(SENSOR_NUMBERS[i], 0, j, 0xffffffffUL - j));
    append(all, radioRecord(SENSOR_NUMBERS[i], 2));
  }
  flushPacket();

  assert(payload() == all && all.size() == 594);

  // Terminal drain is bounded even if input never becomes quiet.
  reset();
  Bytes continuous(3 * DRAIN_LIMIT_MS, 0xff);
  feed(continuous);

  receiveChannel(ch);

  assert(ch.status == PROTOCOL_ERROR && ch.count == 0);
  assert(clockMs < DRAIN_LIMIT_MS + 100 && !input.empty());

  reset();

  setup();

  assert((pins[0] == std::make_pair(4, HIGH)));

  pins.clear();
  clockMs = 0;

  loop();

  assert(clockMs >= POLL_INTERVAL_MS);

  std::vector<int> requests;
  for (auto p : pins) if (p.second == LOW && p.first >= 4 && p.first <= 6)
    requests.push_back(p.first);

  assert((requests == std::vector<int>{4, 5, 6}));

  Bytes noResponses;
  for (uint8_t sensor : SENSOR_NUMBERS) append(noResponses, radioRecord(sensor, 1));

  assert(payload() == noResponses);

  // Actual loop: stale bytes are cleared, and distinct answers map D4/D5/D6
  // to sensors 3/2/1. Scheduled answers begin after each request.
  reset();
  input.emplace_back(0, 0xff);
  Bytes mapped;
  for (unsigned i = 0; i < 3; ++i) {
    unsigned long t = POLL_INTERVAL_MS + PULSE_MS + 10 + i * 100;
    for (uint8_t c : sensorRecord(i, i + 125)) input.emplace_back(t++, c);
    append(mapped, radioRecord(SENSOR_NUMBERS[i], 0, i, i + 125));
  }

  loop();

  assert(input.empty() && payload() == mapped);

  // Failure suppresses the rest of the cycle, without retrying the first packet.
  reset();
  forcedGdo = LOW;
  channels[0].count = 32;
  channels[0].status = TIMEOUT;

  sendChannel(channels[0], 3);

  const size_t failedTransfers = transfers.size();
  assert(!transmissionOk && !radioReady && packets().size() == 1);

  sendChannel(channels[1], 2);
  flushPacket();

  assert(transfers.size() == failedTransfers);

  // The next cycle reinitializes radio, then resumes transmission.
  forcedGdo = -1;
  transfers.clear();

  loop();

  assert(radioReady && transmissionOk && transfers.front() == 0x30);
  assert(payload() == noResponses);

  // Cold-start and retry both establish manual-reset GPIO levels before SPI.
  reset();
  for (unsigned attempt = 1; attempt <= 2; ++attempt) {
    pins.clear();

    assert(Radio::begin());

    const std::vector<std::pair<int, int>> expectedPins{
      {10, HIGH}, {SCK, HIGH}, {MOSI, LOW}, {10, LOW}, {10, HIGH}, {10, LOW}
    };

    assert(pins.size() >= expectedPins.size());
    assert((std::vector<std::pair<int, int>>(pins.begin(), pins.begin() + expectedPins.size())
            == expectedPins));
    assert(SPI.beginCount == attempt && SPI.endCount == attempt);
    assert(SPI.registers[0x04] == 0xd3 && SPI.registers[0x05] == 0x91);
    assert(SPI.registers[0x08] == 0x05 && SPI.registers[0x3e] == 0xc0);
  }

  // A stuck bus or ignored writes cannot masquerade as successful init.
  for (int stuck : {0x00, 0xff}) {
    reset();
    SPI.readOverride = stuck;

    assert(!Radio::begin());
    assert(clockMs < 1000);
  }

  reset();
  SPI.dropWrites = true;

  assert(!Radio::begin());

  // Verify every configured register, including PATABLE, not just identity.
  reset();
  assert(Radio::begin());
  const auto configured = SPI.registers;

  for (unsigned address = 0; address < configured.size(); ++address) {
    if (configured[address] == 0 && address != 0x0a && address != 0x0c && address != 0x17)
      continue;

    reset();
    SPI.corruptReadAddress = address;

    assert(!Radio::begin());
  }

  // Recovery after failed readback on the following cycle.
  SPI.corruptReadAddress = -1;

  assert(Radio::begin());

  reset();
  misoBusy = true;

  assert(!Radio::begin());
  assert(clockMs < 1000);
  assert(!Radio::send(reinterpret_cast<const uint8_t *>("x"), 1));

  for (int stuck : {LOW, HIGH}) {
    reset();
    forcedGdo = stuck;

    assert(!Radio::send(reinterpret_cast<const uint8_t *>("x"), 1));
    assert(clockMs < 1000);
    assert(transfers[transfers.size() - 2] == Radio::SIDLE);
    assert(transfers.back() == Radio::SFTX);
  }

  std::cout << "All host tests passed\n";
}
