#include <iostream>
#include "../scs-receiver/scs-receiver.ino"

uint32_t clockMs = 0;
bool gdoHigh = false, misoHigh = true;
MockSPI SPI;
MockSerial Serial;

void status(uint8_t count) {
  SPI.expect(0xfb);
  SPI.expect(0, count);
}

std::string record(uint8_t sensor, uint8_t state, uint8_t seq = 0,
                   uint32_t milli = 0) {
  const uint8_t bytes[] = {
    uint8_t((state << 4) | sensor), seq,
    uint8_t(milli >> 24), uint8_t(milli >> 16),
    uint8_t(milli >> 8), uint8_t(milli)
  };
  return std::string(reinterpret_cast<const char *>(bytes), sizeof(bytes));
}

void packet(const std::string &payload, bool crcOk = true) {
  status(payload.size() + 3);
  SPI.expect(0x36);
  status(payload.size() + 3);
  SPI.expect(0xff);
  SPI.expect(0, payload.size());
  for (unsigned char byte : payload) SPI.expect(0, byte);
  SPI.expect(0, 0x40);
  SPI.expect(0, crcOk ? 0x80 : 0);

  SPI.expect(0x36);
  SPI.expect(0x3a);
  SPI.expect(0x34);
}

void idleAt(uint32_t now) {
  clockMs = now;
  status(0);
  loop();
}

// Exercise rejection through Radio::poll and loop(), not just Burst::append.
void rejectsWithoutActivity(const std::string &payload, bool crcOk = true) {
  const uint32_t start = clockMs + 2000;
  clockMs = lastHeartbeat = start;
  Serial.data.clear();

  packet(payload, crcOk);
  loop();

  assert(Serial.data.empty());

  idleAt(start + BURST_TIMEOUT_MS + 100);

  assert(Serial.data == "BEAT\n"); // Rejected packet did not create a group.

  clockMs = lastHeartbeat = start + 3000;
  Serial.data.clear();

  packet(record(2, 0, 0xab, 0x01020304));
  loop();
  const uint32_t acceptedAt = clockMs - 1;

  assert(Serial.data.empty());

  clockMs = acceptedAt + BURST_TIMEOUT_MS / 2;
  packet(payload, crcOk);
  loop();
  const uint32_t rejectedAt = clockMs - 1;

  assert(Serial.data.empty());

  idleAt(acceptedAt + BURST_TIMEOUT_MS);

  const std::string expected = "BEGIN\n2 AB 16909.060\nEND\nBEAT\n";
  assert(Serial.data == expected); // Rejected packet did not extend the group.

  idleAt(rejectedAt + BURST_TIMEOUT_MS);

  assert(Serial.data == expected); // Nor leave a second/partial group behind.
  assert(SPI.transfers.empty());
}

int main() {
  setup(); // MISO stuck high: bounded startup failure, no SPI transfers.

  assert(!radioReady && Serial.data.empty());

  clockMs = 1000;
  loop();

  assert(Serial.data == "BEAT\n");

  clockMs = 2000;
  loop(); // Heartbeat continues through radio retry failure.

  assert(!radioReady && Serial.data == "BEAT\nBEAT\n");

  misoHigh = false;
  radioReady = true;
  clockMs = 2500;
  packet(record(3, 0, 0, 125));
  loop();

  assert(Serial.data == "BEAT\nBEAT\n");

  clockMs = 3000;
  status(0);
  loop(); // Heartbeat while a group is buffered is outside its frame.

  assert(Serial.data == "BEAT\nBEAT\nBEAT\n");

  clockMs = 4000;
  status(0);
  loop(); // Expiry and heartbeat due in the same iteration.

  assert(Serial.data == "BEAT\nBEAT\nBEAT\nBEGIN\n3 00 0.125\nEND\nBEAT\n");

  // Exact timeout boundary: the next packet belongs to a new group.
  clockMs = 5000;
  packet(record(2, 0, 0xff, 4117));
  loop();
  const uint32_t boundary = clockMs - 1 + BURST_TIMEOUT_MS;

  clockMs = boundary;
  packet(record(1, 1));
  loop();

  assert(Serial.data.find("BEGIN\n2 FF 4.117\nEND\nBEAT\n") != std::string::npos);
  assert(Serial.data.find("1 ERR NR\n") == std::string::npos);

  idleAt(boundary + 1100);

  assert(Serial.data.find("BEGIN\n2 FF 4.117\nEND\nBEAT\nBEGIN\n1 ERR NR\nEND\n") != std::string::npos);

  // All legal packet lengths and error status codes pass through the radio.
  for (unsigned count = 1; count <= 5; ++count) {
    const uint32_t start = clockMs + 2000;
    clockMs = lastHeartbeat = start;
    Serial.data.clear();
    std::string payload = record(3, 0, 0x0a, 1001);
    std::string expected = "BEGIN\n3 0A 1.001\n";
    const char *errors[] = {"NR", "TO", "CR", "OF"};
    for (unsigned state = 1; state < count; ++state) {
      payload += record(1, state);
      expected += std::string("1 ERR ") + errors[state - 1] + "\n";
    }

    packet(payload);
    loop();

    assert(Serial.data.empty());

    idleAt(clockMs - 1 + BURST_TIMEOUT_MS);

    assert(Serial.data == expected + "END\nBEAT\n");
  }

  rejectsWithoutActivity(record(1, 0, 0x12, 1234), false);

  for (unsigned length : {1u, 5u, 7u, 31u, 32u})
    rejectsWithoutActivity(std::string(length, '\x01'));

  for (const std::string &invalid : {
         record(0, 0), record(4, 0), record(1, 5), record(3, 15)}) {
    rejectsWithoutActivity(invalid);
    rejectsWithoutActivity(record(1, 0, 0x55, 1234) + invalid);
    rejectsWithoutActivity(invalid + record(3, 0, 0xaa, 5678));
  }

  assert(SPI.transfers.empty());

  std::cout << "loop/heartbeat tests passed\n";
}
